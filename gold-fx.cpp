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

// Set stop loss. This is can be changed from the UI
input double stopLoss = -20.0;

// Set take profit. This is can be changed from the UI
input double takeProfit = 70.0;

// Average profit
input double averageProfit = 20.0;

// Has hit average profit
bool hasReachedAverageProfit = false;

// Maximum allowed candle moving in opposite direction 
input int invertedCandleCount = 2;

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

static int counter = 0;
static double previousCandleOpen = 0.0;
static double previousCandleClose = 0.0;
static int tradeCount = 0;

void DrawBuySignal()
{
   string name = "BuySignal_" + IntegerToString(TimeCurrent());
   double arrowPrice = iLow(_Symbol, PERIOD_CURRENT, 0) - 50 * _Point;

   ObjectCreate(0, name, OBJ_ARROW_UP, 0, TimeCurrent(), arrowPrice);
   ObjectSetInteger(0, name, OBJPROP_COLOR, clrLime);
   ObjectSetInteger(0, name, OBJPROP_WIDTH, 2);
   ObjectSetInteger(0, name, OBJPROP_ARROWCODE, 241);
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

    if (OrderSend(request, result) && result.retcode == TRADE_RETCODE_DONE)
        buying = true;
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

    OrderSend(request, result);
}

// Returns the consolidation base low if at least MinimumConsolidationBars
// of the last RangeBars completed candles have lows within MaxLowRangePoints.
// Returns -1 if consolidation is not confirmed.
// Pass shift=1 to examine only completed candles (exclude the live bar 0).
double getConsolidationLow(int shift)
{
   int lowestLowIndex =
      iLowest(_Symbol, PERIOD_CURRENT, MODE_LOW, RangeBars, shift);
   double lowestLow =
      iLow(_Symbol, PERIOD_CURRENT, lowestLowIndex);

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
    counter = 0;
    previousCandleOpen = 0.0;
    previousCandleClose = 0.0;
    hasbullishCrossing = false;
    hasBearishCrossing = false;
    hasReachedAverageProfit = false;
    //stopLoss = 0.0;
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
    // Buy logic
    //if ((exponentialMovingAverage200[0] < low0)) {
                if (isLowConsolidating()) {
                if ((!PositionSelect(_Symbol)) && (!buying)) {
                    Buy();
                    DrawBuySignal();
                    //stopLoss = low3;
                }
            }
   // }

    // Sell logic
    if ((exponentialMovingAverage200[0] > low0) && (bearishCount == candleCount)  && ((high1 > exponentialMovingAverage50[0] && low1 < exponentialMovingAverage50[0]) || 
    (high2 > exponentialMovingAverage50[0] && low2 < exponentialMovingAverage50[0]) || 
    (high3 > exponentialMovingAverage50[0] && low3 < exponentialMovingAverage50[0]))) {
        //if ((open2 < close2) && (open1 > close1)) {
           // double buyLength = high1 - low1;
           // double sellLength = high2 - low2;
           // double distanceFrom200 = exponentialMovingAverage200[0] - high0;
            //if (sellLength > buyLength) {
                if ((!PositionSelect(_Symbol)) && (!selling) && (stopLoss != high3) && (close3 > close2) && (close2 > close1)) {
               // Sell();
               // selling = true;
                //stopLoss = high3;
            }
           // }
        //}
    }
}

bool shouldContinueTrading() {
    return tradeCount >= maximumNumOfFailedTrades;
}

void calculateInvertedCandles(double profit) {
    double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
    double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    if ((open1 != previousCandleOpen) || (close1 != previousCandleClose)) {
        if (buying) {
            // Count bearish candles — they move against a buy
            if ((open1 > close1) && (profit > 0)) {
                counter++;
            }
        }

        if (selling) {
            // Count bullish candles — they move against a sell
            if ((open1 < close1) && (profit > 0)) {
                counter++;
            }
        }
        previousCandleOpen = open1;
        previousCandleClose = close1;
    }
    // Close trade if there 3 consecutive inverted candles
    if ((counter >= invertedCandleCount) && (profit > 1)) {
        CloseAll();
    }
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
            tradeCount = 0;
            CloseAll();
        }

        // Stop loss — close when account profit drops to or below the stopLoss value (e.g. -20)
        if ((buying || selling) && (accountProfit <= stopLoss)) {
            tradeCount++;
            CloseAll();
        }

        calculateInvertedCandles(accountProfit);

    }
}