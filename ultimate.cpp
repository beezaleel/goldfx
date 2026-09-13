#include <Trade\Trade.mqh>

CTrade trade;

static int magicNumber = 309815;

// ── State ─────────────────────────────────────────────────────────────────────
static int      pendingDir        = 0;
static int      watchBars         = 0;
static bool     inTrade           = false;
static int      entryCount        = 0;
static double   peakProfit        = 0.0;
static double   trendingPeakProfit = 0.0;
static bool     trailingActive    = false;
static double   firstEntryPrice   = 0.0;
static bool     breakEvenDone     = false;
static double   zoneHigh          = -1;
static double   zoneLow           = -1;
static datetime lastProcessedBar  = 0;
static int      consecutiveLosses = 0;
static int      exitCooldownBars  = 0;  // bars to wait before exit monitor activates
static bool     isFlipTrade       = false;
static bool     isTrendingTrade   = false;

// ── DXY 7-minute momentum snapshot ───────────────────────────────────────────
static double   dxySnapshot       = 0;        // DXY price stored 7 mins ago
static datetime dxySnapshotTime   = 0;        // when the snapshot was taken
static int      DXY_INTERVAL_SECS = 7 * 60;  // 7 minutes in seconds
// ── Exit-signal state (opposing zone detected while in trade) ─────────────
static int      exitPendingDir    = 0;
static double   exitZoneHigh      = 0;
static double   exitZoneLow       = 0;
static int      exitWatchBars     = 0;

// Running consolidation state — updated bar by bar as price forms
static double   sellCeiling           = 0;
static double   sellCeilingAnchor     = 0;
static datetime sellCeilingAnchorTime = 0;
static int      sellHugCount          = 0;
static double   sellConsolidationLow  = DBL_MAX; // min low seen during sell consolidation
static double   lastConfirmedSellCeiling = 0;    // ceiling of the most recently confirmed SELL zone
static datetime lastConfirmedSellTime    = 0;
static double   lastConfirmedBuyFloor    = 0;    // floor of the most recently confirmed BUY zone
static datetime lastConfirmedBuyTime     = 0;
static double   buyFloor              = 0;
static double   buyFloorAnchor        = 0;
static datetime buyFloorAnchorTime    = 0;
static int      buyHugCount           = 0;
static double   buyConsolidationHigh  = -DBL_MAX; // max high seen during buy consolidation

// ── Inputs ────────────────────────────────────────────────────────────────────
input double Money_FixLot_Lots          = 0.05;
input int    MinConsolidationBars       = 7;    // minimum candles required
input double ConsolidationSpreadDollars = 4.0;  // max spread of highs (SELL) or lows (BUY) in $ (e.g. $2 on XAUUSD M1)
input double ZoneWidthDollars           = 10.0; // height of the armed zone in $ (e.g. $10 on XAUUSD)
input int    FastMAPeriod               = 20;   // fast MA period for trend direction
input int    SlowMAPeriod               = 100;  // slow MA period for trend direction
input double MinTrendSpreadDollars      = 1.6;  // minimum MA gap in $ to confirm a real trend
input int    EntryWindowBars            = 10;   // bars to wait for price to revisit trigger
input double StopLossDollars            = 30.0;
input double TakeProfitDollars          = 250.0;
input double TrailingActivateFactor     = 0.5;  // trailing activates when profit >= SL × this
input double TrailingSpeedFactor        = 0.7;  // closes when profit drops by SL × this from peak
input double TrendingTrailAdvanceFactor = 0.5;  // how fast SL advances from original SL for trending trades (0.5 = $0.50 per $1 profit)
input int    MaxEntriesPerZone          = 1;   // max trades allowed per confirmed zone
input int    MaxConsecutiveLosses       = 3;
input double PrevZoneTolerance          = 5.0;  // current zone must be within $X of a previous consolidation level
input int    PrevZoneLookback           = 100;  // bars to scan for previous consolidation
input bool   EnableConsolidationTrades  = true; // set true to re-enable consolidation trades alongside trending

// ── Helpers ───────────────────────────────────────────────────────────────────

int CountMagicPositions()
{
   int n = 0;
   for (int i = 0; i < PositionsTotal(); i++)
      if (PositionGetSymbol(i) == _Symbol && PositionGetInteger(POSITION_MAGIC) == magicNumber)
         n++;
   return n;
}

double DollarsToPriceDist(double dollars)
{
   double tickSize  = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);
   double tickValue = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_VALUE);
   if (tickValue == 0 || tickSize == 0) return 0;
   return dollars / (Money_FixLot_Lots * (tickValue / tickSize));
}

double CalcSMA(int period, int shift)
{
   double sum = 0;
   for (int i = shift; i < shift + period; i++)
      sum += iClose(_Symbol, _Period, i);
   return sum / period;
}




// Returns true if EMA 200 crossed price within the last `lookback` bars.
bool HasEMA200Cross(int lookback)
{
   int handle = iMA(_Symbol, _Period, 200, 0, MODE_EMA, PRICE_CLOSE);
   if (handle == INVALID_HANDLE) return true;
   double ema[]; double cls[];
   ArraySetAsSeries(ema, true); ArraySetAsSeries(cls, true);
   if (CopyBuffer(handle, 0, 1, lookback, ema) <= 0 ||
       CopyClose(_Symbol, _Period, 1, lookback, cls) <= 0)
   { IndicatorRelease(handle); return true; }
   IndicatorRelease(handle);
   for (int i = 0; i < lookback - 1; i++)
   {
      if ((cls[i] > ema[i]) != (cls[i+1] > ema[i+1])) return true;
   }
   return false;
}

// Scans bars [startBar .. startBar+lookback] for the first cluster of at least
// MinConsolidationBars consecutive bars whose highs (isSell) or lows (!isSell)
// stay within ConsolidationSpreadDollars. Returns the cluster ceiling/floor or 0.
double FindPreviousConsolidationLevel(bool isSell, int startBar, int lookback, datetime &outTime)
{
   double tolDist = DollarsToPriceDist(ConsolidationSpreadDollars);
   int    minBars = MinConsolidationBars;

   for (int i = startBar; i <= startBar + lookback - minBars; i++)
   {
      double maxH = -DBL_MAX, minH = DBL_MAX;
      double maxL = -DBL_MAX, minL = DBL_MAX;

      for (int j = i; j < i + minBars; j++)
      {
         double h = iHigh(_Symbol, _Period, j);
         double l = iLow (_Symbol, _Period, j);
         if (h > maxH) maxH = h;
         if (h < minH) minH = h;
         if (l > maxL) maxL = l;
         if (l < minL) minL = l;
      }

      if (isSell  && (maxH - minH) <= tolDist) { outTime = iTime(_Symbol, _Period, i); return maxH; }
      if (!isSell && (maxL - minL) <= tolDist) { outTime = iTime(_Symbol, _Period, i); return minL; }
   }
   outTime = 0;
   return 0;
}

// Called on every new bar. Updates the running consolidation trackers using
// bar 1 (the just-completed candle). Returns:
//  -1 if a SELL ceiling just reached MinConsolidationBars
//   1 if a BUY floor just reached MinConsolidationBars
//   0 if no zone is confirmed yet
int UpdateConsolidation(double &outCeiling, double &outFloor, double &outConsolidationLow, double &outConsolidationHigh)
{
   double tol      = ConsolidationSpreadDollars;
   double bar1High = iHigh(_Symbol, _Period, 1);
   double bar1Low  = iLow (_Symbol, _Period, 1);

   datetime bar1Time = iTime(_Symbol, _Period, 1);

   // ── SELL ceiling tracker ──────────────────────────────────────────────────
   if (sellCeiling == 0 || bar1High > sellCeiling + tol)
   {
      sellCeiling           = bar1High;
      sellCeilingAnchor     = bar1High;
      sellCeilingAnchorTime = bar1Time;
      sellHugCount          = 1;
      sellConsolidationLow  = bar1Low;
   }
   else if (bar1High >= sellCeiling - tol)
   {
      if (bar1High > sellCeiling) sellCeiling = bar1High;
      if (bar1Low < sellConsolidationLow) sellConsolidationLow = bar1Low;
      sellHugCount++;
   }
   else
   {
      sellCeiling           = 0;
      sellCeilingAnchor     = 0;
      sellCeilingAnchorTime = 0;
      sellHugCount          = 0;
      sellConsolidationLow  = DBL_MAX;
   }

   // ── BUY floor tracker ─────────────────────────────────────────────────────
   if (buyFloor == 0 || bar1Low < buyFloor - tol)
   {
      buyFloor             = bar1Low;
      buyFloorAnchor       = bar1Low;
      buyFloorAnchorTime   = bar1Time;
      buyHugCount          = 1;
      buyConsolidationHigh = bar1High;
   }
   else if (bar1Low <= buyFloor + tol)
   {
      if (bar1Low < buyFloor) buyFloor = bar1Low;
      if (bar1High > buyConsolidationHigh) buyConsolidationHigh = bar1High;
      buyHugCount++;
   }
   else
   {
      buyFloor             = 0;
      buyFloorAnchor       = 0;
      buyFloorAnchorTime   = 0;
      buyHugCount          = 0;
      buyConsolidationHigh = -DBL_MAX;
   }

   Print("Consolidation: sellCeiling=", sellCeiling, " sellCount=", sellHugCount,
         " | buyFloor=", buyFloor, " buyCount=", buyHugCount);

   // ── Check if a zone just crossed the threshold ────────────────────────────
   double fastMA = CalcSMA(FastMAPeriod, 1);
   double slowMA = CalcSMA(SlowMAPeriod, 1);

   double minSpread = MinTrendSpreadDollars;

   // SELL: uptrend (fastMA > slowMA) with sufficient spread, AND ceiling is ABOVE the fast MA
   // — price has risen to an elevated level, not just clustering in the middle of a flat range.
   if (sellHugCount >= MinConsolidationBars && fastMA > slowMA + minSpread && sellCeiling > fastMA
       && sellCeiling <= sellCeilingAnchor + tol)
   {
      // Require current ceiling to be near a previous consolidation ceiling
      // Use last confirmed SELL zone as previous level if within PrevZoneLookback bars, else scan candles
      double   prevLevel   = 0;
      datetime prevTime    = 0;
      double   prevTolDist = DollarsToPriceDist(PrevZoneTolerance);
      if (lastConfirmedSellCeiling > 0)
      {
         int barsAgo = iBarShift(_Symbol, _Period, lastConfirmedSellTime, false);
         if (barsAgo >= 0 && barsAgo <= PrevZoneLookback)
         {
            prevLevel = lastConfirmedSellCeiling;
            prevTime  = lastConfirmedSellTime;
         }
         else
         {
            lastConfirmedSellCeiling = 0;
            lastConfirmedSellTime    = 0;
         }
      }
      if (prevLevel == 0)
      {
         int startBar = sellHugCount + 1;
         prevLevel = FindPreviousConsolidationLevel(true, startBar, PrevZoneLookback, prevTime);
      }

      if (prevLevel > 0 && sellCeiling < prevLevel - prevTolDist)
      {
         Print("SELL reject (prev zone): ceiling=", sellCeiling, " prevLevel=", prevLevel,
               " gap=", DoubleToString((prevLevel - sellCeiling) / _Point * _Point * 100, 2), "$");
      }
      else
      {
         outCeiling          = sellCeiling;
         outFloor            = sellCeiling - DollarsToPriceDist(ZoneWidthDollars);
         outConsolidationLow = sellConsolidationLow;
         Print("SELL zone confirmed: hugCount=", sellHugCount, " ceiling=", sellCeiling,
               " prevLevel=", prevLevel,
               " anchor=", sellCeilingAnchor, " drift=", (sellCeiling - sellCeilingAnchor) / _Point,
               "pts fastMA=", fastMA, " slowMA=", slowMA, " spread=", (fastMA - slowMA) / _Point, "pts");
         DrawConsolidationMark(sellCeilingAnchorTime, sellCeilingAnchor, true, true);
         if (prevLevel > 0 && prevTime > 0)
            DrawPrevZoneLine(prevTime, prevLevel, sellCeilingAnchorTime, sellCeiling, true);
         lastConfirmedSellCeiling = sellCeiling;
         lastConfirmedSellTime    = sellCeilingAnchorTime;
         sellHugCount = 0; sellCeiling = 0; sellCeilingAnchor = 0; sellCeilingAnchorTime = 0; sellConsolidationLow = DBL_MAX;
         buyHugCount  = 0; buyFloor    = 0; buyFloorAnchor    = 0; buyFloorAnchorTime    = 0; buyConsolidationHigh = -DBL_MAX;
         return -1;
      }
   }
   else if (sellHugCount >= MinConsolidationBars)
      Print("SELL reject: ceiling=", sellCeiling, " anchor=", sellCeilingAnchor,
            " drift=", (sellCeiling - sellCeilingAnchor) / _Point, "pts fastMA=", fastMA,
            " slowMA=", slowMA, " spread=", (fastMA - slowMA) / _Point, "pts (need ceiling>fastMA>slowMA+minSpread, drift<=", ConsolidationSpreadDollars, ")");

   // BUY: downtrend (fastMA < slowMA) with sufficient spread, AND floor is BELOW the fast MA
   // — price has dropped to a depressed level, not just flat mid-range.
   if (buyHugCount >= MinConsolidationBars && fastMA < slowMA - minSpread && buyFloor < fastMA
       && buyFloor >= buyFloorAnchor - tol)
   {
      // Require current floor to be near a previous consolidation floor
      // Use last confirmed BUY zone as previous level if within PrevZoneLookback bars, else scan candles
      double   prevLevel   = 0;
      datetime prevTime    = 0;
      double   prevTolDist = DollarsToPriceDist(PrevZoneTolerance);
      if (lastConfirmedBuyFloor > 0)
      {
         int barsAgo = iBarShift(_Symbol, _Period, lastConfirmedBuyTime, false);
         if (barsAgo >= 0 && barsAgo <= PrevZoneLookback)
         {
            prevLevel = lastConfirmedBuyFloor;
            prevTime  = lastConfirmedBuyTime;
         }
         else
         {
            lastConfirmedBuyFloor = 0;
            lastConfirmedBuyTime  = 0;
         }
      }
      if (prevLevel == 0)
      {
         int startBar = buyHugCount + 1;
         prevLevel = FindPreviousConsolidationLevel(false, startBar, PrevZoneLookback, prevTime);
      }

      if (prevLevel > 0 && buyFloor > prevLevel + prevTolDist)
      {
         Print("BUY reject (prev zone): floor=", buyFloor, " prevLevel=", prevLevel,
               " gap=", DoubleToString((buyFloor - prevLevel) / _Point * _Point * 100, 2), "$");
      }
      else
      {
         outCeiling           = buyFloor + DollarsToPriceDist(ZoneWidthDollars);
         outFloor             = buyFloor;
         outConsolidationHigh = buyConsolidationHigh;
         Print("BUY zone confirmed: hugCount=", buyHugCount, " floor=", buyFloor,
               " prevLevel=", prevLevel,
               " anchor=", buyFloorAnchor, " drift=", (buyFloorAnchor - buyFloor) / _Point,
               "pts fastMA=", fastMA, " slowMA=", slowMA, " spread=", (slowMA - fastMA) / _Point, "pts");
         DrawConsolidationMark(buyFloorAnchorTime, buyFloorAnchor, true, false);
         if (prevLevel > 0 && prevTime > 0)
            DrawPrevZoneLine(prevTime, prevLevel, buyFloorAnchorTime, buyFloor, false);
         lastConfirmedBuyFloor = buyFloor;
         lastConfirmedBuyTime  = buyFloorAnchorTime;
         buyHugCount  = 0; buyFloor    = 0; buyFloorAnchor    = 0; buyFloorAnchorTime    = 0; buyConsolidationHigh = -DBL_MAX;
         sellHugCount = 0; sellCeiling = 0; sellCeilingAnchor = 0; sellCeilingAnchorTime = 0; sellConsolidationLow = DBL_MAX;
         return 1;
      }
   }
   else if (buyHugCount >= MinConsolidationBars)
      Print("BUY reject: floor=", buyFloor, " anchor=", buyFloorAnchor,
            " drift=", (buyFloorAnchor - buyFloor) / _Point, "pts fastMA=", fastMA,
            " slowMA=", slowMA, " spread=", (slowMA - fastMA) / _Point, "pts (need floor<fastMA<slowMA-minSpread, drift<=", ConsolidationSpreadDollars, ")");

   return 0;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void DrawPrevZoneLine(datetime prevTime, double prevPrice, datetime curTime, double curPrice, bool isSell)
{
   string name = (isSell ? "PZL_S_" : "PZL_B_") + IntegerToString((long)curTime);
   ObjectDelete(0, name);
   if (!ObjectCreate(0, name, OBJ_TREND, 0, prevTime, prevPrice, curTime, curPrice)) return;
   ObjectSetInteger(0, name, OBJPROP_COLOR,  isSell ? clrOrangeRed : clrDeepSkyBlue);
   ObjectSetInteger(0, name, OBJPROP_STYLE,  STYLE_SOLID);
   ObjectSetInteger(0, name, OBJPROP_WIDTH,  3);
   ObjectSetInteger(0, name, OBJPROP_RAY_RIGHT, false);
}

void DrawConsolidationMark(datetime t, double price, bool isStart, bool isSell)
{
   string name = (isSell ? "SC_" : "BC_") + (isStart ? "S_" : "E_") + IntegerToString((long)t);
   ObjectDelete(0, name);
   if (!ObjectCreate(0, name, OBJ_ARROW, 0, t, price)) return;
   ObjectSetInteger(0, name, OBJPROP_ARROWCODE, isSell ? 234 : 233); // 234=down, 233=up
   ObjectSetInteger(0, name, OBJPROP_COLOR, isStart ? clrAqua : clrMagenta);
   ObjectSetInteger(0, name, OBJPROP_WIDTH, 1);
}

void DrawZone(double hi, double lo)
{
   ObjectDelete(0, "AlphaZoneHigh");
   ObjectDelete(0, "AlphaZoneLow");
   ObjectCreate(0, "AlphaZoneHigh", OBJ_HLINE, 0, 0, hi);
   ObjectSetInteger(0, "AlphaZoneHigh", OBJPROP_COLOR, clrSilver);
   ObjectSetInteger(0, "AlphaZoneHigh", OBJPROP_STYLE, STYLE_DASH);
   ObjectCreate(0, "AlphaZoneLow", OBJ_HLINE, 0, 0, lo);
   ObjectSetInteger(0, "AlphaZoneLow", OBJPROP_COLOR, clrSilver);
   ObjectSetInteger(0, "AlphaZoneLow", OBJPROP_STYLE, STYLE_DASH);
}

void DrawEntry(bool isBuy)
{
   string name  = (isBuy ? "AlphaBuyArrow_" : "AlphaSellArrow_") + IntegerToString(TimeCurrent());
   double price = isBuy ? SymbolInfoDouble(_Symbol, SYMBOL_ASK) : SymbolInfoDouble(_Symbol, SYMBOL_BID);
   ObjectCreate(0, name, isBuy ? OBJ_ARROW_UP : OBJ_ARROW_DOWN, 0, TimeCurrent(), price);
   ObjectSetInteger(0, name, OBJPROP_COLOR, isBuy ? clrLime : clrRed);
   ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);
}


void DrawTradeLabel(bool isBuy, bool isFlip, bool isTrending = false, double dxyDelta = 0)
{
   string name  = "TradeLabel_" + IntegerToString(TimeCurrent());
   double price = isBuy ? SymbolInfoDouble(_Symbol, SYMBOL_ASK) : SymbolInfoDouble(_Symbol, SYMBOL_BID);
   string text;
   color  clr;
   if (isTrending)
   {
      string deltaStr = (dxyDelta >= 0 ? "+" : "") + DoubleToString(dxyDelta, 4);
      text = (isBuy ? "TRENDING BUY" : "TRENDING SELL") + " DXY:" + deltaStr;
      clr  = isBuy ? clrAqua : clrGold;
   }
   else if (isFlip)
   {
      text = isBuy ? "FLIP TO BUY"  : "FLIP TO SELL";
      clr  = isBuy ? clrOrange      : clrViolet;
   }
   else
   {
      text = isBuy ? "NORMAL BUY"   : "NORMAL SELL";
      clr  = isBuy ? clrLime        : clrRed;
   }
   ObjectCreate(0, name, OBJ_TEXT, 0, TimeCurrent(), price);
   ObjectSetString(0, name, OBJPROP_TEXT, text);
   ObjectSetInteger(0, name, OBJPROP_COLOR, clr);
   ObjectSetInteger(0, name, OBJPROP_FONTSIZE, 8);
}

void DrawStopLine(double price)
{
   if (ObjectFind(0, "AlphaBreakEven") < 0)
   {
      ObjectCreate(0, "AlphaBreakEven", OBJ_HLINE, 0, 0, price);
      ObjectSetInteger(0, "AlphaBreakEven", OBJPROP_COLOR, clrDodgerBlue);
      ObjectSetInteger(0, "AlphaBreakEven", OBJPROP_WIDTH, 3);
      ObjectSetInteger(0, "AlphaBreakEven", OBJPROP_STYLE, STYLE_SOLID);
   }
   else
      ObjectSetDouble(0, "AlphaBreakEven", OBJPROP_PRICE, price);
}

void CleanupLines()
{
   ObjectDelete(0, "AlphaZoneHigh");
   ObjectDelete(0, "AlphaZoneLow");
   ObjectDelete(0, "AlphaBreakEven");
}

// ── Trending setup detector ───────────────────────────────────────────────────
// Returns true if a trending entry is valid.
// outDir: 1=BUY, -1=SELL   outSL: stop-loss price
// Calculates DXY from the 6 major USD pairs using the official ICE formula:
// DXY = 50.14348112 × EUR/USD^(-0.576) × USD/JPY^(0.136) × GBP/USD^(-0.119)
//                   × USD/CAD^(0.091)  × USD/SEK^(0.042)  × USD/CHF^(0.036)
double CalcDXY()
{
   double eurusd = SymbolInfoDouble("EURUSD", SYMBOL_BID);
   double usdjpy = SymbolInfoDouble("USDJPY", SYMBOL_BID);
   double gbpusd = SymbolInfoDouble("GBPUSD", SYMBOL_BID);
   double usdcad = SymbolInfoDouble("USDCAD", SYMBOL_BID);
   double usdsek = SymbolInfoDouble("USDSEK", SYMBOL_BID);
   double usdchf = SymbolInfoDouble("USDCHF", SYMBOL_BID);

   if (eurusd <= 0 || usdjpy <= 0 || gbpusd <= 0 ||
       usdcad <= 0 || usdsek <= 0 || usdchf <= 0) return 0;

   return 50.14348112
        * MathPow(eurusd, -0.576)
        * MathPow(usdjpy,  0.136)
        * MathPow(gbpusd, -0.119)
        * MathPow(usdcad,  0.091)
        * MathPow(usdsek,  0.042)
        * MathPow(usdchf,  0.036);
}

// Updates the DXY snapshot every 7 minutes — call once per bar
void UpdateDXYSnapshot()
{
   datetime now = TimeCurrent();
   if (dxySnapshot == 0 || (now - dxySnapshotTime) >= DXY_INTERVAL_SECS)
   {
      double calc = CalcDXY();
      if (calc > 0)
      {
         dxySnapshot     = calc;
         dxySnapshotTime = now;
         Print("DXY snapshot updated: ", DoubleToString(dxySnapshot, 5), " at ", TimeToString(now));
      }
   }
}

// Returns true if DXY momentum confirms the trade direction
// BUY gold: DXY must be falling (current < snapshot)
// SELL gold: DXY must be rising (current > snapshot)
bool DXYConfirms(int dir)
{
   if (dxySnapshot == 0) return true; // no snapshot yet — don't block trades
   double currentDXY = CalcDXY();
   if (currentDXY <= 0) return true;  // pairs unavailable — don't block trades
   double change = currentDXY - dxySnapshot;
   Print("DXY confirmation: snapshot=", DoubleToString(dxySnapshot, 5),
         " current=", DoubleToString(currentDXY, 5),
         " change=", DoubleToString(change, 5),
         " dir=", (dir == 1 ? "BUY" : "SELL"),
         " confirm=", (dir == 1 ? (change < 0) : (change > 0)));
   return (dir == 1) ? (change < 0) : (change > 0);
}

// Returns true if candle at bar `b` touches the MA value (wick or body overlaps)
bool CandleTouchesMA(int bar, double maVal)
{
   double hi = iHigh(_Symbol, _Period, bar);
   double lo = iLow (_Symbol, _Period, bar);
   return (maVal >= lo && maVal <= hi);
}

bool CheckTrendingSetup(int &outDir, double &outSL)
{
   // Get EMA 200 and MA 50 values at bar 1
   int h200 = iMA(_Symbol, _Period, 200, 0, MODE_EMA, PRICE_CLOSE);
   int h50  = iMA(_Symbol, _Period, 50,  0, MODE_EMA, PRICE_CLOSE);
   if (h200 == INVALID_HANDLE || h50 == INVALID_HANDLE)
   { IndicatorRelease(h200); IndicatorRelease(h50); return false; }

   double buf200[3], buf50[3];
   ArraySetAsSeries(buf200, true); ArraySetAsSeries(buf50, true);
   bool ok = (CopyBuffer(h200, 0, 1, 3, buf200) > 0 &&
              CopyBuffer(h50,  0, 1, 3, buf50)  > 0);
   IndicatorRelease(h200); IndicatorRelease(h50);
   if (!ok) return false;

   double ema200_1 = buf200[0]; // bar 1 (newest)
   double ema50_1  = buf50[0];  // bar 1
   double ema50_3  = buf50[2];  // bar 3 (oldest)

   // Direction: current bar close vs EMA 200
   double close1 = iClose(_Symbol, _Period, 1);
   bool   wantBuy  = (close1 > ema200_1);
   bool   wantSell = (close1 < ema200_1);
   if (!wantBuy && !wantSell) return false;

   // FastMA vs SlowMA must confirm direction
   double fastMA = CalcSMA(FastMAPeriod, 1);
   double slowMA = CalcSMA(SlowMAPeriod, 1);
   if (wantBuy  && fastMA <= slowMA) return false;
   if (wantSell && fastMA >= slowMA) return false;

   // Staircase chain: close[older] ≈ open[newer] within tolerance
   double chainTol = DollarsToPriceDist(ConsolidationSpreadDollars);
   for (int i = 1; i < 3; i++)
   {
      double closeOlder = iClose(_Symbol, _Period, i + 1);
      double openNewer  = iOpen (_Symbol, _Period, i);
      if (MathAbs(closeOlder - openNewer) > chainTol) return false;
   }

   // Count directional candles among the 3
   int dirCount = 0;
   for (int i = 1; i <= 3; i++)
   {
      double o = iOpen (_Symbol, _Period, i);
      double c = iClose(_Symbol, _Period, i);
      if (wantBuy  && c > o) dirCount++;
      if (wantSell && c < o) dirCount++;
   }

   // Candle 3 must touch MA 50
   bool candle3TouchesMA50 = CandleTouchesMA(3, ema50_3);
   if (!candle3TouchesMA50) return false;

   // Setup 1: at least 2 directional candles + candle 1 near/touching MA 200
   //          (candle 1 between MA 50 and MA 200, or touching MA 200)
   bool candle1NearMA200 = CandleTouchesMA(1, ema200_1) ||
                           (wantSell && close1 >= ema200_1 && close1 <= ema50_1) ||
                           (wantBuy  && close1 <= ema200_1 && close1 >= ema50_1);
   bool setup1 = (dirCount >= 2) && candle1NearMA200;

   // Setup 2: all 3 candles directional
   bool setup2 = (dirCount == 3);

   if (!setup1 && !setup2) return false;

   if (wantBuy)
   {
      outDir = 1;
      double lowestLow = iLow(_Symbol, _Period, 1);
      for (int i = 2; i <= 3; i++) { double l = iLow(_Symbol, _Period, i); if (l < lowestLow) lowestLow = l; }
      outSL = lowestLow - DollarsToPriceDist(StopLossDollars);
   }
   else
   {
      outDir = -1;
      double highestHigh = iHigh(_Symbol, _Period, 1);
      for (int i = 2; i <= 3; i++) { double h = iHigh(_Symbol, _Period, i); if (h > highestHigh) highestHigh = h; }
      outSL = highestHigh + DollarsToPriceDist(StopLossDollars);
   }

   return true;
}

// ── Trade execution ───────────────────────────────────────────────────────────

void CloseAll()
{
   for (int i = PositionsTotal() - 1; i >= 0; i--)
      if (PositionGetSymbol(i) == _Symbol && PositionGetInteger(POSITION_MAGIC) == magicNumber)
         trade.PositionClose(PositionGetInteger(POSITION_TICKET));
}


void ExecMarket(int dir)
{
   double slDist = DollarsToPriceDist(StopLossDollars);

   if (dir == -1)
   {
      double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
      double sl  = bid + slDist;
      if (!trade.Sell(Money_FixLot_Lots, _Symbol, bid, sl, 0))
      {
         Print("SELL failed: ", trade.ResultRetcode(), " ", trade.ResultRetcodeDescription());
         return;
      }
      if (!inTrade) firstEntryPrice = bid;
   }
   else
   {
      double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
      double sl  = ask - slDist;
      if (!trade.Buy(Money_FixLot_Lots, _Symbol, ask, sl, 0))
      {
         Print("BUY failed: ", trade.ResultRetcode(), " ", trade.ResultRetcodeDescription());
         return;
      }
      if (!inTrade) firstEntryPrice = ask;
   }

   inTrade          = true;
   entryCount++;
   exitCooldownBars = MinConsolidationBars;
   DrawEntry(dir == 1);
   DrawTradeLabel(dir == 1, isFlipTrade, isTrendingTrade);
}

void MoveSLToPrice(double newSL)
{
   for (int i = 0; i < PositionsTotal(); i++)
   {
      if (PositionGetSymbol(i) != _Symbol || PositionGetInteger(POSITION_MAGIC) != magicNumber)
         continue;
      ulong  ticket    = PositionGetInteger(POSITION_TICKET);
      double currentSL = PositionGetDouble(POSITION_SL);
      long   posType   = PositionGetInteger(POSITION_TYPE);

      // Only move SL in the profitable direction — never worsen it
      bool isBuy  = (posType == POSITION_TYPE_BUY);
      bool better = isBuy ? (newSL > currentSL) : (newSL < currentSL);
      if (!better) continue;

      MqlTradeRequest req; MqlTradeResult res; ZeroMemory(req);
      req.action   = TRADE_ACTION_SLTP;
      req.symbol   = _Symbol;
      req.position = ticket;
      req.sl       = newSL;
      req.tp       = PositionGetDouble(POSITION_TP);
      if (OrderSend(req, res)) DrawStopLine(newSL);
   }
}

void MoveToBreakEven()
{
   for (int i = 0; i < PositionsTotal(); i++)
   {
      if (PositionGetSymbol(i) != _Symbol || PositionGetInteger(POSITION_MAGIC) != magicNumber)
         continue;
      double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
      MoveSLToPrice(openPrice);
   }
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void ResetAll()
{
   pendingDir       = 0;
   watchBars        = 0;
   inTrade          = false;
   entryCount       = 0;
   peakProfit       = 0.0; trendingPeakProfit = 0.0;
   trailingActive   = false;
   firstEntryPrice  = 0.0;
   breakEvenDone    = false;
   zoneHigh         = -1;
   zoneLow          = -1;
   exitCooldownBars = 0;
   isFlipTrade      = false;
   isTrendingTrade  = false;
   sellCeiling      = 0;  sellHugCount = 0;  sellConsolidationLow  = DBL_MAX;
   buyFloor         = 0;  buyHugCount  = 0;  buyConsolidationHigh  = -DBL_MAX;
   exitPendingDir   = 0;
   exitZoneHigh     = 0;
   exitZoneLow      = 0;
   exitWatchBars    = 0;
   CleanupLines();
}

// ── OnTick ────────────────────────────────────────────────────────────────────

void OnTick()
{
   if (consecutiveLosses >= MaxConsecutiveLosses) return;

   // ── Trailing stop & break-even — every tick ───────────────────────────────
   int posCount = CountMagicPositions();

   if (posCount > 0)
   {
      double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);

      if (accountProfit >= TakeProfitDollars)
      {
         consecutiveLosses = 0;
         CloseAll();
         ResetAll();
         return;
      }

      if (accountProfit > peakProfit) peakProfit = accountProfit;
      if (isTrendingTrade && accountProfit > trendingPeakProfit) trendingPeakProfit = accountProfit;

      // Break-even skipped for trending trades — gradual trail handles SL advancement
      if (inTrade && !breakEvenDone && !isTrendingTrade && accountProfit >= StopLossDollars)
      {
         Print("BREAKEVEN fired: flip=", isFlipTrade, " profit=", DoubleToString(accountProfit, 2));
         MoveToBreakEven();
         breakEvenDone = true;
      }

      if (peakProfit >= StopLossDollars * TrailingActivateFactor)
      {
         if (!trailingActive)
            Print("TRAILING activated: flip=", isFlipTrade, " peak=", DoubleToString(peakProfit, 2));
         trailingActive = true;
         double speedFactor = TrailingSpeedFactor;
         double drawback    = StopLossDollars * speedFactor;
         double trailingSL  = DollarsToPriceDist(peakProfit - drawback);

         // For trending trades: SL advances from the original SL level toward entry
         // and beyond — starting at (entry - SL) when trailing activates, reaching
         // break-even when peak = activation + SL, then locking in profit beyond that.
         // For normal/flip trades: trail from entry as before.
         for (int i = 0; i < PositionsTotal(); i++)
         {
            if (PositionGetSymbol(i) != _Symbol || PositionGetInteger(POSITION_MAGIC) != magicNumber)
               continue;
            double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
            long   posType   = PositionGetInteger(POSITION_TYPE);
            double newSL;
            if (isTrendingTrade)
            {
               // originalSL = entry ± StopLossDollars
               // advance it 1:1 with profit above activation threshold
               double slDist       = DollarsToPriceDist(StopLossDollars);
               double activateDist = DollarsToPriceDist(StopLossDollars * TrailingActivateFactor);
               double advance      = (DollarsToPriceDist(peakProfit) - activateDist) * TrendingTrailAdvanceFactor;
               newSL = (posType == POSITION_TYPE_BUY)
                       ? (openPrice - slDist) + advance
                       : (openPrice + slDist) - advance;
            }
            else
            {
               newSL = (posType == POSITION_TYPE_BUY)
                       ? openPrice + trailingSL
                       : openPrice - trailingSL;
            }
            MoveSLToPrice(newSL);
         }
      }
   }

   // ── Per-bar logic ─────────────────────────────────────────────────────────
   datetime currentBar = iTime(_Symbol, _Period, 0);
   if (currentBar == lastProcessedBar) return;
   lastProcessedBar = currentBar;
   UpdateDXYSnapshot();

   posCount = CountMagicPositions();

   // ── Position just closed ──────────────────────────────────────────────────
   if (inTrade && posCount == 0)
   {
      HistorySelect(0, TimeCurrent());
      double lastProfit = 0;
      for (int i = HistoryDealsTotal() - 1; i >= 0; i--)
      {
         ulong dt = HistoryDealGetTicket(i);
         if (HistoryDealGetString(dt, DEAL_SYMBOL) == _Symbol &&
             HistoryDealGetInteger(dt, DEAL_MAGIC) == magicNumber &&
             HistoryDealGetInteger(dt, DEAL_ENTRY) == DEAL_ENTRY_OUT)
         {
            lastProfit = HistoryDealGetDouble(dt, DEAL_PROFIT);
            break;
         }
      }
      if (lastProfit < 0) consecutiveLosses++; else consecutiveLosses = 0;
      Print("TRADE_CLOSED: flip=", isFlipTrade, " trending=", isTrendingTrade,
            " profit=", DoubleToString(lastProfit, 2),
            " peak=", DoubleToString(peakProfit, 2),
            (isTrendingTrade ? " trendingPeak=" + DoubleToString(trendingPeakProfit, 2) : ""),
            " trailingActive=", trailingActive,
            " breakEvenDone=", breakEvenDone);
      ResetAll();
      return;
   }

   // ── Waiting for price to revisit the trigger ──────────────────────────────
   if (!inTrade && pendingDir != 0)
   {
      watchBars++;

      if (watchBars > EntryWindowBars)
      {
         Print("Zone expired. Dir=", pendingDir, " WatchBars=", watchBars);
         ResetAll();
         return;
      }

      double prevHigh = iHigh(_Symbol, _Period, 1);
      double prevLow  = iLow(_Symbol,  _Period, 1);

      // SELL: bar high enters the consolidation band (within tolerance of ceiling)
      // BUY:  bar low  enters the consolidation band (within tolerance of floor)
      double entryTol  = ConsolidationSpreadDollars;
      bool triggered = (pendingDir == -1 && prevHigh >= zoneHigh - entryTol) ||
                       (pendingDir ==  1 && prevLow  <= zoneLow  + entryTol);

      Print("Watch: bar=", watchBars, "/", EntryWindowBars,
            " dir=", pendingDir,
            " high=", prevHigh, " low=", prevLow,
            " trigger=", (pendingDir == -1 ? zoneHigh : zoneLow),
            " hit=", triggered);

      if (triggered && entryCount < MaxEntriesPerZone)
      {
         double entryPrice = (pendingDir == -1)
                             ? SymbolInfoDouble(_Symbol, SYMBOL_BID)
                             : SymbolInfoDouble(_Symbol, SYMBOL_ASK);

         Print("ENTRY dir=", (pendingDir == -1 ? "SELL" : "BUY"),
               " flip=", isFlipTrade,
               " price=", DoubleToString(entryPrice, _Digits));
         ExecMarket(pendingDir);
      }

      return;
   }

   // ── In trade — monitor for opposing exit signal ──────────────────────────
   if (inTrade && posCount > 0)
   {
      // Determine current trade direction
      long tradeType = -1;
      for (int i = 0; i < PositionsTotal(); i++)
         if (PositionGetSymbol(i) == _Symbol && PositionGetInteger(POSITION_MAGIC) == magicNumber)
            { tradeType = PositionGetInteger(POSITION_TYPE); break; }

      if (exitPendingDir != 0)
      {
         // Watching for the exit trigger
         exitWatchBars++;
         if (exitWatchBars > EntryWindowBars)
         {
            Print("Exit zone expired. Clearing.");
            exitPendingDir = 0; exitZoneHigh = 0; exitZoneLow = 0; exitWatchBars = 0;
         }
         else
         {
            double prevHigh = iHigh(_Symbol, _Period, 1);
            double prevLow  = iLow (_Symbol, _Period, 1);
            double entryTol = ConsolidationSpreadDollars;
            bool exitTriggered = (exitPendingDir == -1 && prevHigh >= exitZoneHigh - entryTol) ||
                                 (exitPendingDir ==  1 && prevLow  <= exitZoneLow  + entryTol);
            Print("Exit watch: bar=", exitWatchBars, "/", EntryWindowBars,
                  " dir=", exitPendingDir, " hit=", exitTriggered);
            if (exitTriggered)
            {
               Print("EXIT SIGNAL fired — closing early: flip=", isFlipTrade,
                     " profit=", DoubleToString(AccountInfoDouble(ACCOUNT_PROFIT), 2),
                     " peak=", DoubleToString(peakProfit, 2));
               CloseAll();
               ResetAll();
               return;
            }
         }
      }
      else
      {
         // Flip and trending trades ride the trend — skip exit monitor for them
         if (isFlipTrade || isTrendingTrade) return;

         // Wait for cooldown before scanning for exit signals
         if (exitCooldownBars > 0) { exitCooldownBars--; return; }

         // Look for a confirmed zone in the opposing direction only
         double newHi, newLo;
         double dummy1, dummy2;
         int zoneDir = UpdateConsolidation(newHi, newLo, dummy1, dummy2);
         bool isBuyTrade = (tradeType == POSITION_TYPE_BUY);
         if ((isBuyTrade && zoneDir == -1) || (!isBuyTrade && zoneDir == 1))
         {
            exitPendingDir = zoneDir;
            exitZoneHigh   = newHi;
            exitZoneLow    = newLo;
            exitWatchBars  = 0;
            Print("EXIT ZONE armed: flip=", isFlipTrade,
                  " dir=", exitPendingDir,
                  " trigger=", (exitPendingDir == -1 ? newHi : newLo),
                  " cooldown=", exitCooldownBars);
         }
      }
      return;
   }

   // ── Idle — update running consolidation state, then try trending ────────────
   if (!inTrade && pendingDir == 0)
   {
      double newHi, newLo, newConsLow, newConsHigh;
      int zoneDir = UpdateConsolidation(newHi, newLo, newConsLow, newConsHigh);

      // ── Trending pattern fires first; consolidation only if enabled ──────────
      if (zoneDir == 0 || !EnableConsolidationTrades)
      {
         int    trendDir = 0;
         double trendSL  = 0;
         if (CheckTrendingSetup(trendDir, trendSL) && DXYConfirms(trendDir))
         {
            double curDXY    = CalcDXY();
            double dxyDelta  = (dxySnapshot > 0 && curDXY > 0) ? (curDXY - dxySnapshot) : 0;
            Print("TRENDING_ENTRY: dir=", (trendDir == 1 ? "TRENDING BUY" : "TRENDING SELL"),
                  " sl=", DoubleToString(trendSL, _Digits),
                  " dxy_delta=", DoubleToString(dxyDelta, 4));
            isFlipTrade     = false;
            isTrendingTrade = true;
            ExecMarket(trendDir);
            // Redraw label with DXY delta (replaces the one drawn inside ExecMarket)
            DrawTradeLabel(trendDir == 1, false, true, dxyDelta);
            MoveSLToPrice(trendSL);
         }
         return;
      }

      // EMA 200 cross check: no cross in last 100 bars = strongly trending → flip
      // Only flip if the flipped direction agrees with price's position relative to EMA 200:
      //   flip to SELL only if price is below EMA 200
      //   flip to BUY  only if price is above EMA 200
      // No EMA 200 cross = strongly trending — skip consolidation, let trending pattern handle it
      if (!HasEMA200Cross(100))
      {
         Print("EMA200: no cross in 100 bars — skipping consolidation zone, trending pattern will handle");
         return;
      }

      isFlipTrade = false;
      zoneHigh    = newHi;
      zoneLow     = newLo;
      pendingDir  = zoneDir;
      watchBars   = 0;

      DrawZone(zoneHigh, zoneLow);
      Print("Zone armed: dir=", pendingDir,
            " trigger=", (pendingDir == -1 ? zoneHigh : zoneLow),
            " window=", EntryWindowBars, " bars");
   }
}

int OnInit()
{
   trade.SetExpertMagicNumber(magicNumber);
   return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
   CleanupLines();
}
