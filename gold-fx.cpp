#include <Trade\Trade.mqh>

MqlTradeRequest request;
MqlTradeResult result;

input double Money_FixLot_Lots = 0.03;
//input double takeProfit = 10.0;
int magicNumber = 109814;
static bool buying = false;
static bool selling = false;
static bool hasbullishCrossing = false;
static bool hasBearishCrossing = false;
static bool hasbullishCrossing2 = false;
static bool hasBearishCrossing2 = false;
input double stopLoss = -10.0;
input double takeProfit = 30.0;
input int maximumCount = 4;
static int counter = 0;
static double previousCandleOpen = 0.0;
static double previousCandleClose = 0.0;
double candle5Open = 0;
double candle5Close = 0;

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
    //OrdersTotal()
}

void CloseAllOrders() {
    CTrade trade;

    int i = PositionsTotal() - 1;

    while (i >= 0) {
        if (trade.PositionClose(PositionGetSymbol(i)))
            i--;
    }
}

void trade() {

    double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
    double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    double high2 = iHigh(_Symbol, PERIOD_CURRENT, 2);
    double low2 = iLow(_Symbol, PERIOD_CURRENT, 2);
    double open2 = iOpen(_Symbol, PERIOD_CURRENT, 2);
    double close2 = iClose(_Symbol, PERIOD_CURRENT, 2);

    double exponentialMovingAverage9[];
    int exponentialMovingAverage9Def = iMA(_Symbol, _Period, 9, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage9, true);
    CopyBuffer(exponentialMovingAverage9Def, 0, 0, 3, exponentialMovingAverage9);

    double exponentialMovingAverage20[];
    int exponentialMovingAverage20Def = iMA(_Symbol, _Period, 20, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage20, true);
    CopyBuffer(exponentialMovingAverage20Def, 0, 0, 3, exponentialMovingAverage20);

    double exponentialMovingAverage50[];
    int exponentialMovingAverage50Def = iMA(_Symbol, _Period, 50, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage50, true);
    CopyBuffer(exponentialMovingAverage50Def, 0, 0, 3, exponentialMovingAverage50);

    double exponentialMovingAverage200[];
    int exponentialMovingAverage200Def = iMA(_Symbol, _Period, 200, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage200, true);
    CopyBuffer(exponentialMovingAverage200Def, 0, 0, 3, exponentialMovingAverage200);

    if (((exponentialMovingAverage9[0] > exponentialMovingAverage20[0]) && (exponentialMovingAverage9[1] < exponentialMovingAverage20[1]))) {
        hasbullishCrossing = true;
        //stopLoss = exponentialMovingAverage9[0];
    }

    if (((exponentialMovingAverage9[0] > exponentialMovingAverage50[0]) && (exponentialMovingAverage9[1] < exponentialMovingAverage50[1]))) {
        hasbullishCrossing2 = true;
        hasBearishCrossing = false;
        hasBearishCrossing2 = false;
        candle5Open = open1;
        candle5Close = close1;
        //stopLoss = low1;
    }

    if ((!PositionSelect(_Symbol)) && (hasbullishCrossing) && (hasbullishCrossing2) && (!buying)) {
        int bullishcount = 0;
        int indicatorCount = 0;

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

        if (open5 == candle5Open && close5 == candle5Close) {
            if (open5 < close5)
            bullishcount++;
            if (open4 < close4)
                bullishcount++;
            if (open3 < close3)
                bullishcount++;
            if (open2 < close2)
                bullishcount++;
            if (open1 < close1)
                bullishcount++;
            
            if (open5 > low5)
                indicatorCount++;
            if (open4 > low5)
                indicatorCount++;
            if (open3 > low5)
                indicatorCount++;
            if (open2 > low5)
                indicatorCount++;
            if (open1 > low5)
                indicatorCount++;
            
            if (indicatorCount >= 3 && bullishcount >= 2) {
                Buy();
                buying = true;
                hasbullishCrossing = false;
                hasbullishCrossing2 = false;
            }
        }
    }

    if (((exponentialMovingAverage9[0] < exponentialMovingAverage20[0]) && (exponentialMovingAverage9[1] > exponentialMovingAverage20[1]))) {
        hasBearishCrossing = true;
        //stopLoss = exponentialMovingAverage9[0];
    }

    
    if (((exponentialMovingAverage9[0] < exponentialMovingAverage50[0]) && (exponentialMovingAverage9[1] > exponentialMovingAverage50[1]))) {
        hasBearishCrossing2 = true;
        hasbullishCrossing = false;
        hasbullishCrossing2 = false;
        candle5Open = open1;
        candle5Close = close1;
        //stopLoss = high1;
    }

    if ((!PositionSelect(_Symbol)) && (hasBearishCrossing) && (hasBearishCrossing2) && (!selling)) {
        int bearishcount = 0;
        int indicatorCount = 0;

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

        if (open5 == candle5Open && close5 == candle5Close) {
            if (open5 > close5)
            bearishcount++;
            if (open4 > close4)
                bearishcount++;
            if (open3 > close3)
                bearishcount++;
            if (open2 > close2)
                bearishcount++;
            if (open1 > close1)
                bearishcount++;
            
            if (open5 < high5)
                indicatorCount++;
            if (open4 < high5)
                indicatorCount++;
            if (open3 < high5)
                indicatorCount++;
            if (open2 < high5)
                indicatorCount++;
            if (open1 < high5)
                indicatorCount++;

            if (indicatorCount >= 3 && bearishcount >= 2) {
                Sell();
                selling = true;
                hasBearishCrossing = false;
                hasBearishCrossing2 = false;
            }
        } 
    }
}

void OnTick()
{
    double high0 = iHigh(_Symbol, PERIOD_CURRENT, 0);
    double low0 = iLow(_Symbol, PERIOD_CURRENT, 0);
    double open0 = iOpen(_Symbol, PERIOD_CURRENT, 0);
    double close0 = iClose(_Symbol, PERIOD_CURRENT, 0);

    double high1 = iHigh(_Symbol, PERIOD_CURRENT, 1);
    double low1 = iLow(_Symbol, PERIOD_CURRENT, 1);
    double open1 = iOpen(_Symbol, PERIOD_CURRENT, 1);
    double close1 = iClose(_Symbol, PERIOD_CURRENT, 1);

    double accountBalance = AccountInfoDouble(ACCOUNT_BALANCE);
    double accountProfit = AccountInfoDouble(ACCOUNT_PROFIT);
    double accountEquity = AccountInfoDouble(ACCOUNT_EQUITY);

    double difference = accountEquity - accountBalance;

    trade();

    double exponentialMovingAverage9[];
    int exponentialMovingAverage9Def = iMA(_Symbol, _Period, 9, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage9, true);
    CopyBuffer(exponentialMovingAverage9Def, 0, 0, 3, exponentialMovingAverage9);

    double exponentialMovingAverage50[];
    int exponentialMovingAverage50Def = iMA(_Symbol, _Period, 50, 0, MODE_EMA, PRICE_CLOSE);
    ArraySetAsSeries(exponentialMovingAverage50, true);
    CopyBuffer(exponentialMovingAverage50Def, 0, 0, 3, exponentialMovingAverage50);

    if (difference > takeProfit) {
        CloseAll();
    }
    if (difference < stopLoss) {
        CloseAll();
    }

    if (PositionSelect(_Symbol) && selling) {
        hasBearishCrossing = false;
        hasBearishCrossing2 = false;
    }

    if (PositionSelect(_Symbol) && buying) {
        hasbullishCrossing = false;
        hasbullishCrossing2 = false;
    }

    if ((exponentialMovingAverage9[0] > exponentialMovingAverage50[0]) &&
        (exponentialMovingAverage9[1] < exponentialMovingAverage50[1])) {
        if (selling) {
            CloseAll();
            selling = false;
        }
    }

    if ((exponentialMovingAverage9[0] < exponentialMovingAverage50[0]) &&
        (exponentialMovingAverage9[1] > exponentialMovingAverage50[1])) {
         if (buying) {
            CloseAll();
            buying = false;
        }
    }

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

    //if (buying && low0 < stopLoss) {
     // CloseAll();
    //}

   // if (selling && high0 > stopLoss) {
     // CloseAll();
   // }

    // if ((counter == maximumCount) && (difference > 1)) {
    //     CloseAll();
    // }
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
    hasbullishCrossing2 = false;
    hasBearishCrossing2 = false;
}

