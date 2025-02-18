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
input double averageProfit = 20.0;

// Has hit average profit
bool hasReachedAverageProfit = false;

// Maximum allowed candle moving in opposite direction 
input int invertedCandleCount = 2;

// Maximum number of failed trade before final exit (Stop trading)
input int maximumNumOfFailedTrades = 3;

// Exit with minimum profit
input int minimumProfit = 10;

// The offset range
input double offset = 0.01;

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
    if ((exponentialMovingAverage200[0] < low0) && (exponentialMovingAverage200[0] < exponentialMovingAverage50[0]) && 
    (exponentialMovingAverage50[0] < exponentialMovingAverage9[0])) {
        if ((open2 > close2) && (open1 < close1)) {
            double diff = MathAbs(close2 - open1);
            double buyLength = high1 - low1;
            double sellLength = high2 - low2;
            double distanceFrom200 = high0 - exponentialMovingAverage200[0];
            if (buyLength > sellLength) {
                if ((!PositionSelect(_Symbol)) && (!buying) && (distanceFrom200 > AVERAGE_CANDLE_HEIGHT)) {
                Buy();
                buying = true;
                stopLoss = exponentialMovingAverage200[0];
            }
            }
        }
    }

    // Sell logic
    if ((exponentialMovingAverage200[0] > low0) && (exponentialMovingAverage200[0] > exponentialMovingAverage50[0]) && 
    (exponentialMovingAverage50[0] > exponentialMovingAverage9[0])) {
        if ((open2 < close2) && (open1 > close1)) {
            double diff = MathAbs(close2 - open1);
            double buyLength = high1 - low1;
            double sellLength = high2 - low2;
            double distanceFrom200 = exponentialMovingAverage200[0] - high0;
            if (sellLength > buyLength) {
                if ((!PositionSelect(_Symbol)) && (!selling) && (distanceFrom200 > AVERAGE_CANDLE_HEIGHT)) {
                Sell();
                selling = true;
                stopLoss = exponentialMovingAverage200[0];
            }
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
            if (open1 > close1) {
                counter++;
            }
             else {
                 counter = 0;
             }
        }

        if (selling) {
            if (open1 < close1) {
                counter++;
            }
             else {
                 counter = 0;
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
        if (accountProfit >= takeProfit) {
            tradeCount = 0;
            CloseAll();
        }

        // Stop loss
        if ((buying) && (low0 < stopLoss)) {
            tradeCount++;
            CloseAll();
        }

        if ((selling) && (high0 > stopLoss)) {
            tradeCount++;
            CloseAll();
        }

    //     if (accountProfit < stopLoss) {
    //         tradeCount++;
    //         CloseAll();
    //    }

        // Calculate number of inverted candles
        calculateInvertedCandles(accountProfit);

    }
}
