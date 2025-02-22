//+------------------------------------------------------------------+
//|                                                      SmartOB.mq5 |
//|                        Copyright 2023, ForexBee & AI Assistant   |
//|                                       https://www.forexbee.com   |
//+------------------------------------------------------------------+
#property copyright "ForexBee"
#property version   "4.0"
#property description "Ultimate EA: Order Block + Multi-Timeframe + Dynamic Risk"

#include <Trade\Trade.mqh>
#include <Math\Stat\Math.mqh>

//--- Input Parameters
input group "Strategy Settings"
input int      MagicNumber = 2023;          // Magic Number
input int      MaxTradesPerDay = 30;        // Max Trades/Day
input bool     UseDynamicRisk = true;       // Enable Dynamic Risk

input group "Order Block Parameters"
input int      OB_Lookback = 50;            // OB Analysis Period
input double   OB_VolumeMult = 1.5;         // Volume Multiplier
input double   FibLevel1 = 61.8;            // Fibonacci Level 1 (%)
input double   FibLevel2 = 50.0;            // Fibonacci Level 2 (%)

input group "Indicator Parameters"
input int      RSIPeriod = 14;              // RSI Period
input int      EMAFast = 50;                // Fast EMA
input int      EMASlow = 200;               // Slow EMA
input double   RSIOverbought = 70.0;        // RSI Overbought
input double   RSIOversold = 30.0;          // RSI Oversold
input int      ATRPeriod = 14;              // ATR Period
input double   ATRMult = 2.0;               // ATR Multiplier

input group "Timeframe Settings"
input ENUM_TIMEFRAMES HighTF1 = PERIOD_H1;  // Trend TF 1
input ENUM_TIMEFRAMES HighTF2 = PERIOD_M30; // Trend TF 2
input ENUM_TIMEFRAMES EntryTF = PERIOD_M5;  // Entry Timeframe

//--- Global Variables
CTrade trade;
datetime lastTradeTime;
int tradeCountToday;
double equity;
string symbols[2] = {"XAUUSD","EURUSD"};

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
   trade.SetExpertMagicNumber(MagicNumber);
   EventSetTimer(3600);
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
   if(!IsNewBar() || TimeCurrent()-lastTradeTime<2880) return;
   
   equity = AccountInfoDouble(ACCOUNT_EQUITY);
   
   for(int i=0; i<ArraySize(symbols); i++)
   {
      string symbol = symbols[i];
      ENUM_OB_TYPE obType = DetectOB(symbol);
      bool trendUp = GetTrend(symbol,HighTF1) && GetTrend(symbol,HighTF2);
      
      if(trendUp && obType==BOB && CheckEntry(symbol,true)) ExecuteTrade(symbol,ORDER_TYPE_BUY,obType);
      else if(!trendUp && obType==BEAROB && CheckEntry(symbol,false)) ExecuteTrade(symbol,ORDER_TYPE_SELL,obType);
   }
}

//+------------------------------------------------------------------+
//| Order Block Detection                                            |
//+------------------------------------------------------------------+
enum ENUM_OB_TYPE {BOB,BEAROB};
ENUM_OB_TYPE DetectOB(string symbol)
{
   MqlRates rates[];
   CopyRates(symbol,HighTF1,0,OB_Lookback,rates);
   
   double fibLevel = CalculateFib(rates,FibLevel1);
   double priceGap = SymbolInfoDouble(symbol,SYMBOL_POINT)*50;
   
   bool bobCondition = rates[0].close>rates[0].open && 
                      rates[0].volume>rates[1].volume*OB_VolumeMult &&
                      MathAbs(rates[0].open-fibLevel)<priceGap;
                      
   bool bearobCondition = rates[0].close<rates[0].open && 
                         rates[0].volume>rates[1].volume*OB_VolumeMult &&
                         MathAbs(rates[0].open-fibLevel)<priceGap;
   
   if(bobCondition) return BOB;
   if(bearobCondition) return BEAROB;
   
   return WRONG_VALUE;
}

//+------------------------------------------------------------------+
//| Trend Direction                                                  |
//+------------------------------------------------------------------+
bool GetTrend(string symbol,ENUM_TIMEFRAMES tf)
{
   double emaFast = iMA(symbol,tf,EMAFast,0,MODE_EMA,PRICE_CLOSE);
   double emaSlow = iMA(symbol,tf,EMASlow,0,MODE_EMA,PRICE_CLOSE);
   return emaFast>emaSlow;
}

//+------------------------------------------------------------------+
//| Entry Conditions                                                 |
//+------------------------------------------------------------------+
bool CheckEntry(string symbol,bool isLong)
{
   double rsi = iRSI(symbol,EntryTF,RSIPeriod,PRICE_CLOSE);
   bool fvg = CheckFVG(symbol,isLong);
   bool mss = isLong ? CheckMSS(symbol,true) : CheckMSS(symbol,false);
   
   return isLong ? (rsi<RSIOversold && fvg && mss) : (rsi>RSIOverbought && fvg && mss);
}

//+------------------------------------------------------------------+
//| Fair Value Gap Detection                                         |
//+------------------------------------------------------------------+
bool CheckFVG(string symbol,bool isLong)
{
   MqlRates rates[];
   CopyRates(symbol,EntryTF,0,3,rates);
   
   if(isLong) return rates[1].low>rates[0].high && rates[1].low>rates[2].high;
   return rates[1].high<rates[0].low && rates[1].high<rates[2].low;
}

//+------------------------------------------------------------------+
//| Market Structure Shift                                           |
//+------------------------------------------------------------------+
bool CheckMSS(string symbol,bool isLong)
{
   double hi = iHigh(symbol,HighTF1,iHighest(symbol,HighTF1,MODE_HIGH,50,0));
   double lo = iLow(symbol,HighTF1,iLowest(symbol,HighTF1,MODE_LOW,50,0));
   return isLong ? (iClose(symbol,EntryTF,0)>hi) : (iClose(symbol,EntryTF,0)<lo);
}

//+------------------------------------------------------------------+
//| Trade Execution                                                  |
//+------------------------------------------------------------------+
void ExecuteTrade(string symbol,ENUM_ORDER_TYPE type,ENUM_OB_TYPE obType)
{
   double price = type==ORDER_TYPE_BUY ? SymbolInfoDouble(symbol,SYMBOL_ASK) : SymbolInfoDouble(symbol,SYMBOL_BID);
   double atr = iATR(symbol,EntryTF,ATRPeriod)*ATRMult;
   double sl = type==ORDER_TYPE_BUY ? price-atr : price+atr;
   double risk = CalculateRisk();
   double lot = MathMin(OptimalLot(symbol,risk,price,sl),SymbolInfoDouble(symbol,SYMBOL_VOLUME_MAX));
   
   if(trade.PositionOpen(symbol,type,lot,price,sl,0,"AutoTrade"))
   {
      tradeCountToday++;
      lastTradeTime = TimeCurrent();
      LogTrade(symbol,type,obType,lot,sl);
   }
}

//+------------------------------------------------------------------+
//| Risk Management                                                  |
//+------------------------------------------------------------------+
double CalculateRisk()
{
   if(!UseDynamicRisk) return 2.0;
   if(equity<=10) return 20.0;
   if(equity<=200) return 2.0;
   return 1.0;
}

double OptimalLot(string symbol,double risk,double entry,double sl)
{
   double riskAmount = equity*(risk/100);
   double tickValue = SymbolInfoDouble(symbol,SYMBOL_TRADE_TICK_VALUE);
   double points = MathAbs(entry-sl)/SymbolInfoDouble(symbol,SYMBOL_POINT);
   return NormalizeDouble(riskAmount/(points*tickValue),2);
}

//+------------------------------------------------------------------+
//| CSV Reporting                                                    |
//+------------------------------------------------------------------+
void LogTrade(string symbol,ENUM_ORDER_TYPE type,ENUM_OB_TYPE obType,double lot,double sl)
{
   int handle = FileOpen("SmartOB_Trades.csv",FILE_WRITE|FILE_CSV|FILE_ANSI,",");
   FileSeek(handle,0,SEEK_END);
   
   string obName = obType==BOB ? "BullishOB" : "BearishOB";
   string direction = type==ORDER_TYPE_BUY ? "BUY" : "SELL";
   
   FileWrite(handle,
      TimeToString(TimeCurrent()),
      symbol,
      direction,
      obName,
      DoubleToString(lot,2),
      DoubleToString(sl,5),
      DoubleToString(equity,2)
   );
   
   FileClose(handle);
}

//+------------------------------------------------------------------+
//| Utility Functions                                                |
//+------------------------------------------------------------------+
bool IsNewBar()
{
   static datetime lastBar;
   datetime currentBar = iTime(_Symbol,_Period,0);
   if(lastBar!=currentBar) { lastBar=currentBar; return true; }
   return false;
}

double CalculateFib(MqlRates &rates[],double level)
{
   double high = rates[ArrayMaximum(rates)].high;
   double low = rates[ArrayMinimum(rates)].low;
   return high - ((high-low)*(level/100));
}

//+------------------------------------------------------------------+
//| Timer & Deinit                                                   |
//+------------------------------------------------------------------+
void OnTimer()
{
   MqlDateTime today;
   TimeToStruct(TimeCurrent(),today);
   static int lastDay = -1;
   if(today.day!=lastDay) { tradeCountToday=0; lastDay=today.day; }
}

void OnDeinit(const int reason)
{
   EventKillTimer();
   FileDelete("SmartOB_Trades.csv");
}