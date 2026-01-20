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
    //stopLoss = 0.0;
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

    if (open13 < close13) {
        bullishCount++;
    }
    else if (open13 > close13) {
        bearishCount++;
    }

    if ((open12 < close12) && (close13 < open12)) {
        bullishCount++;
    }
    else if ((open12 > close12) && (close13 > open12)) {
        bearishCount++;
    }

    if ((open11 < close11) && (close12 < open11)) {
        bullishCount++;
    }
    else if ((open11 > close11) && (close12 > open11)) {
        bearishCount++;
    }

    if ((open10 < close10) && (close11 < open10)) {
        bullishCount++;
    }
    else if ((open10 > close10) && (close11 > open10)) {
        bearishCount++;
    }

    if ((open9 < close9) && (close10 < open9)) {
        bullishCount++;
    }
    else if ((open9 > close9) && (close10 > open9)) {
        bearishCount++;
    }

    if ((open8 < close8) && (close9 < open8)) {
        bullishCount++;
    }
    else if ((open8 > close8) && (close9 > open8)) {
        bearishCount++;
    }

    if ((open7 < close7) && (close8 < open7)) {
        bullishCount++;
    }
    else if ((open7 > close7) && (close8 > open7)) {
        bearishCount++;
    }

    if ((open6 < close6) && (close7 < open6)) {
        bullishCount++;
    }
    else if ((open6 > close6) && (close7 > open6)) {
        bearishCount++;
    }

    if ((open5 < close5) && (close6 < open5)) {
        bullishCount++;
    }
    else if ((open5 > close5) && (close6 > open5)) {
        bearishCount++;
    }

    if ((open4 < close4) && (close5 < open4)) {
        bullishCount++;
    }
    else if ((open4 > close4) && (close5 > open4)) {
        bearishCount++;
    }

    if ((open3 < close3) && (close4 < open5)) {
        bullishCount++;
    }
    else if ((open3 > close3) && (close4 > open3)) {
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
    if ((bullishCount >= candleCount) && (bullishCount > bearishCount)) {
        double buyLength = high1 - low1;
        double sellLength = high2 - low2;
        if ((!PositionSelect(_Symbol)) && (!buying) && (open2 > close2) && (open1 < close1) && (close1 > open2) && (open1 < close2)) {
            Buy();
            buying = true;
        }
    }

    // Sell logic
    if ((bearishCount >= candleCount) && (bearishCount > bullishCount)) {
        double buyLength = high2 - low2;
        double sellLength = high1 - low1;
        if ((!PositionSelect(_Symbol)) && (!selling) && (open2 < close2) && (open1 > close1) && (close1 < open2) && (open1 > close2)) {
            Sell();
            selling = true;
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
            if (((open1 < close1) || (open1 > close1)) && (profit > 1)) {
                counter++;
            }
        }

        if (selling) {
            if (((open1 > close1) || (open1 < close1)) && (profit > 1)) {
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


        if (accountProfit >= takeProfit) {
            tradeCount = 0;
            CloseAll();
        }

        // Stop loss
        //if ((buying) && (low0 < stopLoss)) {
            //tradeCount++;
            //CloseAll();
        //}

        if (accountProfit <= stopLoss) {
            tradeCount++;
            CloseAll();
        }

        //if ((selling) && (high0 > stopLoss)) {
            //tradeCount++;
           // CloseAll();
       // }

        calculateInvertedCandles(accountProfit);

    }
}
