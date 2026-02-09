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
double stopLoss = 0.0;

// Set take profit. This is can be changed from the UI
input double takeProfit = 50.0;

// Average profit
input double averageProfit = 10.0;

// Has hit average profit
bool hasReachedAverageProfit = false;

// Maximum allowed candle moving in opposite direction 
input int invertedCandleCount = 2;

// Maximum number of failed trade before final exit (Stop trading)
input int maximumNumOfFailedTrades = 3;

// Exit with minimum profit
input int minimumProfit = 1;

// The offset range
input double offset = 0.01;

input int candleCount = 3;

// exit point
double exitPoint = 0.0;

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
    CloseAllOrders();
    selling = false;
    buying = false;
    counter = 0;
    previousCandleOpen = 0.0;
    previousCandleClose = 0.0;
    hasbullishCrossing = false;
    hasBearishCrossing = false;
    hasReachedAverageProfit = false;
    stopLoss = 0.0;
}

void trade() {
    int bullishCandleHeight = 0;
    int bearishCandleHeight = 0;

    // Previous candle before current
    int bearishCount = 0;
    int bullishCount = 0;

    double high13 = iHigh(_Symbol, PERIOD_CURRENT, 13);
    double low13 = iLow(_Symbol, PERIOD_CURRENT, 13);
    double open13 = iOpen(_Symbol, PERIOD_CURRENT, 13);
    double close13 = iClose(_Symbol, PERIOD_CURRENT, 13);

    double high12 = iHigh(_Symbol, PERIOD_CURRENT, 12);
    double low12 = iLow(_Symbol, PERIOD_CURRENT, 12);
    double open12 = iOpen(_Symbol, PERIOD_CURRENT, 12);
    double close12 = iClose(_Symbol, PERIOD_CURRENT, 12);

    double high11 = iHigh(_Symbol, PERIOD_CURRENT, 11);
    double low11 = iLow(_Symbol, PERIOD_CURRENT, 11);
    double open11 = iOpen(_Symbol, PERIOD_CURRENT, 11);
    double close11 = iClose(_Symbol, PERIOD_CURRENT, 11);

    double high10 = iHigh(_Symbol, PERIOD_CURRENT, 10);
    double low10 = iLow(_Symbol, PERIOD_CURRENT, 10);
    double open10 = iOpen(_Symbol, PERIOD_CURRENT, 10);
    double close10 = iClose(_Symbol, PERIOD_CURRENT, 10);

    double high9 = iHigh(_Symbol, PERIOD_CURRENT, 9);
    double low9 = iLow(_Symbol, PERIOD_CURRENT, 9);
    double open9 = iOpen(_Symbol, PERIOD_CURRENT, 9);
    double close9 = iClose(_Symbol, PERIOD_CURRENT, 9);

    double high8 = iHigh(_Symbol, PERIOD_CURRENT, 8);
    double low8 = iLow(_Symbol, PERIOD_CURRENT, 8);
    double open8 = iOpen(_Symbol, PERIOD_CURRENT, 8);
    double close8 = iClose(_Symbol, PERIOD_CURRENT, 8);

    double high7 = iHigh(_Symbol, PERIOD_CURRENT, 7);
    double low7 = iLow(_Symbol, PERIOD_CURRENT, 7);
    double open7 = iOpen(_Symbol, PERIOD_CURRENT, 7);
    double close7 = iClose(_Symbol, PERIOD_CURRENT, 7);

    double high6 = iHigh(_Symbol, PERIOD_CURRENT, 6);
    double low6 = iLow(_Symbol, PERIOD_CURRENT, 6);
    double open6 = iOpen(_Symbol, PERIOD_CURRENT, 6);
    double close6 = iClose(_Symbol, PERIOD_CURRENT, 6);

    double high5 = iHigh(_Symbol, PERIOD_CURRENT, 5);
    double low5 = iLow(_Symbol, PERIOD_CURRENT, 5);
    double open5 = iOpen(_Symbol, PERIOD_CURRENT, 5);
    double close5 = iClose(_Symbol, PERIOD_CURRENT, 5);

    double high4 = iHigh(_Symbol, PERIOD_CURRENT, 4);
    double low4 = iLow(_Symbol, PERIOD_CURRENT, 4);
    double open4 = iOpen(_Symbol, PERIOD_CURRENT, 4);
    double close4 = iClose(_Symbol, PERIOD_CURRENT, 4);

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

    if ((low13 < low1) && (low13 < low2)) {
        bullishCount++;
    }
    else if ((high13 > high1) && (high13 > high2)) {
        bearishCount++;
    }

    if ((low12 < low1) && (low12 < low2)) {
        bullishCount++;
    }
    else if ((high12 > high1) && (high12 > high2)) {
        bearishCount++;
    }

    if ((low11 < low1) && (low11 < low2)) {
        bullishCount++;
    }
    else if ((high11 > high1) && (high11 > high2)) {
        bearishCount++;
    }

    if ((low10 < low1) && (low10 < low2)) {
        bullishCount++;
    }
    else if ((high10 > high1) && (high10 > high2)) {
        bearishCount++;
    }

    if ((low9 < low1) && (low9 < low2)) {
        bullishCount++;
    }
    else if ((high9 > high1) && (high9 > high2)) {
        bearishCount++;
    }

    if ((low8 < low1) && (low8 < low2)) {
        bullishCount++;
    }
    else if ((high8 > high1) && (high8 > high2)) {
        bearishCount++;
    }

    if ((low7 < low1) && (low7 < low2)) {
        bullishCount++;
    }
    else if ((high7 > high1) && (high7 > high2)) {
        bearishCount++;
    }

    if ((low6 < low1) && (low6 < low2)) {
        bullishCount++;
    }
    else if ((high6 > high1) && (high6 > high2)) {
        bearishCount++;
    }

    if ((low5 < low1) && (low5 < low2)) {
        bullishCount++;
    }
    else if ((high5 > high1) && (high5 > high2)) {
        bearishCount++;
    }

    if ((low4 < low1) && (low4 < low2)) {
        bullishCount++;
    }
    else if ((high4 > high1) && (high4 > high2)) {
        bearishCount++;
    }

    if ((low3 < low1) && (low3 < low2)) {
        bullishCount++;
    }
    else if ((high3 > high1) && (high3 > high2)) {
        bearishCount++;
    }



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
    
    // Exponential moving average 200
    double exponentialMovingAverage200[];
    int exponentialMovingAverage200Def = iMA(_Symbol, _Period, 200, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage200, true);
    CopyBuffer(exponentialMovingAverage200Def, 0, 0, 3, exponentialMovingAverage200);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Buy logic
    if ((exponentialMovingAverage200[0] < low0) && (bullishCount >= candleCount) && (bullishCount > bearishCount) && (open1 != exitPoint) && ((low1 < exponentialMovingAverage50[0] && high1 > exponentialMovingAverage50[0]) || 
    (low2 < exponentialMovingAverage50[0] && high2 > exponentialMovingAverage50[0]) || 
    (low3 < exponentialMovingAverage50[0] && high3 > exponentialMovingAverage50[0]) || 
    (low4 < exponentialMovingAverage50[0] && high4 > exponentialMovingAverage50[0]))) {
        double buyLength = high1 - low1;
        double sellLength = high2 - low2;
        if ((!PositionSelect(_Symbol)) && (!buying) && (open2 > close2) && (open1 < close1) && ((open3 < close3) || (open4 < close4))) {
            stopLoss = low13;
            if (low12 < stopLoss) {
                stopLoss = low12;
            }
            if (low11 < stopLoss) {
                stopLoss = low11;
            }
            if (low10 < stopLoss) {
                stopLoss = low10;
            }
            if (low9 < stopLoss) {
                stopLoss = low9;
            }
            if (low8 < stopLoss) {
                stopLoss = low8;
            }
            if (low7 < stopLoss) {
                stopLoss = low7;
            }
            if (low6 < stopLoss) {
                stopLoss = low6;
            }
            if (low5 < stopLoss) {
                stopLoss = low5;
            }
            if (low4 < stopLoss) {
                stopLoss = low4;
            }
            if (low3 < stopLoss) {
                stopLoss = low3;
            }
            if ((stopLoss < low1) && (stopLoss < low2)) {
                Buy();
                buying = true;
            }
        }
    }

    // Sell logic
    if ((exponentialMovingAverage200[0] > low0) && (bearishCount >= candleCount) && (bearishCount > bullishCount) && (open1 != exitPoint) && ((high1 > exponentialMovingAverage50[0] && low1 < exponentialMovingAverage50[0]) || 
    (high2 > exponentialMovingAverage50[0] && low2 < exponentialMovingAverage50[0]) || 
    (high3 > exponentialMovingAverage50[0] && low3 < exponentialMovingAverage50[0]) || 
    (high4 > exponentialMovingAverage50[0] && low4 < exponentialMovingAverage50[0]))) {
        double buyLength = high2 - low2;
        double sellLength = high1 - low1;
        if ((!PositionSelect(_Symbol)) && (!selling) && (open2 < close2) && (open1 > close1) && ((open3 > close3) || (open4 > close4))) {
            stopLoss = high13;
            if (high12 > stopLoss) {
                stopLoss = high12;
            }
            if (high11 > stopLoss) {
                stopLoss = high11;
            }
            if (high10 > stopLoss) {
                stopLoss = high10;
            }
            if (high9 > stopLoss) {
                stopLoss = high9;
            }
            if (high8 > stopLoss) {
                stopLoss = high8;
            }
            if (high7 > stopLoss) {
                stopLoss = high7;
            }
            if (high6 > stopLoss) {
                stopLoss = high6;
            }
            if (high5 > stopLoss) {
                stopLoss = high5;
            }
            if (high4 > stopLoss) {
                stopLoss = high4;
            }
            if (high3 > stopLoss) {
                stopLoss = high3;
            }
            if ((stopLoss > high1) && (stopLoss > high2)) {
                Sell();
                selling = true;
            }
        }
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
            if ((open1 < close1) && (profit > 1)) {
                counter++;
            }
        }

        if (selling) {
            if ((open1 > close1) && (profit > 1)) {
                counter++;
            }
        }
        previousCandleOpen = open1;
        previousCandleClose = close1;
    }
    // Close trade if there 3 consecutive inverted candles
    if ((counter >= invertedCandleCount) && (profit > 1)) {
        exitPoint = open1;
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
        double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
        double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
        double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
        double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);


        //double diff = accountEquity - accountBalance; // calculate profit or loss

        // diff greater than average profit set
        if (accountProfit > averageProfit) {
            hasReachedAverageProfit = true;
        }

        // Profit falls below average profit and minimum profit set, exit
        if ((accountProfit < minimumProfit) && (hasReachedAverageProfit)) {
            exitPoint = open1;
            CloseAll();
        }


        if (accountProfit >= takeProfit) {
            tradeCount = 0;
            exitPoint = open1;
            CloseAll();
        }

        // Stop loss
        if ((buying) && (low0 < stopLoss)) {
            tradeCount++;
            exitPoint = open1;
            CloseAll();
        }

        //if (accountProfit <= stopLoss) {
           // tradeCount++;
           // exitPoint = open1;
           // CloseAll();
        //}

        if ((selling) && (high0 > stopLoss)) {
            tradeCount++;
            exitPoint = open1;
            CloseAll();
        }

        calculateInvertedCandles(accountProfit);

    }
}
