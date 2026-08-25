#include <Trade\Trade.mqh>

MqlTradeRequest request;
MqlTradeResult  result;

int    magicNumber = 109814;
static bool     buying             = false;
static bool     selling            = false;
static int      tradeCount         = 0;
static double   peakProfit         = 0.0;
static double   entryPrice         = 0.0;
static bool     trailingActive     = false;
static double   entryZoneCeiling   = 0.0; // sell: 2 candles closing above this = exit
static double   entryZoneFloor     = 0.0; // buy: 2 candles closing below this = exit
static int      reversalCount           = 0;
static int      reversalThreshold       = 2;
static datetime lastReversalBar         = 0;
static datetime lastCloseBarTime        = 0;
static double   profitAtLastEntry      = 0.0; // accountProfit when the last entry fired
static double   peakGainSinceLastEntry = 0.0; // peak gain since that entry — next entry needs abs(stopLoss)
static double   previousDXY           = 0.0;
static double   previousXAU           = 0.0;
static datetime lastTradeDate         = 0;


input double Money_FixLot_Lots                  = 0.01;
input double stopLoss                           = -20.0;
input double takeProfit                         = 70.0;
input int    maximumNumOfFailedTrades           = 10;
input int    RangeBars                          = 10;
input int    MinimumConsolidationBars           = 5;
input double MaxLowRangePoints                  = 400;
input double MaxHighConsolidationPoints         = 400;
input int    SwingStrength                      = 3;
input int    MaxEntries                         = 5;
input int    RangeBarsSubsequent                = 6;
input int    MinimumConsolidationBarsSubsequent = 3;
input double DXYDailyChangeThreshold           = 0.05;
input double RangeSwingTolerancePoints         = 300;
input double TrailingSpeedFactor               = 1.0;
input int    SwingLookback                     = 50;   // bars to scan for macro swing high/low before consolidation
input double MinimumDropPoints                 = 300;  // min drop/rally (points) from macro swing to zone

// ── Helpers ──────────────────────────────────────────────────────────────────

int CountSymbolPositions()
{
   int count = 0;
   for (int i = 0; i < PositionsTotal(); i++)
      if (PositionGetSymbol(i) == _Symbol) count++;
   return count;
}

double getEMAValue(int period)
{
   double arr[];
   int handle = iMA(_Symbol, _Period, period, 0, MODE_EMA, PRICE_CLOSE);
   if (handle == INVALID_HANDLE) return 0.0;
   ArraySetAsSeries(arr, true);
   if (CopyBuffer(handle, 0, 0, 1, arr) < 1) return 0.0;
   return arr[0];
}


double getVWAP()
{
   datetime dayStart = StringToTime(TimeToString(TimeCurrent(), TIME_DATE));
   double sumTPV = 0.0, sumVol = 0.0;
   for (int i = 0; i < 1440; i++)
   {
      if (iTime(_Symbol, PERIOD_M1, i) < dayStart) break;
      double tp  = (iHigh(_Symbol, PERIOD_M1, i) + iLow(_Symbol, PERIOD_M1, i) + iClose(_Symbol, PERIOD_M1, i)) / 3.0;
      double vol = (double)iVolume(_Symbol, PERIOD_M1, i);
      sumTPV += tp * vol;
      sumVol += vol;
   }
   return (sumVol > 0.0) ? sumTPV / sumVol : 0.0;
}

// Most recent confirmed swing high (used for sell: consolidation ceiling must be below this)
double getLastSwingHigh()
{
   for (int i = SwingStrength + 1; i <= 200 - SwingStrength; i++)
   {
      double hi = iHigh(_Symbol, PERIOD_CURRENT, i);
      bool isPivot = true;
      for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
      {
         if (j == i) continue;
         if (iHigh(_Symbol, PERIOD_CURRENT, j) >= hi) { isPivot = false; break; }
      }
      if (isPivot) return hi;
   }
   return -1;
}

// Most recent confirmed swing low (used for buy: consolidation floor must be above this)
double getLastSwingLow()
{
   for (int i = SwingStrength + 1; i <= 200 - SwingStrength; i++)
   {
      double lo = iLow(_Symbol, PERIOD_CURRENT, i);
      bool isPivot = true;
      for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
      {
         if (j == i) continue;
         if (iLow(_Symbol, PERIOD_CURRENT, j) <= lo) { isPivot = false; break; }
      }
      if (isPivot) return lo;
   }
   return -1;
}

// ── Synthetic DXY ────────────────────────────────────────────────────────────

double getSyntheticDXYAtBar(int barShift)
{
   return 50.14348112
      * MathPow(iClose("EURUSD", PERIOD_M1, barShift), -0.576)
      * MathPow(iClose("USDJPY", PERIOD_M1, barShift),  0.136)
      * MathPow(iClose("GBPUSD", PERIOD_M1, barShift), -0.119)
      * MathPow(iClose("USDCAD", PERIOD_M1, barShift),  0.091)
      * MathPow(iClose("USDSEK", PERIOD_M1, barShift),  0.042)
      * MathPow(iClose("USDCHF", PERIOD_M1, barShift),  0.036);
}

// Returns XAUUSD daily change as a percentage vs yesterday's D1 close
double getXAUDailyChangePct()
{
   double currentXAU   = iClose(_Symbol, PERIOD_CURRENT, 0);
   double yesterdayXAU = iClose(_Symbol, PERIOD_D1, 1);
   if (yesterdayXAU == 0) return 0;
   return ((currentXAU - yesterdayXAU) / yesterdayXAU) * 100.0;
}

// Normalized momentum: (current - previous) / |previous|
// Positive = strengthening vs previous, Negative = weakening.
// Handles zero-previous and sign-flip safely.
double normalizedMomentum(double current, double previous)
{
   if (MathAbs(previous) < 0.0001)
      return (current > 0) ? 1.0 : (current < 0) ? -1.0 : 0.0;
   return (current - previous) / MathAbs(previous);
}

// Returns DXY daily change as a percentage vs yesterday's D1 close
double getDXYDailyChangePct()
{
   double currentDXY   = getSyntheticDXYAtBar(0);
   double yesterdayDXY = 50.14348112
      * MathPow(iClose("EURUSD", PERIOD_D1, 1), -0.576)
      * MathPow(iClose("USDJPY", PERIOD_D1, 1),  0.136)
      * MathPow(iClose("GBPUSD", PERIOD_D1, 1), -0.119)
      * MathPow(iClose("USDCAD", PERIOD_D1, 1),  0.091)
      * MathPow(iClose("USDSEK", PERIOD_D1, 1),  0.042)
      * MathPow(iClose("USDCHF", PERIOD_D1, 1),  0.036);
   if (yesterdayDXY == 0) return 0;
   return ((currentDXY - yesterdayDXY) / yesterdayDXY) * 100.0;
}

// Returns DXY daily % change momentum.
// First trade of the day: compares current daily % vs 100-bar-ago daily %.
// Subsequent trades: compares current daily % vs value saved when last trade fired.
// Positive = DXY strengthening, negative = DXY weakening.
double getDXYTrend()
{
   double dxyPctNow = getDXYDailyChangePct();
   datetime today = StringToTime(TimeToString(TimeCurrent(), TIME_DATE));
   bool isFirstTradeOfDay = (lastTradeDate < today);

   if (isFirstTradeOfDay)
   {
      // Build daily % for the bar 100 bars ago
      double dxy100     = getSyntheticDXYAtBar(100);
      double dxyYest    = 50.14348112
         * MathPow(iClose("EURUSD", PERIOD_D1, 1), -0.576)
         * MathPow(iClose("USDJPY", PERIOD_D1, 1),  0.136)
         * MathPow(iClose("GBPUSD", PERIOD_D1, 1), -0.119)
         * MathPow(iClose("USDCAD", PERIOD_D1, 1),  0.091)
         * MathPow(iClose("USDSEK", PERIOD_D1, 1),  0.042)
         * MathPow(iClose("USDCHF", PERIOD_D1, 1),  0.036);
      double dxyPct100 = (dxyYest > 0) ? ((dxy100 - dxyYest) / dxyYest) * 100.0 : 0;
      return dxyPctNow - dxyPct100;
   }
   else
      return (previousDXY != 0) ? dxyPctNow - previousDXY : dxyPctNow;
}

// Compares XAUUSD and DXY % moves from the start of the consolidation to the current bar.
// Whichever moved more (by absolute value) is dominant and dictates the expected direction.
// Returns  1 = expect BUY (gold rising or DXY falling dominates)
//         -1 = expect SELL (gold falling or DXY rising dominates)
//          0 = indeterminate
int getDominantDirection(int rangeBars)
{
   int startBar = 1 + rangeBars;

   double xauStart = iClose(_Symbol, PERIOD_CURRENT, startBar);
   double xauNow   = iClose(_Symbol, PERIOD_CURRENT, 0);
   if (xauStart <= 0) return 0;
   double xauDelta = (xauNow - xauStart) / xauStart * 100.0;

   double dxyStart = getSyntheticDXYAtBar(startBar);
   double dxyNow   = getSyntheticDXYAtBar(0);
   if (dxyStart <= 0) return 0;
   double dxyDelta = (dxyNow - dxyStart) / dxyStart * 100.0;

   if (MathAbs(dxyDelta) > MathAbs(xauDelta))
      return (dxyDelta > 0) ? -1 : 1;  // DXY up → sell gold; DXY down → buy gold
   else
      return (xauDelta > 0) ?  1 : -1; // XAU up → buy; XAU down → sell
}

// Returns true when gold and DXY moved in the SAME direction during the consolidation window
// (gold leading — enter at consolidation touch).
// Returns false when OPPOSITE directions (DXY driving — wait for breakout).
bool isSameDirection(int rangeBars)
{
   int startBar = 1 + rangeBars;
   double xauStart = iClose(_Symbol, PERIOD_CURRENT, startBar);
   double xauNow   = iClose(_Symbol, PERIOD_CURRENT, 0);
   double dxyStart = getSyntheticDXYAtBar(startBar);
   double dxyNow   = getSyntheticDXYAtBar(0);
   if (xauStart <= 0 || dxyStart <= 0) return false;
   bool goldRising = xauNow > xauStart;
   bool dxyRising  = dxyNow > dxyStart;
   return goldRising == dxyRising;
}

// ── Market structure ─────────────────────────────────────────────────────────

int detectMarketStructure()
{
   double sh1 = -1, sh2 = -1;
   double sl1 = -1, sl2 = -1;

   for (int i = SwingStrength + 1; i <= 200 - SwingStrength; i++)
   {
      if (sh2 < 0)
      {
         double hi = iHigh(_Symbol, PERIOD_CURRENT, i);
         bool isPivotHigh = true;
         for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
         {
            if (j == i) continue;
            if (iHigh(_Symbol, PERIOD_CURRENT, j) >= hi) { isPivotHigh = false; break; }
         }
         if (isPivotHigh) { if (sh1 < 0) sh1 = hi; else sh2 = hi; }
      }

      if (sl2 < 0)
      {
         double lo = iLow(_Symbol, PERIOD_CURRENT, i);
         bool isPivotLow = true;
         for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
         {
            if (j == i) continue;
            if (iLow(_Symbol, PERIOD_CURRENT, j) <= lo) { isPivotLow = false; break; }
         }
         if (isPivotLow) { if (sl1 < 0) sl1 = lo; else sl2 = lo; }
      }

      if (sh2 >= 0 && sl2 >= 0) break;
   }

   if (sh1 < 0 || sh2 < 0 || sl1 < 0 || sl2 < 0) return 0;

   if (sh1 > sh2 && sl1 > sl2) return  1; // HH + HL → uptrend
   if (sh1 < sh2 && sl1 < sl2) return -1; // LH + LL → downtrend
   return 0;
}

// Returns true when the 2 most recent swing highs AND 2 swing lows are within
// RangeSwingTolerancePoints of each other — price is going sideways, not trending.
bool isRangingMarket()
{
   double sh1 = -1, sh2 = -1, sl1 = -1, sl2 = -1;

   for (int i = SwingStrength + 1; i <= 200 - SwingStrength; i++)
   {
      if (sh2 < 0)
      {
         double hi = iHigh(_Symbol, PERIOD_CURRENT, i);
         bool isPivotHigh = true;
         for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
         {
            if (j == i) continue;
            if (iHigh(_Symbol, PERIOD_CURRENT, j) >= hi) { isPivotHigh = false; break; }
         }
         if (isPivotHigh) { if (sh1 < 0) sh1 = hi; else sh2 = hi; }
      }

      if (sl2 < 0)
      {
         double lo = iLow(_Symbol, PERIOD_CURRENT, i);
         bool isPivotLow = true;
         for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
         {
            if (j == i) continue;
            if (iLow(_Symbol, PERIOD_CURRENT, j) <= lo) { isPivotLow = false; break; }
         }
         if (isPivotLow) { if (sl1 < 0) sl1 = lo; else sl2 = lo; }
      }

      if (sh2 >= 0 && sl2 >= 0) break;
   }

   if (sh1 < 0 || sh2 < 0 || sl1 < 0 || sl2 < 0) return false;

   double tol = RangeSwingTolerancePoints * _Point;
   return (MathAbs(sh1 - sh2) <= tol && MathAbs(sl1 - sl2) <= tol);
}

// ── Consolidation ─────────────────────────────────────────────────────────────

// Returns the consolidation floor (lowest low) if confirmed, or -1.
// Floor must be above the most recent swing low (HL) to confirm a genuine higher low.
double getConsolidationLow(int startShift, int rangeBars, int minConBars)
{
   int    lowestLowIndex = iLowest(_Symbol, PERIOD_CURRENT, MODE_LOW, rangeBars, startShift);
   double lowestLow      = iLow(_Symbol, PERIOD_CURRENT, lowestLowIndex);

   // Macro swing high must exist within SwingLookback bars above the zone
   int    swingHighIndex = iHighest(_Symbol, PERIOD_CURRENT, MODE_HIGH, SwingLookback, startShift);
   double swingHigh      = iHigh(_Symbol, PERIOD_CURRENT, swingHighIndex);

   // Drop from swing high to zone floor must be large enough
   if ((swingHigh - lowestLow) < MinimumDropPoints * _Point) return -1;

   // Swing high must be older than the zone floor (price peaked first, then dropped)
   if (swingHighIndex <= lowestLowIndex) return -1;

   double zoneTop = lowestLow + MaxLowRangePoints * _Point;
   int    count   = 0;
   for (int i = startShift; i < startShift + rangeBars; i++)
   {
      double lo = iLow(_Symbol, PERIOD_CURRENT, i);
      if (lo >= lowestLow && lo <= zoneTop) count++;
   }

   return (count >= minConBars) ? lowestLow : -1;
}

// Returns true when completed candles are consolidating at a low AND
// the live candle's wick or open touches the zone.
bool isLowConsolidating(int rangeBars, int minConBars)
{
   double baseLow = getConsolidationLow(1, rangeBars, minConBars);
   if (baseLow < 0) return false;

   double rangeTop    = baseLow + MaxLowRangePoints * _Point;
   double currentLow  = iLow(_Symbol,  PERIOD_CURRENT, 0);
   double currentOpen = iOpen(_Symbol, PERIOD_CURRENT, 0);
   double tolerance   = MaxLowRangePoints * _Point * 0.2;

   bool wickInRange = (currentLow  >= baseLow - tolerance && currentLow  <= rangeTop);
   bool openInRange = (currentOpen >= baseLow              && currentOpen <= rangeTop);

   return wickInRange || openInRange;
}

// Returns the consolidation ceiling (highest high) if confirmed, or -1.
// Ceiling must be below the most recent swing high (LH) to confirm a genuine lower high.
double getConsolidationHigh(int startShift, int rangeBars, int minConBars)
{
   int    highestHighIndex = iHighest(_Symbol, PERIOD_CURRENT, MODE_HIGH, rangeBars, startShift);
   double highestHigh      = iHigh(_Symbol, PERIOD_CURRENT, highestHighIndex);

   // Macro swing low must exist within SwingLookback bars below the zone
   int    swingLowIndex = iLowest(_Symbol, PERIOD_CURRENT, MODE_LOW, SwingLookback, startShift);
   double swingLow      = iLow(_Symbol, PERIOD_CURRENT, swingLowIndex);

   // Rally from swing low to zone ceiling must be large enough
   if ((highestHigh - swingLow) < MinimumDropPoints * _Point) return -1;

   // Swing low must be older than the zone ceiling (price bottomed first, then rallied)
   if (swingLowIndex <= highestHighIndex) return -1;

   double zoneBottom = highestHigh - MaxHighConsolidationPoints * _Point;
   int    count      = 0;
   for (int i = startShift; i < startShift + rangeBars; i++)
   {
      double hi = iHigh(_Symbol, PERIOD_CURRENT, i);
      if (hi >= zoneBottom && hi <= highestHigh) count++;
   }

   return (count >= minConBars) ? highestHigh : -1;
}

// Returns true when completed candles are consolidating at a high AND
// the live candle's wick or open touches the zone.
bool isHighConsolidatingEntry(int rangeBars, int minConBars)
{
   double baseHigh = getConsolidationHigh(1, rangeBars, minConBars);
   if (baseHigh < 0) return false;

   double zoneBottom  = baseHigh - MaxHighConsolidationPoints * _Point;
   double currentHigh = iHigh(_Symbol,  PERIOD_CURRENT, 0);
   double currentOpen = iOpen(_Symbol,  PERIOD_CURRENT, 0);
   double tolerance   = MaxHighConsolidationPoints * _Point * 0.2;

   bool wickInRange = (currentHigh <= baseHigh + tolerance && currentHigh >= zoneBottom);
   bool openInRange = (currentOpen >= zoneBottom && currentOpen <= baseHigh);

   return wickInRange || openInRange;
}

// Returns true when price has broken above a completed consolidation zone (buy scale-in)
bool isBuyBreakout(int rangeBars, int minConBars)
{
   double baseLow = getConsolidationLow(1, rangeBars, minConBars);
   if (baseLow < 0) return false;

   double zoneTop      = baseLow + MaxLowRangePoints * _Point;
   double currentClose = iClose(_Symbol, PERIOD_CURRENT, 0);

   return (currentClose > zoneTop);
}

// Returns true when price has broken below a completed consolidation zone (sell scale-in)
bool isSellBreakout(int rangeBars, int minConBars)
{
   double baseHigh = getConsolidationHigh(1, rangeBars, minConBars);
   if (baseHigh < 0) return false;

   double zoneBottom   = baseHigh - MaxHighConsolidationPoints * _Point;
   double currentClose = iClose(_Symbol, PERIOD_CURRENT, 0);

   return (currentClose < zoneBottom);
}

// ── Drawing ───────────────────────────────────────────────────────────────────


void DrawReversalStopLine(double price)
{
   ObjectDelete(0, "ReversalStopLine");
   ObjectCreate(0, "ReversalStopLine", OBJ_HLINE, 0, 0, price);
   ObjectSetInteger(0, "ReversalStopLine", OBJPROP_COLOR, clrOrangeRed);
   ObjectSetInteger(0, "ReversalStopLine", OBJPROP_WIDTH, 1);
   ObjectSetInteger(0, "ReversalStopLine", OBJPROP_STYLE, STYLE_DASH);
}

void DrawTrailingStopLine(double price)
{
   string name = "TrailingStopLine";
   if (ObjectFind(0, name) < 0)
   {
      ObjectCreate(0, name, OBJ_HLINE, 0, 0, price);
      ObjectSetInteger(0, name, OBJPROP_COLOR, clrLightBlue);
      ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);
      ObjectSetInteger(0, name, OBJPROP_STYLE, STYLE_DASH);
   }
   else
      ObjectSetDouble(0, name, OBJPROP_PRICE, price);
}

static datetime lastBlockedBarTime = 0;
static datetime lastEntryBarTime   = 0;

color GetBlockedColor(bool isBuy, string reason)
{
   if (isBuy)
   {
      if (reason == "XAUUSD not rising")   return clrCornflowerBlue;
      if (reason == "DXY not weak")        return clrRoyalBlue;
      if (reason == "no consolidation")    return clrSteelBlue;
      if (reason == "floor below EMA/VWAP") return clrCadetBlue;
      if (reason == "max entries reached") return clrDeepSkyBlue;
      return clrDodgerBlue;
   }
   else
   {
      if (reason == "XAUUSD not falling")    return clrOrange;
      if (reason == "DXY not strong")        return clrDarkOrange;
      if (reason == "no consolidation")      return clrSandyBrown;
      if (reason == "ceiling above EMA/VWAP") return clrChocolate;
      if (reason == "max entries reached")   return clrPeru;
      return clrOrangeRed;
   }
}

void DrawBlockedLabel(bool isBuy, string reason)
{
   // Only draw once per bar to avoid flooding the chart
   datetime currentBar = iTime(_Symbol, PERIOD_CURRENT, 0);
   if (currentBar == lastBlockedBarTime) return;
   lastBlockedBarTime = currentBar;

   string name = "Blocked_" + IntegerToString(TimeCurrent());
   double price = isBuy
      ? iLow(_Symbol,  PERIOD_CURRENT, 0) - 120 * _Point
      : iHigh(_Symbol, PERIOD_CURRENT, 0) + 120 * _Point;

   ObjectCreate(0, name, OBJ_TEXT, 0, TimeCurrent(), price);
   ObjectSetString(0,  name, OBJPROP_TEXT,     (isBuy ? "⊘B: " : "⊘S: ") + reason);
   ObjectSetInteger(0, name, OBJPROP_COLOR,    GetBlockedColor(isBuy, reason));
   ObjectSetInteger(0, name, OBJPROP_FONTSIZE, 7);
   ObjectSetString(0,  name, OBJPROP_FONT,     "Arial");
   ObjectSetInteger(0, name, OBJPROP_ANCHOR,   isBuy ? ANCHOR_TOP : ANCHOR_BOTTOM);
}

void DrawRangeLabel()
{
   datetime currentBar = iTime(_Symbol, PERIOD_CURRENT, 0);
   if (currentBar == lastBlockedBarTime) return;
   lastBlockedBarTime = currentBar;

   string name  = "Range_" + IntegerToString(TimeCurrent());
   double price = (iHigh(_Symbol, PERIOD_CURRENT, 0) + iLow(_Symbol, PERIOD_CURRENT, 0)) / 2.0;
   ObjectCreate(0, name, OBJ_TEXT, 0, TimeCurrent(), price);
   ObjectSetString(0,  name, OBJPROP_TEXT,     "⇔ Range");
   ObjectSetInteger(0, name, OBJPROP_COLOR,    clrGold);
   ObjectSetInteger(0, name, OBJPROP_FONTSIZE, 7);
   ObjectSetString(0,  name, OBJPROP_FONT,     "Arial");
   ObjectSetInteger(0, name, OBJPROP_ANCHOR,   ANCHOR_CENTER);
}

void DrawBuySignal()
{
   double dxyNow    = getDXYDailyChangePct();
   double xauNow    = getXAUDailyChangePct();
   // Strong: gold ↑ + DXY ↓ (inverse relationship) → lime green
   // Weak:   gold ↑ + DXY ↑ (gold leading)         → yellow
   bool   strongBuy = (dxyNow < previousDXY);
   color  arrowClr  = strongBuy ? clrLime   : clrYellow;
   color  dxyClr    = strongBuy ? clrLime   : clrYellow;
   color  xauClr    = strongBuy ? clrAqua   : clrYellow;

   string name = "BuySignal_" + IntegerToString(TimeCurrent());
   double price = iLow(_Symbol, PERIOD_CURRENT, 0) - 50 * _Point;
   ObjectCreate(0, name, OBJ_ARROW_UP, 0, TimeCurrent(), price);
   ObjectSetInteger(0, name, OBJPROP_COLOR,     arrowClr);
   ObjectSetInteger(0, name, OBJPROP_WIDTH,     2);
   ObjectSetInteger(0, name, OBJPROP_ARROWCODE, 241);

   string lbl = "BuyLbl_" + IntegerToString(TimeCurrent());
   string lblText = "XAU " + DoubleToString(xauNow, 2) + " (" + DoubleToString(previousXAU, 2) + ")"
                  + "  DXY " + DoubleToString(dxyNow, 2) + " (" + DoubleToString(previousDXY, 2) + ")";
   ObjectCreate(0, lbl, OBJ_TEXT, 0, TimeCurrent(), price - 100 * _Point);
   ObjectSetString(0,  lbl, OBJPROP_TEXT,     lblText);
   ObjectSetInteger(0, lbl, OBJPROP_COLOR,    arrowClr);
   ObjectSetInteger(0, lbl, OBJPROP_FONTSIZE, 8);
   ObjectSetString(0,  lbl, OBJPROP_FONT,     "Arial");
   ObjectSetInteger(0, lbl, OBJPROP_ANCHOR,   ANCHOR_TOP);
}

void DrawSellSignal()
{
   double dxyNow    = getDXYDailyChangePct();
   double xauNow    = getXAUDailyChangePct();
   // Strong: gold ↓ + DXY ↑ (inverse relationship) → red
   // Weak:   gold ↓ + DXY ↓ (gold leading)         → orange
   bool   strongSell = (dxyNow > previousDXY);
   color  arrowClr   = strongSell ? clrRed    : clrOrange;
   color  dxyClr     = strongSell ? clrRed    : clrOrange;
   color  xauClr     = strongSell ? clrTomato : clrOrange;

   string name = "SellSignal_" + IntegerToString(TimeCurrent());
   double price = iHigh(_Symbol, PERIOD_CURRENT, 0) + 50 * _Point;
   ObjectCreate(0, name, OBJ_ARROW_DOWN, 0, TimeCurrent(), price);
   ObjectSetInteger(0, name, OBJPROP_COLOR,     arrowClr);
   ObjectSetInteger(0, name, OBJPROP_WIDTH,     2);
   ObjectSetInteger(0, name, OBJPROP_ARROWCODE, 242);

   string lbl = "SellLbl_" + IntegerToString(TimeCurrent());
   string lblText = "XAU " + DoubleToString(xauNow, 2) + " (" + DoubleToString(previousXAU, 2) + ")"
                  + "  DXY " + DoubleToString(dxyNow, 2) + " (" + DoubleToString(previousDXY, 2) + ")";
   ObjectCreate(0, lbl, OBJ_TEXT, 0, TimeCurrent(), price + 100 * _Point);
   ObjectSetString(0,  lbl, OBJPROP_TEXT,     lblText);
   ObjectSetInteger(0, lbl, OBJPROP_COLOR,    arrowClr);
   ObjectSetInteger(0, lbl, OBJPROP_FONTSIZE, 8);
   ObjectSetString(0,  lbl, OBJPROP_FONT,     "Arial");
   ObjectSetInteger(0, lbl, OBJPROP_ANCHOR,   ANCHOR_BOTTOM);
}

// ── Order execution ───────────────────────────────────────────────────────────

void Buy(bool isSubsequent = false, double zoneFloor = 0.0)
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

   buying             = true;
   entryPrice         = (isSubsequent ? entryPrice : ask);
   lastEntryBarTime   = iTime(_Symbol, PERIOD_CURRENT, 0);
   reversalCount      = 0;

   if (!isSubsequent)
   {
      entryZoneFloor           = zoneFloor;
      reversalThreshold        = 2;
      profitAtLastEntry        = 0.0;
      peakGainSinceLastEntry   = 0.0;
   }
   else
   {
      if (zoneFloor > 0) entryZoneFloor = zoneFloor;
      reversalThreshold        = 3;
      profitAtLastEntry        = AccountInfoDouble(ACCOUNT_PROFIT);
      peakGainSinceLastEntry   = 0.0;
      DrawReversalStopLine(entryZoneFloor);
   }
}

void Sell(bool isSubsequent = false, double zoneCeiling = 0.0)
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

   selling            = true;
   entryPrice         = (isSubsequent ? entryPrice : bid);
   lastEntryBarTime   = iTime(_Symbol, PERIOD_CURRENT, 0);
   reversalCount      = 0;

   if (!isSubsequent)
   {
      entryZoneCeiling         = zoneCeiling;
      reversalThreshold        = 2;
      profitAtLastEntry        = 0.0;
      peakGainSinceLastEntry   = 0.0;
   }
   else
   {
      if (zoneCeiling > 0) entryZoneCeiling = zoneCeiling;
      reversalThreshold        = 3;
      profitAtLastEntry        = AccountInfoDouble(ACCOUNT_PROFIT);
      peakGainSinceLastEntry   = 0.0;
      DrawReversalStopLine(entryZoneCeiling);
   }
}

// ── Close ─────────────────────────────────────────────────────────────────────

void CloseAllOrders()
{
   CTrade trade;
   for (int i = PositionsTotal() - 1; i >= 0; i--)
   {
      if (PositionGetSymbol(i) == _Symbol)
         trade.PositionClose(PositionGetInteger(POSITION_TICKET));
   }
}

void CloseAll()
{
   CloseAllOrders();
   buying             = false;
   selling            = false;
   peakProfit         = 0.0;
   entryPrice         = 0.0;
   trailingActive     = false;
   entryZoneCeiling   = 0.0;
   entryZoneFloor     = 0.0;
   reversalCount            = 0;
   reversalThreshold        = 2;
   profitAtLastEntry        = 0.0;
   peakGainSinceLastEntry   = 0.0;
   lastCloseBarTime         = iTime(_Symbol, PERIOD_CURRENT, 0);
   ObjectDelete(0, "TrailingStopLine");
   ObjectDelete(0, "ReversalStopLine");
}

// ── Entry logic ───────────────────────────────────────────────────────────────

bool shouldContinueTrading()
{
   return tradeCount >= maximumNumOfFailedTrades;
}

void trade()
{
   int      posCount             = CountSymbolPositions();
   datetime currentBar           = iTime(_Symbol, PERIOD_CURRENT, 0);
   bool     newBarSinceLastEntry = (currentBar != lastEntryBarTime);

   if (!newBarSinceLastEntry) return;



   // ── 200-bar range context ─────────────────────────────────────────────────
   double range200High = iHigh(_Symbol, PERIOD_CURRENT, iHighest(_Symbol, PERIOD_CURRENT, MODE_HIGH, 200, 0));
   double range200Low  = iLow(_Symbol,  PERIOD_CURRENT, iLowest(_Symbol,  PERIOD_CURRENT, MODE_LOW,  200, 0));
   double range200Mid  = (range200High + range200Low) / 2.0;
   double close0       = iClose(_Symbol, PERIOD_CURRENT, 0);
   bool   inLowerHalf  = close0 < range200Mid;
   bool   inUpperHalf  = close0 > range200Mid;

   // ── BUY ──────────────────────────────────────────────────────────────────
   if (!selling)
   {
      if (posCount >= MaxEntries) { /* max entries reached — wait */ }
      else if (posCount == 0)
      {
         datetime zoneStart  = iTime(_Symbol, PERIOD_CURRENT, 1 + RangeBars);
         double floor      = getConsolidationLow(1, RangeBars, MinimumConsolidationBars);
         double xauPctNow  = getXAUDailyChangePct();
         double dxyNow     = getDXYDailyChangePct();
         datetime today    = StringToTime(TimeToString(TimeCurrent(), TIME_DATE));

         if (lastCloseBarTime > 0 && zoneStart <= lastCloseBarTime) { /* zone too old */ }
         else if (!inLowerHalf) { /* not in lower half */ }
         else if (floor < 0)    { /* no consolidation */ }
         else if (normalizedMomentum(xauPctNow, previousXAU) <= normalizedMomentum(dxyNow, previousDXY)) { /* gold not outperforming DXY */ }
         else if (!isLowConsolidating(RangeBars, MinimumConsolidationBars)) { /* not touching zone */ }
         else
         {
            DrawBuySignal();
            previousDXY   = dxyNow;
            previousXAU   = xauPctNow;
            lastTradeDate = today;
            Buy(false, floor);
         }
      }
      else
      {
         double subFloor  = getConsolidationLow(1, RangeBarsSubsequent, MinimumConsolidationBarsSubsequent);
         double xauPctNow = getXAUDailyChangePct();
         double dxyNow    = getDXYDailyChangePct();

         if (peakGainSinceLastEntry < MathAbs(stopLoss)) { /* gain not yet $20 at this bar */ }
         else if (subFloor < 0) { /* no consolidation yet */ }
         else if (!isLowConsolidating(RangeBarsSubsequent, MinimumConsolidationBarsSubsequent)) { /* not touching zone */ }
         else if (normalizedMomentum(xauPctNow, previousXAU) <= normalizedMomentum(dxyNow, previousDXY)) { /* gold not outperforming DXY */ }
         else
         {
            DrawBuySignal();
            previousDXY = dxyNow;
            previousXAU = xauPctNow;
            Buy(true, subFloor);
         }
      }
   }

   // ── SELL ─────────────────────────────────────────────────────────────────
   if (!buying)
   {
      if (posCount >= MaxEntries) { /* max entries reached — wait */ }
      else if (posCount == 0)
      {
         datetime zoneStart = iTime(_Symbol, PERIOD_CURRENT, 1 + RangeBars);
         double   ceiling   = getConsolidationHigh(1, RangeBars, MinimumConsolidationBars);
         double   xauPctNow = getXAUDailyChangePct();
         double   dxyNow    = getDXYDailyChangePct();
         datetime today     = StringToTime(TimeToString(TimeCurrent(), TIME_DATE));

         if (lastCloseBarTime > 0 && zoneStart <= lastCloseBarTime) { /* zone too old */ }
         else if (!inUpperHalf)  { /* not in upper half */ }
         else if (ceiling < 0)   { /* no consolidation */ }
         else if (normalizedMomentum(dxyNow, previousDXY) <= normalizedMomentum(xauPctNow, previousXAU)) { /* DXY not outperforming gold */ }
         else if (!isHighConsolidatingEntry(RangeBars, MinimumConsolidationBars)) { /* not touching zone */ }
         else
         {
            DrawSellSignal();
            previousDXY   = dxyNow;
            previousXAU   = xauPctNow;
            lastTradeDate = today;
            Sell(false, ceiling);
         }
      }
      else
      {
         double subCeiling = getConsolidationHigh(1, RangeBarsSubsequent, MinimumConsolidationBarsSubsequent);
         double xauPctNow  = getXAUDailyChangePct();
         double dxyNow     = getDXYDailyChangePct();

         if (peakGainSinceLastEntry < MathAbs(stopLoss)) { /* gain not yet $20 at this bar */ }
         else if (subCeiling < 0) { /* no consolidation yet */ }
         else if (!isHighConsolidatingEntry(RangeBarsSubsequent, MinimumConsolidationBarsSubsequent)) { /* not touching zone */ }
         else if (normalizedMomentum(dxyNow, previousDXY) <= normalizedMomentum(xauPctNow, previousXAU)) { /* DXY not outperforming gold */ }
         else
         {
            DrawSellSignal();
            previousDXY = dxyNow;
            previousXAU = xauPctNow;
            Sell(true, subCeiling);
         }
      }
   }
}

// ── OnTick ────────────────────────────────────────────────────────────────────

void OnTick()
{
   if (shouldContinueTrading()) return;

   // ── Reversal candle check — only active before trailing kicks in ─────────
   if (CountSymbolPositions() > 1 && !trailingActive)
   {
      datetime lastBar = iTime(_Symbol, PERIOD_CURRENT, 1);
      if (lastBar != lastReversalBar)
      {
         lastReversalBar = lastBar;
         double lastClose = iClose(_Symbol, PERIOD_CURRENT, 1);
         double lastOpen  = iOpen(_Symbol,  PERIOD_CURRENT, 1);
         bool   bullish   = (lastClose > lastOpen);
         bool   bearish   = (lastClose < lastOpen);

         if (selling && bullish && lastClose > entryZoneCeiling)
            reversalCount++;
         else if (selling)
            reversalCount = 0;

         if (buying && bearish && lastClose < entryZoneFloor)
            reversalCount++;
         else if (buying)
            reversalCount = 0;

         if (reversalCount >= reversalThreshold)
         {
            tradeCount++;
            CloseAll();
            return;
         }
      }
   }

   trade();

   double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);
   double contractSize  = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);
   int    posCount      = CountSymbolPositions();

   if (accountProfit >= takeProfit)
   {
      tradeCount = 0;
      CloseAll();
      return;
   }

   // Hard stop only applies when there is exactly one position (first entry, no subsequent yet)
   if (posCount == 1 && accountProfit <= stopLoss)
   {
      tradeCount++;
      CloseAll();
      return;
   }

   if (posCount > 0)
   {
      if (accountProfit > peakProfit) peakProfit = accountProfit;

      double gainSinceLastEntry = accountProfit - profitAtLastEntry;
      if (gainSinceLastEntry > peakGainSinceLastEntry) peakGainSinceLastEntry = gainSinceLastEntry;

      if (peakProfit >= MathAbs(stopLoss))
      {
         trailingActive = true;

         // Trail exit: profit has pulled back by abs(stopLoss) * TrailingSpeedFactor from peak
         double trailDrawback = stopLoss * TrailingSpeedFactor;
         if (accountProfit <= peakProfit + trailDrawback)
         {
            tradeCount = 0;
            CloseAll();
            return;
         }

         // Draw trail line using weighted average entry price and total volume
         // so the line is accurate regardless of how many positions are open
         double totalVolume = 0, weightedPrice = 0;
         for (int i = 0; i < PositionsTotal(); i++)
         {
            if (PositionGetSymbol(i) == _Symbol)
            {
               double vol = PositionGetDouble(POSITION_VOLUME);
               totalVolume   += vol;
               weightedPrice += PositionGetDouble(POSITION_PRICE_OPEN) * vol;
            }
         }
         double avgEntry  = (totalVolume > 0) ? weightedPrice / totalVolume : entryPrice;
         double totalLots = (totalVolume > 0) ? totalVolume : Money_FixLot_Lots;
         double trailPrice = buying
            ? avgEntry + (peakProfit + trailDrawback) / (totalLots * contractSize)
            : avgEntry - (peakProfit + trailDrawback) / (totalLots * contractSize);
         DrawTrailingStopLine(trailPrice);
      }
      else
      {
         // Not yet trailing — draw fixed stop based on first entry only
         double fixedStop = buying
            ? entryPrice + stopLoss / (Money_FixLot_Lots * contractSize)
            : entryPrice - stopLoss / (Money_FixLot_Lots * contractSize);
         DrawTrailingStopLine(fixedStop);
      }
   }
}
