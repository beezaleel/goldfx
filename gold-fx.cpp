#include <Trade\Trade.mqh>

MqlTradeRequest request;
MqlTradeResult result;

input double Money_FixLot_Lots = 0.01;
int magicNumber = 109814;
static bool buying = false;
static bool selling = false;
static bool hasbullishCrossing = false;
static bool hasBearishCrossing = true;
input double AVERAGE_CANDLE_HEIGHT = 0.70;

// Set stop loss. This is can be changed from the UI
double stopLoss = 0;

// Set take profit. This is can be changed from the UI
input double takeProfit = 20.0;

// Average profit
input double averageProfit = 15.0;

// Has hit average profit
bool hasReachedAverageProfit = false;

// Maximum allowed candle moving in opposite direction 
input int invertedCandleCount = 1;

// Maximum number of failed trade before final exit (Stop trading)
input int maximumNumOfFailedTrades = 10;

// Exit with minimum profit
input int minimumProfit = 5;

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


    if ((open1 > close1)) {
        bearishCount++;
        if (MathAbs(open1 - close1) >= AVERAGE_CANDLE_HEIGHT)
            bearishCandleHeight++;
    }
    else {
        bullishCount++;
        if (MathAbs(open1 - close1) >= AVERAGE_CANDLE_HEIGHT)
            bullishCandleHeight++;
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

    if ((exponentialMovingAverage200[0] < low0) && 
    (exponentialMovingAverage200[0] < exponentialMovingAverage50[0]) && 
    (low1 < exponentialMovingAverage50[1] ) && 
    (high1 > exponentialMovingAverage50[1] ) &&  
    (bullishCount >= 1)) {
        if ((!PositionSelect(_Symbol)) && (!buying)) { // Check if there is no current trade running
            Buy();
            stopLoss = low1;
            buying = true;
            hasbullishCrossing = false;
        }
    }

    if ((exponentialMovingAverage200[0] > high0) && 
    (exponentialMovingAverage200[0] > exponentialMovingAverage50[0]) && 
    (high1 > exponentialMovingAverage50[1]) && 
    (low1 < exponentialMovingAverage50[1]) && 
    (bearishCount >= 1)) {
        if ((!PositionSelect(_Symbol)) && (!selling)) { // Check if there is no current trade running
            Sell();
            stopLoss = high1;
            selling = true;
            hasBearishCrossing = false;
        }
    }
}

bool shouldContinueTrading() {
    return tradeCount >= maximumNumOfFailedTrades;
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

    if ((buying)) {
        if (low0 < low1) {
            CloseAll();
            previousCandleOpen = open1;
        }
    }

    if (selling) {
        if (high0 > high1) {
            CloseAll();
            previousCandleOpen = open1;
        }
    }
}

void OnTick() {
    if (!shouldContinueTrading()) {
        double high0 = iHigh(_Symbol, PERIOD_CURRENT, 1);
        double low0 = iLow(_Symbol, PERIOD_CURRENT, 1);
        double open0 = iOpen(_Symbol, PERIOD_CURRENT, 1);
        double close0 = iClose(_Symbol, PERIOD_CURRENT, 1);

        double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
        double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
        double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
        double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

        //if (previousCandleOpen != 0 && previousCandleOpen != open1) {
            //previousCandleOpen = 0;
        //}

        // if ((stopLoss != 0) && (stopLoss != low1) && (!buying)) {
        //     stopLoss = 0;
        // }

        // if ((stopLoss != 0) && (stopLoss != high1) && (!selling)) {
        //     stopLoss = 0;
        // }

        if (buying && low0 < stopLoss) {
            CloseAll();
        }

        if (selling && high0 > stopLoss) {
            CloseAll();
        }
        
        trade();

        double accountBalance = AccountInfoDouble(ACCOUNT_BALANCE);
        double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);
        double accountEquity = AccountInfoDouble(ACCOUNT_EQUITY);

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
    //     if (accountProfit < stopLoss) {
    //         tradeCount++;
    //         CloseAll();
    //    }

        // Calculate number of inverted candles
        //calculateInvertedCandles(accountProfit);

    }
}
