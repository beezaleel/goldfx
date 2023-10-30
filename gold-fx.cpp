#include <Trade\Trade.mqh>

MqlTradeRequest request;
MqlTradeResult result;

input double Money_FixLot_Lots = 0.08;
int magicNumber = 109814;
static bool buying = false;
static bool selling = false;
static bool hasbullishCrossing = false;
static bool hasBearishCrossing = true;
input double AVERAGE_CANDLE_HEIGHT = 0.70;

// Set stop loss. This is can be changed from the UI
input double stopLoss = -10.0;

// Set take profit. This is can be changed from the UI
input double takeProfit = 50.0;

// Average profit
input double averageProfit = 20.0;

// Has hit average profit
bool hasReachedAverageProfit = false;

// Maximum allowed candle moving in opposite direction 
input int invertedCandleCount = 1;

// Maximum number of failed trade before final exit (Stop trading)
input int maximumNumOfFailedTrades = 5;

// Exit with minimum profit
input int minimumProfit = 10;

static int counter = 0;
static double previousCandleOpen = 0.0;
static double previousCandleClose = 0.0;
static int tradeCount = 0;

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

    OrderSend(request, result);
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

void CloseAllOrders() {
    CTrade trade;

    int i = PositionsTotal() - 1;

    while (i >= 0) {
        if (trade.PositionClose(PositionGetSymbol(i)))
            i--;
    }
}

void CloseAll() {
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    CloseAllOrders();
    selling = false;
    buying = false;
    counter = 0;
    previousCandleOpen = NormalizeDouble(open1, 4);
    previousCandleClose = NormalizeDouble(close1, 4);
    hasbullishCrossing = false;
    hasBearishCrossing = false;
    hasReachedAverageProfit = false;
}

void trade() {
    int bullishCandleHeight = 0;
    int bearishCandleHeight = 0;

    // The current candle
    double high0 = iHigh(_Symbol, PERIOD_CURRENT, 0);
    double low0 = iLow(_Symbol, PERIOD_CURRENT, 0);
    double open0 = iOpen(_Symbol, PERIOD_CURRENT, 0);
    double close0 = iClose(_Symbol, PERIOD_CURRENT, 0);

    // Previous candle before current
    int bearishCount = 0;
    int bullishCount = 0;

    double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
    double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    double high2 = iHigh(_Symbol, PERIOD_CURRENT, 2);
    double low2 = iLow(_Symbol, PERIOD_CURRENT, 2);
    double open2 = iOpen(_Symbol, PERIOD_CURRENT, 2);
    double close2 = iClose(_Symbol, PERIOD_CURRENT, 2);

    double high3 = iHigh(_Symbol, PERIOD_CURRENT, 3);
    double low3 = iLow(_Symbol, PERIOD_CURRENT, 3);
    double open3 = iOpen(_Symbol, PERIOD_CURRENT, 3);
    double close3 = iClose(_Symbol, PERIOD_CURRENT, 3);

    double high4 = iHigh(_Symbol, PERIOD_CURRENT, 4);
    double low4 = iLow(_Symbol, PERIOD_CURRENT, 4);
    double open4 = iOpen(_Symbol, PERIOD_CURRENT, 4);
    double close4 = iClose(_Symbol, PERIOD_CURRENT, 4);

    double high5 = iHigh(_Symbol, PERIOD_CURRENT, 5);
    double low5 = iLow(_Symbol, PERIOD_CURRENT, 5);
    double open5 = iOpen(_Symbol, PERIOD_CURRENT, 5);
    double close5 = iClose(_Symbol, PERIOD_CURRENT, 5);

    double high6 = iHigh(_Symbol, PERIOD_CURRENT, 6);
    double low6 = iLow(_Symbol, PERIOD_CURRENT, 6);
    double open6 = iOpen(_Symbol, PERIOD_CURRENT, 6);
    double close6 = iClose(_Symbol, PERIOD_CURRENT, 6);

    double high7 = iHigh(_Symbol, PERIOD_CURRENT, 7);
    double low7 = iLow(_Symbol, PERIOD_CURRENT, 7);
    double open7 = iOpen(_Symbol, PERIOD_CURRENT, 7);
    double close7 = iClose(_Symbol, PERIOD_CURRENT, 7);


    if ((open1 > close1)) {
        double lengthCandle1 = MathAbs(high1 - low1);
        double lengthCandle2 = MathAbs(high2 - low2);
        if ((lengthCandle1 > lengthCandle2) && (open2 < close2)) {
            bearishCount++;
        } else if (open2 > close2) {
            bearishCount++;
        }
    }
    else if (open1 < close1) {
        double lengthCandle1 = MathAbs(high1 - low1);
        double lengthCandle2 = MathAbs(high2 - low2);
        if ((lengthCandle1 > lengthCandle2) && (open2 > close2)) {
            bullishCount++;
        } else if (open2 < close2) {
            bullishCount++;
        }
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

    if ((bullishCount == 1) && 
    (exponentialMovingAverage200[0] < low0) && 
    (exponentialMovingAverage200[0] < exponentialMovingAverage50[0]) && 
    (exponentialMovingAverage200[0] < exponentialMovingAverage20[0]) &&
    (exponentialMovingAverage200[0] < exponentialMovingAverage9[0]) &&
    (exponentialMovingAverage50[0] < exponentialMovingAverage20[0]) &&
    (exponentialMovingAverage50[0] < exponentialMovingAverage9[0]) &&
    (open1 < exponentialMovingAverage50[0] && close1 > exponentialMovingAverage50[0]) &&
    //(exponentialMovingAverage50[0] < exponentialMovingAverage20[0]) &&
    //(exponentialMovingAverage50[0] < exponentialMovingAverage9[0]) &&
    (previousCandleOpen == 0) &&
    (previousCandleClose == 0)) {
        if ((!PositionSelect(_Symbol)) && (!buying)) { // Check if there is no current trade running
            Buy();
            buying = true;
            hasbullishCrossing = false;
        }
    }

    if ((bearishCount == 1) &&
    (exponentialMovingAverage200[0] > high0) && 
    (exponentialMovingAverage200[0] > exponentialMovingAverage50[0]) && 
    (exponentialMovingAverage200[0] > exponentialMovingAverage20[0]) &&
    (exponentialMovingAverage200[0] > exponentialMovingAverage9[0]) &&
    (exponentialMovingAverage50[0] > exponentialMovingAverage20[0]) &&
    (exponentialMovingAverage50[0] > exponentialMovingAverage9[0]) &&
    (open1 > exponentialMovingAverage50[0] && close1 < exponentialMovingAverage50[0]) &&
    //(exponentialMovingAverage50[0] > exponentialMovingAverage20[0]) &&
   // (exponentialMovingAverage50[0] > exponentialMovingAverage9[0]) &&
    (previousCandleOpen == 0) &&
    (previousCandleClose == 0)) {
        if ((!PositionSelect(_Symbol)) && (!selling)) { // Check if there is no current trade running
            Sell();
            selling = true;
            hasBearishCrossing = false;
        }
    }
}

bool shouldContinueTrading() {
    return tradeCount <= maximumNumOfFailedTrades;
}

void calculateInvertedCandles(double profit) {
    double high0 = iHigh(_Symbol, PERIOD_CURRENT, 0);
    double low0 = iLow(_Symbol, PERIOD_CURRENT, 0);
    double open0 = iOpen(_Symbol, PERIOD_CURRENT, 0);
    double close0 = iClose(_Symbol, PERIOD_CURRENT, 0);

    double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
    double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    if ((buying) && (profit > 1)) {
        if (close0 < low1) {
            CloseAll();
        }
    }

    if ((selling) && (profit > 1)) {
        if (close0 > high1) {
            CloseAll();
        }
    }
}

void OnTick() {
    if (shouldContinueTrading()) {
        double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
        double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

        if ((!buying) && (!selling) && (NormalizeDouble(open1, 4) != previousCandleOpen) && (NormalizeDouble(close1, 4) != previousCandleClose)) {
            previousCandleOpen = 0;
            previousCandleClose = 0;
        }

        trade();

        double accountBalance = AccountInfoDouble(ACCOUNT_BALANCE);
        double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);
        double accountEquity = AccountInfoDouble(ACCOUNT_EQUITY);

        //double diff = accountEquity - accountBalance; // calculate profit or loss

            // Exponential moving average 9
        double exponentialMovingAverage9[];
        int exponentialMovingAverage9Def = iMA(_Symbol, _Period, 9, 0, MODE_EMA, PRICE_CLOSE);
        ArraySetAsSeries(exponentialMovingAverage9, true);
        CopyBuffer(exponentialMovingAverage9Def, 0, 0, 3, exponentialMovingAverage9);

        // Exponential moving average 50
        double exponentialMovingAverage50[];
        int exponentialMovingAverage50Def = iMA(_Symbol, _Period, 50, 0, MODE_EMA, PRICE_CLOSE);
        ArraySetAsSeries(exponentialMovingAverage50, true);
        CopyBuffer(exponentialMovingAverage50Def, 0, 0, 3, exponentialMovingAverage50);

        // Close sell trade if there is sign of a buy
        if (selling) {
            if ((exponentialMovingAverage9[0] > exponentialMovingAverage50[0]) &&
                (exponentialMovingAverage9[1] < exponentialMovingAverage50[1])) {
                CloseAll();
            }
        }

        // Close buy trade if there is sign of a sell
        if (buying) {
            if ((exponentialMovingAverage9[0] < exponentialMovingAverage50[0]) &&
                (exponentialMovingAverage9[1] > exponentialMovingAverage50[1])) {
                CloseAll();
            }
        }

        // diff greater than average profit set
        if (accountProfit > averageProfit) {
            hasReachedAverageProfit = true;
        }

        // Profit falls below average profit and minimum profit set, exit
        if ((accountProfit < minimumProfit) && (hasReachedAverageProfit)) {
            CloseAll();
        }

        // Take profit
        if (accountProfit >= takeProfit) {
            tradeCount = 0;
            CloseAll();
        }

        // Stop loss
        if (accountProfit < stopLoss) {
            tradeCount++;
            CloseAll();
       }

        // Calculate number of inverted candles
       // calculateInvertedCandles(accountProfit);
    }
}

