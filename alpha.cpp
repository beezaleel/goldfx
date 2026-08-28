#include <Trade\Trade.mqh>

MqlTradeRequest request;
MqlTradeResult  result;

int    magicNumber = 209814;

// ── Trade state ───────────────────────────────────────────────────────────────
static bool     buying           = false;
static bool     selling          = false;
static int      entryCount       = 0;      // how many entries placed in current trade
static double   entryPrice       = 0.0;

// ── Setup state ───────────────────────────────────────────────────────────────
static double   zoneHigh         = -1;     // consolidation zone high
static double   zoneLow          = -1;     // consolidation zone low
static int      breakoutDir      = 0;      // 1=buy breakout, -1=sell breakout, 0=none
static int      confirmCount     = 0;      // confirmation candles counted so far
static bool     waitingConfirm   = false;  // breakout detected, counting confirms
static datetime zoneBarTime      = 0;      // time of zone detection (avoid re-detect)
static datetime lastProcessedBar = 0;      // one-bar-per-tick guard

// ── Exit state ────────────────────────────────────────────────────────────────
static int      exitCandleCount  = 0;      // consecutive candles outside MA50
static datetime lastExitBar      = 0;      // last bar we checked exit candles on
static int      consecutiveLosses = 0;     // consecutive losing trade count

// ── Inputs ────────────────────────────────────────────────────────────────────
input double Money_FixLot_Lots        = 0.01;
input int    RangeBars                = 10;   // consolidation window (bars)
input int    MinimumConsolidationBars = 5;    // min bars inside zone
input double MaxConsolidationPoints   = 400;  // max zone height in points
input int    BreakoutConfirmBars      = 3;    // confirmation candles before entry
input int    ExitCandlesBase          = 3;    // exit candles needed on first entry (reduces per additional entry)
input int    MaxEntries               = 2;    // max entries per setup
input int    MASlopeLookback          = 50;   // bars back to compare MA50 slope
input int    MaxConsecutiveLosses     = 3;    // stop trading after this many consecutive losses
input double TakeProfit               = 30.0; // close all when account profit reaches this in dollars

// ── Helpers ───────────────────────────────────────────────────────────────────

int CountSymbolPositions()
{
   int count = 0;
   for (int i = 0; i < PositionsTotal(); i++)
      if (PositionGetSymbol(i) == _Symbol) count++;
   return count;
}

double calcSMA50(int startBar)
{
   double sum = 0;
   for (int i = startBar; i < startBar + 50; i++)
      sum += iClose(_Symbol, _Period, i);
   return sum / 50.0;
}

// Detects a consolidation zone in the last RangeBars bars.
// Returns the zone high and low via output params. Returns true if valid.
bool detectConsolidation(double &outHigh, double &outLow)
{
   double hi = -DBL_MAX, lo = DBL_MAX;
   int    count = 0;

   for (int i = 1; i <= RangeBars; i++)
   {
      double barHi = iHigh(_Symbol, _Period, i);
      double barLo = iLow(_Symbol,  _Period, i);
      if (barHi > hi) hi = barHi;
      if (barLo < lo) lo = barLo;
   }

   if ((hi - lo) > MaxConsolidationPoints * _Point) return false;

   for (int i = 1; i <= RangeBars; i++)
   {
      double barHi = iHigh(_Symbol, _Period, i);
      double barLo = iLow(_Symbol,  _Period, i);
      if (barHi <= hi && barLo >= lo) count++;
   }

   if (count < MinimumConsolidationBars) return false;

   outHigh = hi;
   outLow  = lo;
   return true;
}

// ── Order execution ───────────────────────────────────────────────────────────

void ExecBuy()
{
   double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   ZeroMemory(request);
   request.action       = TRADE_ACTION_DEAL;
   request.type         = ORDER_TYPE_BUY;
   request.symbol       = _Symbol;
   request.volume       = Money_FixLot_Lots;
   request.type_filling = ORDER_FILLING_FOK;
   request.price        = ask;
   request.sl           = 0;
   request.tp           = 0;
   request.deviation    = 50;
   OrderSend(request, result);

   if (entryCount == 0) entryPrice = ask;
   entryCount++;
   buying = true;
}

void ExecSell()
{
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   ZeroMemory(request);
   request.action       = TRADE_ACTION_DEAL;
   request.type         = ORDER_TYPE_SELL;
   request.symbol       = _Symbol;
   request.volume       = Money_FixLot_Lots;
   request.type_filling = ORDER_FILLING_FOK;
   request.price        = bid;
   request.sl           = 0;
   request.tp           = 0;
   request.deviation    = 50;
   OrderSend(request, result);

   if (entryCount == 0) entryPrice = bid;
   entryCount++;
   selling = true;
}

void CloseAllPositions()
{
   double profit = AccountInfoDouble(ACCOUNT_PROFIT);

   CTrade trade;
   for (int i = PositionsTotal() - 1; i >= 0; i--)
   {
      if (PositionGetSymbol(i) == _Symbol)
         trade.PositionClose(PositionGetInteger(POSITION_TICKET));
   }

   if (profit < 0)
      consecutiveLosses++;
   else
      consecutiveLosses = 0;

   buying          = false;
   selling         = false;
   entryCount      = 0;
   entryPrice      = 0.0;
   exitCandleCount = 0;
}

void ResetSetup()
{
   zoneHigh      = -1;
   zoneLow       = -1;
   breakoutDir   = 0;
   confirmCount  = 0;
   waitingConfirm = false;
   zoneBarTime   = 0;
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

void DrawZone(double hi, double lo)
{
   ObjectDelete(0, "AlphaZoneHigh");
   ObjectDelete(0, "AlphaZoneLow");

   ObjectCreate(0, "AlphaZoneHigh", OBJ_HLINE, 0, 0, hi);
   ObjectSetInteger(0, "AlphaZoneHigh", OBJPROP_COLOR, clrSilver);
   ObjectSetInteger(0, "AlphaZoneHigh", OBJPROP_STYLE, STYLE_DASH);
   ObjectSetInteger(0, "AlphaZoneHigh", OBJPROP_WIDTH, 1);

   ObjectCreate(0, "AlphaZoneLow", OBJ_HLINE, 0, 0, lo);
   ObjectSetInteger(0, "AlphaZoneLow", OBJPROP_COLOR, clrSilver);
   ObjectSetInteger(0, "AlphaZoneLow", OBJPROP_STYLE, STYLE_DASH);
   ObjectSetInteger(0, "AlphaZoneLow", OBJPROP_WIDTH, 1);
}

void DrawEntry(bool isBuy)
{
   string name  = (isBuy ? "AlphaBuy_" : "AlphaSell_") + IntegerToString(TimeCurrent());
   string label = (isBuy ? "AlphaBuyLbl_" : "AlphaSellLbl_") + IntegerToString(TimeCurrent());
   double price = isBuy ? SymbolInfoDouble(_Symbol, SYMBOL_ASK) : SymbolInfoDouble(_Symbol, SYMBOL_BID);
   color  clr   = isBuy ? clrLime : clrRed;

   ObjectCreate(0, name, isBuy ? OBJ_ARROW_UP : OBJ_ARROW_DOWN, 0, TimeCurrent(), price);
   ObjectSetInteger(0, name, OBJPROP_COLOR, clr);
   ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);

   ObjectCreate(0, label, OBJ_TEXT, 0, TimeCurrent(), isBuy ? price - 150 * _Point : price + 150 * _Point);
   ObjectSetString(0,  label, OBJPROP_TEXT,     isBuy ? "BUY" : "SELL");
   ObjectSetInteger(0, label, OBJPROP_COLOR,    clr);
   ObjectSetInteger(0, label, OBJPROP_FONTSIZE, 8);
   ObjectSetString(0,  label, OBJPROP_FONT,     "Arial");
   ObjectSetInteger(0, label, OBJPROP_ANCHOR,   isBuy ? ANCHOR_TOP : ANCHOR_BOTTOM);
}

// ── Main logic ────────────────────────────────────────────────────────────────

void OnTick()
{
   if (consecutiveLosses >= MaxConsecutiveLosses) return;

   datetime currentBar = iTime(_Symbol, _Period, 0);
   if (currentBar == lastProcessedBar) return;
   lastProcessedBar = currentBar;

   double ma50      = calcSMA50(0);
   double prevOpen  = iOpen(_Symbol,  _Period, 1);
   double prevClose = iClose(_Symbol, _Period, 1);
   double prevHigh  = iHigh(_Symbol,  _Period, 1);
   double prevLow   = iLow(_Symbol,   _Period, 1);
   int    posCount  = CountSymbolPositions();

   // ── Take profit ──────────────────────────────────────────────────────────
   double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);
   if (posCount > 0 && accountProfit >= TakeProfit)
   {
      consecutiveLosses = 0;
      CloseAllPositions();
      ResetSetup();
      return;
   }

   // ── Exit check ───────────────────────────────────────────────────────────
   if (posCount > 0 && currentBar != lastExitBar)
   {
      lastExitBar = currentBar;
      int exitThreshold = MathMax(1, ExitCandlesBase - (entryCount - 1));

      bool outsideMA = false;
      if (selling) outsideMA = (prevOpen > ma50 && prevClose > ma50);
      if (buying)  outsideMA = (prevOpen < ma50 && prevClose < ma50);

      if (outsideMA)
         exitCandleCount++;
      else
         exitCandleCount = 0;

      if (exitCandleCount >= exitThreshold)
      {
         CloseAllPositions();
         ResetSetup();
         return;
      }
   }

   // ── Subsequent entry check ────────────────────────────────────────────────
   if (posCount > 0 && entryCount < MaxEntries && !waitingConfirm)
   {
      double newHi, newLo;
      datetime newZoneBar = iTime(_Symbol, _Period, 1 + RangeBars);
      if (newZoneBar > zoneBarTime && detectConsolidation(newHi, newLo))
      {
         // Check for breakout in same direction from new zone
         bool sameDirBreakout = (selling && prevClose < newLo) ||
                                (buying  && prevClose > newHi);
         if (sameDirBreakout)
         {
            zoneHigh       = newHi;
            zoneLow        = newLo;
            zoneBarTime    = newZoneBar;
            confirmCount   = 0;
            waitingConfirm = true;
            DrawZone(newHi, newLo);
         }
      }
   }

   // ── Confirmation counting ─────────────────────────────────────────────────
   if (waitingConfirm)
   {
      bool confirmValid = false;
      if (breakoutDir == -1) confirmValid = (prevOpen < ma50 && prevClose < ma50);
      if (breakoutDir ==  1) confirmValid = (prevOpen > ma50 && prevClose > ma50);

      if (confirmValid)
      {
         confirmCount++;
         if (confirmCount >= BreakoutConfirmBars)
         {
            // Enter trade
            if (breakoutDir == -1)
            {
               ExecSell();
               DrawEntry(false);
            }
            else
            {
               ExecBuy();
               DrawEntry(true);
            }
            waitingConfirm = false;
            confirmCount   = 0;
         }
      }
      else
      {
         // Confirmation candle failed — reset setup
         confirmCount   = 0;
         waitingConfirm = false;
         if (posCount == 0) ResetSetup();
      }
      return;
   }

   // ── New setup detection (no active trade) ─────────────────────────────────
   if (posCount == 0 && !waitingConfirm)
   {
      double newHi, newLo;
      datetime newZoneBar = iTime(_Symbol, _Period, 1 + RangeBars);

      if (newZoneBar != zoneBarTime && detectConsolidation(newHi, newLo))
      {
         zoneHigh    = newHi;
         zoneLow     = newLo;
         zoneBarTime = newZoneBar;
         DrawZone(newHi, newLo);
      }

      // Check for breakout
      if (zoneHigh > 0 && zoneLow > 0)
      {
         if (prevClose < zoneLow)
         {
            breakoutDir    = -1;
            waitingConfirm = true;
            confirmCount   = 0;
         }
         else if (prevClose > zoneHigh)
         {
            breakoutDir    = 1;
            waitingConfirm = true;
            confirmCount   = 0;
         }
      }
   }
}

int OnInit()
{
   return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
   ObjectDelete(0, "AlphaZoneHigh");
   ObjectDelete(0, "AlphaZoneLow");
}
