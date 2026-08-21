#include <Trade\Trade.mqh>

MqlTradeRequest request;
MqlTradeResult result;

input double Money_FixLot_Lots = 0.01;
int magicNumber = 109814;
static bool buying = false;
static bool selling = false;
static bool hasbullishCrossing = false;
static bool hasBearishCrossing = true;
double AVERAGE_CANDLE_HEIGHT = 0.30;

// Maximum simultaneous entries in the same direction
input int MaxEntries = 5;

// Set stop loss. This is can be changed from the UI
input double stopLoss = -20.0;

// Set take profit. This is can be changed from the UI
input double takeProfit = 70.0;

// Average profit
input double averageProfit = 20.0;

// Has hit average profit
bool hasReachedAverageProfit = false;


// Maximum number of failed trade before final exit (Stop trading)
input int maximumNumOfFailedTrades = 10;

// Exit with minimum profit
input int minimumProfit = 10;

// The offset range
input double offset = 0.01;

input int candleCount = 3;

input int shift = 0;
input int RangeBars = 10;
input double MaxRangePoints = 500;
input int MinimumConsolidationBars = 5;
input double MaxLowRangePoints = 150;


// Sell entry — high consolidation (mirrors MaxLowRangePoints for buy)
input double MaxHighConsolidationPoints = 150;

// Minimum price swing (high–low) required between the swing high/low and the
// consolidation zone. Ensures a genuine move happened before the cluster.
// 300 = $3.00 on XAUUSD. Raise if still triggering at tops.
input double MinimumDropPoints = 300;

// How many bars to look back when searching for the macro swing high (buy)
// or swing low (sell) that preceded the consolidation.
// Should be larger than RangeBars so the drop/rise is measured in broader context.
input int SwingLookback = 50;

// Bars on each side required to confirm a swing high or low pivot point.
// Higher = fewer but more significant pivots. 3 is a good starting point on M1.
input int SwingStrength = 3;


static double previousCandleOpen = 0.0;
static double previousCandleClose = 0.0;
static int tradeCount = 0;
static double peakProfit = 0.0;
static double entryPrice = 0.0;

int CountSymbolPositions()
{
   int count = 0;
   for (int i = 0; i < PositionsTotal(); i++)
      if (PositionGetSymbol(i) == _Symbol) count++;
   return count;
}

void DrawTrailingStopLine(double trailStopPrice)
{
   string name = "TrailingStopLine";
   if (ObjectFind(0, name) < 0)
   {
      ObjectCreate(0, name, OBJ_HLINE, 0, 0, trailStopPrice);
      ObjectSetInteger(0, name, OBJPROP_COLOR, clrLightBlue);
      ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);
      ObjectSetInteger(0, name, OBJPROP_STYLE, STYLE_DASH);
   }
   else
   {
      ObjectSetDouble(0, name, OBJPROP_PRICE, trailStopPrice);
   }
}

void DrawBuySignal()
{
   string name = "BuySignal_" + IntegerToString(TimeCurrent());
   double arrowPrice = iLow(_Symbol, PERIOD_CURRENT, 0) - 50 * _Point;

   ObjectCreate(0, name, OBJ_ARROW_UP, 0, TimeCurrent(), arrowPrice);
   ObjectSetInteger(0, name, OBJPROP_COLOR, clrLime);
   ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);
   ObjectSetInteger(0, name, OBJPROP_ARROWCODE, 241);
}

void DrawSellSignal()
{
   string name = "SellSignal_" + IntegerToString(TimeCurrent());
   double arrowPrice = iHigh(_Symbol, PERIOD_CURRENT, 0) + 50 * _Point;

   ObjectCreate(0, name, OBJ_ARROW_DOWN, 0, TimeCurrent(), arrowPrice);
   ObjectSetInteger(0, name, OBJPROP_COLOR, clrRed);
   ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);
   ObjectSetInteger(0, name, OBJPROP_ARROWCODE, 242);
}

void Buy() {
    ZeroMemory(request);
    request.action = TRADE_ACTION_DEAL;
    request.type = ORDER_TYPE_BUY;
    request.symbol = _Symbol;
    request.volume = Money_FixLot_Lots;
    request.type_filling = ORDER_FILLING_FOK;
    request.price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    request.tp = 0;
    request.deviation = 50;

    if (OrderSend(request, result) && result.retcode == TRADE_RETCODE_DONE) {
        buying = true;
        entryPrice = request.price;
        Print("TRADE_ENTRY | direction=BUY | price=", entryPrice,
              " | lots=", Money_FixLot_Lots,
              " | stopLoss=", stopLoss,
              " | takeProfit=", takeProfit);
    }
}

void Sell() {
    ZeroMemory(request);
    request.action = TRADE_ACTION_DEAL;
    request.type = ORDER_TYPE_SELL;
    request.symbol = _Symbol;
    request.volume = Money_FixLot_Lots;
    request.type_filling = ORDER_FILLING_FOK;
    request.price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    request.tp = 0;
    request.deviation = 50;

    if (OrderSend(request, result) && result.retcode == TRADE_RETCODE_DONE) {
        selling = true;
        entryPrice = request.price;
        Print("TRADE_ENTRY | direction=SELL | price=", entryPrice,
              " | lots=", Money_FixLot_Lots,
              " | stopLoss=", stopLoss,
              " | takeProfit=", takeProfit);
    }
}

// Returns the consolidation base low if at least MinimumConsolidationBars
// of the last RangeBars completed candles have lows within MaxLowRangePoints.
// Returns -1 if consolidation is not confirmed.
// Pass shift=1 to examine only completed candles (exclude the live bar 0).
double getConsolidationLow(int shift)
{
   // Step 1: find the consolidation zone floor within the tight RangeBars window.
   int lowestLowIndex =
      iLowest(_Symbol, PERIOD_CURRENT, MODE_LOW, RangeBars, shift);
   double lowestLow =
      iLow(_Symbol, PERIOD_CURRENT, lowestLowIndex);

   // Step 2: find the macro swing high over the wider SwingLookback window.
   // This captures the real peak that price dropped FROM before consolidating.
   int swingHighIndex =
      iHighest(_Symbol, PERIOD_CURRENT, MODE_HIGH, SwingLookback, shift);
   double swingHigh = iHigh(_Symbol, PERIOD_CURRENT, swingHighIndex);

   // Step 3: confirm the drop is large enough to be meaningful.
   double dropRange = swingHigh - lowestLow;
   if (dropRange < MinimumDropPoints * _Point)
      return -1;

   // Step 4: sequence check — swing high must be older than the consolidation low.
   // Higher index = older bar, so swingHighIndex > lowestLowIndex means
   // price peaked first then dropped into the zone. Rejects consolidation at tops.
   if (swingHighIndex <= lowestLowIndex)
      return -1;

   double zoneTop = lowestLow + MaxLowRangePoints * _Point;

   // Count how many of the last RangeBars candles have lows within the zone.
   // Requiring MinimumConsolidationBars = 5 naturally rejects gradual downtrends
   // because only 2-3 bars ever sit within MaxLowRangePoints of the lowest low
   // during a decline, never enough to reach 5.
   int consolidatingCount = 0;
   for (int i = shift; i < shift + RangeBars; i++)
   {
      double candleLow = iLow(_Symbol, PERIOD_CURRENT, i);
      if (candleLow >= lowestLow && candleLow <= zoneTop)
         consolidatingCount++;
   }

   Print("Support=", lowestLow, " | zoneTop=", zoneTop,
         " | consolidatingCount=", consolidatingCount,
         " | needed=", MinimumConsolidationBars);

   return (consolidatingCount >= MinimumConsolidationBars) ? lowestLow : -1;
}

// Returns true when the last RangeBars completed candles are consolidating
// AND the current candle has entered the consolidation range via its wick (low)
// or its body open — whichever touches the zone first.
bool isLowConsolidating()
{
   double baseLow = getConsolidationLow(1); // start at bar 1 — skip live candle
   if (baseLow < 0)
   {
      Print("Consolidation: not confirmed (fewer than ", MinimumConsolidationBars, " candles in range)");
      return false;
   }

   double rangeTop  = baseLow + MaxLowRangePoints * _Point;

   double currentLow  = iLow(_Symbol,  PERIOD_CURRENT, 0);
   double currentOpen = iOpen(_Symbol, PERIOD_CURRENT, 0);

   // Bar 0 is in the consolidation zone if its low or open is within [baseLow, rangeTop]
   // Allow a small tolerance below baseLow for wicks that test support
   double tolerance = MaxLowRangePoints * _Point * 0.2;
   bool wickInRange = (currentLow  >= baseLow - tolerance && currentLow  <= rangeTop);
   bool openInRange = (currentOpen >= baseLow              && currentOpen <= rangeTop);

   Print("Consolidation zone: ", baseLow, " – ", rangeTop,
         " | currentLow=", currentLow,
         " | currentOpen=", currentOpen,
         " | wickInRange=", wickInRange,
         " | openInRange=", openInRange);

   return wickInRange || openInRange;
}


// Returns the consolidation base high if at least MinimumConsolidationBars
// of the last RangeBars completed candles have highs within MaxHighConsolidationPoints
// of the highest high. Mirror of getConsolidationLow for sell entry detection.
double getConsolidationHigh(int shift)
{
   // Step 1: find the consolidation zone ceiling within the tight RangeBars window.
   int highestHighIndex =
      iHighest(_Symbol, PERIOD_CURRENT, MODE_HIGH, RangeBars, shift);
   double highestHigh =
      iHigh(_Symbol, PERIOD_CURRENT, highestHighIndex);

   // Step 2: find the macro swing low over the wider SwingLookback window.
   // This captures the real trough that price rallied FROM before consolidating.
   int swingLowIndex =
      iLowest(_Symbol, PERIOD_CURRENT, MODE_LOW, SwingLookback, shift);
   double swingLow = iLow(_Symbol, PERIOD_CURRENT, swingLowIndex);

   // Step 3: confirm the rally is large enough to be meaningful.
   double rallyRange = highestHigh - swingLow;
   if (rallyRange < MinimumDropPoints * _Point)
      return -1;

   // Step 4: sequence check — swing low must be older than the consolidation high.
   // Higher index = older bar, so swingLowIndex > highestHighIndex means
   // price bottomed first then rose into the zone. Rejects consolidation at bottoms.
   if (swingLowIndex <= highestHighIndex)
      return -1;

   double zoneBottom = highestHigh - MaxHighConsolidationPoints * _Point;

   int consolidatingCount = 0;
   for (int i = shift; i < shift + RangeBars; i++)
   {
      double candleHigh = iHigh(_Symbol, PERIOD_CURRENT, i);
      if (candleHigh >= zoneBottom && candleHigh <= highestHigh)
         consolidatingCount++;
   }

   return (consolidatingCount >= MinimumConsolidationBars) ? highestHigh : -1;
}

// Returns true when the last RangeBars completed candles are consolidating at a top
// AND the current candle has entered the high zone via its wick (high) or open.
// Mirror of isLowConsolidating for sell entry.
bool isHighConsolidatingEntry()
{
   double baseHigh = getConsolidationHigh(1);
   if (baseHigh < 0)
      return false;

   double zoneBottom = baseHigh - MaxHighConsolidationPoints * _Point;

   double currentHigh = iHigh(_Symbol,  PERIOD_CURRENT, 0);
   double currentOpen = iOpen(_Symbol, PERIOD_CURRENT, 0);

   double tolerance = MaxHighConsolidationPoints * _Point * 0.2;
   bool wickInRange = (currentHigh <= baseHigh + tolerance && currentHigh >= zoneBottom);
   bool openInRange = (currentOpen >= zoneBottom && currentOpen <= baseHigh);

   return wickInRange || openInRange;
}


bool isConsolidating(int shift)
{
   for(int start = shift;
       start <= shift + RangeBars - MinimumConsolidationBars;
       start++)
   {
      int highestIndex =
         iHighest(_Symbol,
                  PERIOD_CURRENT,
                  MODE_HIGH,
                  MinimumConsolidationBars,
                  start);

      int lowestIndex =
         iLowest(_Symbol,
                 PERIOD_CURRENT,
                 MODE_LOW,
                 MinimumConsolidationBars,
                 start);

      double highest =
         iHigh(_Symbol,
               PERIOD_CURRENT,
               highestIndex);

      double lowest =
         iLow(_Symbol,
              PERIOD_CURRENT,
              lowestIndex);

      double range =
         (highest - lowest) / _Point;

      if(range <= MaxRangePoints)
      {
         return true;
      }
   }

   return false;
}

void CloseAllOrders() {
    CTrade trade;

    int i = PositionsTotal() - 1;

    while (i >= 0) {
        if (trade.PositionClose(PositionGetSymbol(i)))
            i--;
    }
}

void CloseAll() {
    CloseAllOrders();
    selling = false;
    buying = false;
    previousCandleOpen = 0.0;
    previousCandleClose = 0.0;
    hasbullishCrossing = false;
    hasBearishCrossing = false;
    hasReachedAverageProfit = false;
    peakProfit = 0.0;
    entryPrice = 0.0;
    ObjectDelete(0, "TrailingStopLine");
    //stopLoss = 0.0;
}

// Detects market structure over the last 200 bars by finding the two most
// recent confirmed swing highs and swing lows (pivot points).
// Returns:  1 = uptrend   (HH + HL — buy context)
//          -1 = downtrend (LH + LL — sell context)
//           0 = unclear   (no trade)
int detectMarketStructure()
{
   double sh1 = -1, sh2 = -1; // swing highs: sh1 = more recent, sh2 = older
   double sl1 = -1, sl2 = -1; // swing lows:  sl1 = more recent, sl2 = older

   // Scan from most recent completed bar outward — skip live bar 0 and the
   // outer SwingStrength bars where we can't confirm a pivot on both sides.
   for (int i = SwingStrength + 1; i <= 200 - SwingStrength; i++)
   {
      // Swing high check — bar i must be the highest in [i-SwingStrength, i+SwingStrength]
      if (sh2 < 0)
      {
         double hi = iHigh(_Symbol, PERIOD_CURRENT, i);
         bool isPivotHigh = true;
         for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
         {
            if (j == i) continue;
            if (iHigh(_Symbol, PERIOD_CURRENT, j) >= hi) { isPivotHigh = false; break; }
         }
         if (isPivotHigh)
         {
            if (sh1 < 0) sh1 = hi;
            else         sh2 = hi;
         }
      }

      // Swing low check — bar i must be the lowest in [i-SwingStrength, i+SwingStrength]
      if (sl2 < 0)
      {
         double lo = iLow(_Symbol, PERIOD_CURRENT, i);
         bool isPivotLow = true;
         for (int j = i - SwingStrength; j <= i + SwingStrength; j++)
         {
            if (j == i) continue;
            if (iLow(_Symbol, PERIOD_CURRENT, j) <= lo) { isPivotLow = false; break; }
         }
         if (isPivotLow)
         {
            if (sl1 < 0) sl1 = lo;
            else         sl2 = lo;
         }
      }

      if (sh2 >= 0 && sl2 >= 0) break; // collected all 4 pivot points — done
   }

   if (sh1 < 0 || sh2 < 0 || sl1 < 0 || sl2 < 0) return 0; // not enough pivots found

   // sh1/sl1 = more recent (lower bar index = closer to now)
   // sh2/sl2 = older
   bool hhDetected = sh1 > sh2; // recent high above older high
   bool hlDetected = sl1 > sl2; // recent low above older low
   bool lhDetected = sh1 < sh2; // recent high below older high
   bool llDetected = sl1 < sl2; // recent low below older low

   if (hhDetected && hlDetected) return  1; // uptrend
   if (lhDetected && llDetected) return -1; // downtrend
   return 0; // mixed — unclear
}

// Calculates synthetic DXY at a given M1 bar shift using the 6 weighted pairs.
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

// Calculates synthetic DXY from the 6 weighted currency pairs.
// Uses D1 bars: bar[0] = today live, bar[1] = yesterday's close.
double getSyntheticDXY()
{
   return getSyntheticDXYAtBar(0);
}

double getSyntheticDXYYesterdayClose()
{
   return 50.14348112
      * MathPow(iClose("EURUSD", PERIOD_D1, 1), -0.576)
      * MathPow(iClose("USDJPY", PERIOD_D1, 1),  0.136)
      * MathPow(iClose("GBPUSD", PERIOD_D1, 1), -0.119)
      * MathPow(iClose("USDCAD", PERIOD_D1, 1),  0.091)
      * MathPow(iClose("USDSEK", PERIOD_D1, 1),  0.042)
      * MathPow(iClose("USDCHF", PERIOD_D1, 1),  0.036);
}

void DrawDXYLabel(bool isBuy, double dxyChangePercent, bool dxyDirectionUp)
{
   string name = "DXYLabel_" + IntegerToString(TimeCurrent());
   string arrow = dxyDirectionUp ? " ↑" : " ↓";
   string text = StringFormat("DXY: %+.2f%%%s", dxyChangePercent, arrow);

   double price = isBuy
      ? iLow(_Symbol,  PERIOD_CURRENT, 0) - 80 * _Point
      : iHigh(_Symbol, PERIOD_CURRENT, 0) + 80 * _Point;

   ObjectCreate(0, name, OBJ_TEXT, 0, TimeCurrent(), price);
   ObjectSetString(0,  name, OBJPROP_TEXT,      text);
   ObjectSetInteger(0, name, OBJPROP_COLOR,     isBuy ? clrLime : clrRed);
   ObjectSetInteger(0, name, OBJPROP_FONTSIZE,  8);
   ObjectSetString(0,  name, OBJPROP_FONT,      "Arial Bold");
   ObjectSetInteger(0, name, OBJPROP_ANCHOR,    isBuy ? ANCHOR_TOP : ANCHOR_BOTTOM);
}

void trade() {
    int bullishCandleHeight = 0;
    int bearishCandleHeight = 0;

    // Previous candle before current
    int bearishCount = 0;
    int bullishCount = 0;

    double high3 = iHigh(_Symbol, PERIOD_CURRENT, 3);
    double low3 = iLow(_Symbol, PERIOD_CURRENT, 3);
    double open3 = iOpen(_Symbol, PERIOD_CURRENT, 3);
    double close3 = iClose(_Symbol, PERIOD_CURRENT, 3);

    double high2 = iHigh(_Symbol, PERIOD_CURRENT, 2);
    double low2 = iLow(_Symbol, PERIOD_CURRENT, 2);
    double open2 = iOpen(_Symbol, PERIOD_CURRENT, 2);
    double close2 = iClose(_Symbol, PERIOD_CURRENT, 2);

    double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
    double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    double high0 = iHigh(_Symbol, PERIOD_CURRENT, 0);
    double low0 = iLow(_Symbol, PERIOD_CURRENT, 0);
    double open0 = iOpen(_Symbol, PERIOD_CURRENT, 0);
    double close0 = iClose(_Symbol, PERIOD_CURRENT, 0);

    if (open3 < close3) {
        bullishCount++;
    }
    else if (open3 > close3) {
        bearishCount++;
    }

    if (open2 < close2) {
        bullishCount++;
    }
    else if (open2 > close2) {
        bearishCount++;
    }

    if (open1 < close1) {
        bullishCount++;
    }
    else if (open1 > close1) {
        bearishCount++;
    }



    // Exponential moving average 9
    double exponentialMovingAverage9[];
    int exponentialMovingAverage9Def = iMA(_Symbol, _Period, 9, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage9, true);
    CopyBuffer(exponentialMovingAverage9Def, 0, 0, 3, exponentialMovingAverage9);

    // Exponential moving average 20
    double exponentialMovingAverage20[];
    int exponentialMovingAverage20Def = iMA(_Symbol, _Period, 20, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage20, true);
    CopyBuffer(exponentialMovingAverage20Def, 0, 0, 3, exponentialMovingAverage20);

    // Exponential moving average 50
    double exponentialMovingAverage50[];
    int exponentialMovingAverage50Def = iMA(_Symbol, _Period, 50, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage50, true);
    CopyBuffer(exponentialMovingAverage50Def, 0, 0, 3, exponentialMovingAverage50);

    // Exponential moving average 200
    double exponentialMovingAverage200[];
    int exponentialMovingAverage200Def = iMA(_Symbol, _Period, 200, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage200, true);
    CopyBuffer(exponentialMovingAverage200Def, 0, 0, 3, exponentialMovingAverage200);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Market structure — scan 200 bars for HH+HL (uptrend) or LH+LL (downtrend)
    int marketStructure = detectMarketStructure();
    bool uptrendConfirmed   = (marketStructure ==  1); // HH + HL → buy context
    bool downtrendConfirmed = (marketStructure == -1); // LH + LL → sell context

    // DXY 7-candle direction filter
    // Compare synthetic DXY now vs 7 M1 candles ago to detect genuine dollar direction.
    double currentDXY  = getSyntheticDXYAtBar(0);
    double dxy7ago     = getSyntheticDXYAtBar(30);
    double dxy7change  = currentDXY - dxy7ago;
    bool   dxyFalling  = dxy7change < -0.01; // dollar weakening → gold supported → buy
    bool   dxyRising   = dxy7change >  0.01; // dollar strengthening → gold pressured → sell
    // flat (abs change < 0.01): direction unclear → neither flag set → no trade

    // Keep daily change % for the chart label display only
    double yesterdayDXY   = getSyntheticDXYYesterdayClose();
    double dxyDailyChange = ((currentDXY - yesterdayDXY) / yesterdayDXY) * 100.0;

    int posCount = CountSymbolPositions();

    // A new entry is allowed when:
    //   - no positions are open yet, OR
    //   - trailing has already activated on the combined profit (peakProfit >= abs(stopLoss))
    //     AND we haven't reached the MaxEntries cap.
    // This ensures each additional entry is only opened while the existing portfolio
    // is already protected by the trailing floor.
    bool trailingActive = (peakProfit >= MathAbs(stopLoss));
    bool canAddEntry    = (posCount == 0) || (posCount < MaxEntries && trailingActive);

    // Buy logic — uptrend structure (HH+HL), DXY falling over 7 candles,
    // drop+consolidation confirmed, live candle touches zone
    if (uptrendConfirmed) {
        if (isLowConsolidating()) {
            if (dxyFalling) {
                if (canAddEntry && (!selling)) {
                    Buy();
                    DrawBuySignal();
                    DrawDXYLabel(true, dxyDailyChange, dxyRising);
                }
            }
        }
    }

    // Sell logic — downtrend structure (LH+LL), DXY rising over 7 candles,
    // rise+consolidation confirmed, live candle touches zone
    if (downtrendConfirmed) {
        if (isHighConsolidatingEntry()) {
            if (dxyRising) {
                if (canAddEntry && (!buying)) {
                    Sell();
                    DrawSellSignal();
                    DrawDXYLabel(false, dxyDailyChange, dxyRising);
                }
            }
        }
    }
}

bool shouldContinueTrading() {
    return tradeCount >= maximumNumOfFailedTrades;
}


void OnTick() {
    if (!shouldContinueTrading()) {
        trade();

        double accountBalance = AccountInfoDouble(ACCOUNT_BALANCE);
        double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);
        double accountEquity = AccountInfoDouble(ACCOUNT_EQUITY);

        double exponentialMovingAverage9[];
        int exponentialMovingAverage9Def = iMA(_Symbol, _Period, 9, 0, MODE_EMA, PRICE_CLOSE);
        ArraySetAsSeries(exponentialMovingAverage9, true);
        CopyBuffer(exponentialMovingAverage9Def, 0, 0, 3, exponentialMovingAverage9);

        // Exponential moving average 30
        double exponentialMovingAverage30[];
        int exponentialMovingAverage30Def = iMA(_Symbol, _Period, 25, 0, MODE_EMA, PRICE_CLOSE);
        ArraySetAsSeries(exponentialMovingAverage30, true);
        CopyBuffer(exponentialMovingAverage30Def, 0, 0, 3, exponentialMovingAverage30);

        double high0 = iHigh(_Symbol, PERIOD_CURRENT, 0);
        double low0 = iLow(_Symbol, PERIOD_CURRENT, 0);


        //double diff = accountEquity - accountBalance; // calculate profit or loss

        // diff greater than average profit set
        if (accountProfit > averageProfit) {
            hasReachedAverageProfit = true;
        }

        // Profit falls below average profit and minimum profit set, exit
        if ((accountProfit < minimumProfit) && (hasReachedAverageProfit)) {
            Print("TRADE_EXIT | reason=AVERAGE_PROFIT_PULLBACK | profit=", accountProfit,
                  " | peakProfit=", peakProfit,
                  " | entryPrice=", entryPrice,
                  " | exitPrice=", SymbolInfoDouble(_Symbol, buying ? SYMBOL_BID : SYMBOL_ASK));
            CloseAll();
        }

        // Take profit
        //if ((buying) && ((exponentialMovingAverage9[0] < exponentialMovingAverage30[0]) && (exponentialMovingAverage9[1] > exponentialMovingAverage30[1]))) {
           // CloseAll();
       // }

        //if ((selling) && ((exponentialMovingAverage9[0] > exponentialMovingAverage30[0]) && (exponentialMovingAverage9[1] < exponentialMovingAverage30[1]))) {
           // CloseAll();
        //}

        if (accountProfit >= takeProfit) {
            Print("TRADE_EXIT | reason=TAKE_PROFIT | profit=", accountProfit,
                  " | peakProfit=", peakProfit,
                  " | entryPrice=", entryPrice,
                  " | exitPrice=", SymbolInfoDouble(_Symbol, buying ? SYMBOL_BID : SYMBOL_ASK));
            tradeCount = 0;
            CloseAll();
        }


        // Stop loss — close all when combined profit drops to or below stopLoss
        if ((CountSymbolPositions() > 0) && (accountProfit <= stopLoss)) {
            Print("TRADE_EXIT | reason=STOP_LOSS | profit=", accountProfit,
                  " | peakProfit=", peakProfit,
                  " | entryPrice=", entryPrice,
                  " | exitPrice=", SymbolInfoDouble(_Symbol, buying ? SYMBOL_BID : SYMBOL_ASK));
            tradeCount++;
            CloseAll();
        }

        // Trailing stop — activates once combined profit reaches abs(stopLoss)
        if (CountSymbolPositions() > 0) {
            if (accountProfit > peakProfit)
                peakProfit = accountProfit;

            double trailTrigger = MathAbs(stopLoss);
            double contractSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_CONTRACT_SIZE);

            if (peakProfit >= trailTrigger) {
                double trailStop = peakProfit - trailTrigger;

                // Buy trail line moves UP, sell trail line moves DOWN
                double trailStopPrice = buying
                    ? entryPrice + trailStop / (Money_FixLot_Lots * contractSize)
                    : entryPrice - trailStop / (Money_FixLot_Lots * contractSize);

                DrawTrailingStopLine(trailStopPrice);

                if (accountProfit <= trailStop) {
                    Print("TRADE_EXIT | reason=TRAILING_STOP | profit=", accountProfit,
                          " | peakProfit=", peakProfit,
                          " | trailStop=", trailStop,
                          " | entryPrice=", entryPrice,
                          " | exitPrice=", SymbolInfoDouble(_Symbol, buying ? SYMBOL_BID : SYMBOL_ASK));
                    tradeCount = 0;
                    CloseAll();
                }
            } else {
                // Before trailing activates, show the fixed stop loss level
                double fixedStopPrice = buying
                    ? entryPrice + stopLoss / (Money_FixLot_Lots * contractSize)
                    : entryPrice - stopLoss / (Money_FixLot_Lots * contractSize);

                DrawTrailingStopLine(fixedStopPrice);
            }
        }

    }
}