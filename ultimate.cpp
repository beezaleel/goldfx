#include <Trade\Trade.mqh>

CTrade trade;

static int magicNumber = 309815;

// ── State ─────────────────────────────────────────────────────────────────────
static int      pendingDir        = 0;
static int      watchBars         = 0;
static bool     inTrade           = false;
static int      entryCount        = 0;
static double   peakProfit        = 0.0;
static bool     trailingActive    = false;
static double   firstEntryPrice   = 0.0;
static bool     breakEvenDone     = false;
static double   zoneHigh          = -1;
static double   zoneLow           = -1;
static datetime lastProcessedBar  = 0;
static int      consecutiveLosses = 0;

// Running consolidation state — updated bar by bar as price forms
static double   sellCeiling           = 0;
static double   sellCeilingAnchor     = 0;
static datetime sellCeilingAnchorTime = 0;
static int      sellHugCount          = 0;
static double   buyFloor              = 0;
static double   buyFloorAnchor        = 0;
static datetime buyFloorAnchorTime    = 0;
static int      buyHugCount           = 0;

// ── Inputs ────────────────────────────────────────────────────────────────────
input double Money_FixLot_Lots          = 0.02;
input int    MaxConsolidationBars       = 10;   // max candles in consolidation window
input int    MinConsolidationBars       = 5;    // minimum candles required
input double ConsolidationSpreadDollars = 2.0;  // max spread of highs (SELL) or lows (BUY) in $ (e.g. $2 on XAUUSD M1)
input double ZoneWidthDollars           = 10.0; // height of the armed zone in $ (e.g. $10 on XAUUSD)
input int    FastMAPeriod               = 20;   // fast MA period for trend direction
input int    SlowMAPeriod               = 100;  // slow MA period for trend direction
input double MinTrendSpreadDollars      = 1.5;  // minimum MA gap in $ to confirm a real trend
input int    EntryWindowBars            = 20;   // bars to wait for price to revisit trigger
input double StopLossDollars            = 10.0;
input double TakeProfitDollars          = 250.0;
input double TrailingActivateFactor     = 1.5;  // trailing activates when profit >= SL × this
input double TrailingSpeedFactor        = 1.5;  // closes when profit drops by SL × this from peak
input int    MaxConsecutiveLosses       = 3;

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

// Called on every new bar. Updates the running consolidation trackers using
// bar 1 (the just-completed candle). Returns:
//  -1 if a SELL ceiling just reached MinConsolidationBars
//   1 if a BUY floor just reached MinConsolidationBars
//   0 if no zone is confirmed yet
int UpdateConsolidation(double &outCeiling, double &outFloor)
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
   }
   else if (bar1High >= sellCeiling - tol)
   {
      if (bar1High > sellCeiling) sellCeiling = bar1High;
      sellHugCount++;
   }
   else
   {
      sellCeiling           = 0;
      sellCeilingAnchor     = 0;
      sellCeilingAnchorTime = 0;
      sellHugCount          = 0;
   }

   // ── BUY floor tracker ─────────────────────────────────────────────────────
   if (buyFloor == 0 || bar1Low < buyFloor - tol)
   {
      buyFloor            = bar1Low;
      buyFloorAnchor      = bar1Low;
      buyFloorAnchorTime  = bar1Time;
      buyHugCount         = 1;
   }
   else if (bar1Low <= buyFloor + tol)
   {
      if (bar1Low < buyFloor) buyFloor = bar1Low;
      buyHugCount++;
   }
   else
   {
      buyFloor            = 0;
      buyFloorAnchor      = 0;
      buyFloorAnchorTime  = 0;
      buyHugCount         = 0;
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
      outCeiling = sellCeiling;
      outFloor   = sellCeiling - DollarsToPriceDist(ZoneWidthDollars);
      Print("SELL zone confirmed: hugCount=", sellHugCount, " ceiling=", sellCeiling,
            " anchor=", sellCeilingAnchor, " drift=", (sellCeiling - sellCeilingAnchor) / _Point,
            "pts fastMA=", fastMA, " slowMA=", slowMA, " spread=", (fastMA - slowMA) / _Point, "pts");
      DrawConsolidationMark(sellCeilingAnchorTime, sellCeilingAnchor, true, true);
      sellHugCount = 0; sellCeiling = 0; sellCeilingAnchor = 0; sellCeilingAnchorTime = 0;
      buyHugCount  = 0; buyFloor    = 0; buyFloorAnchor    = 0; buyFloorAnchorTime    = 0;
      return -1;
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
      outCeiling = buyFloor + DollarsToPriceDist(ZoneWidthDollars);
      outFloor   = buyFloor;
      Print("BUY zone confirmed: hugCount=", buyHugCount, " floor=", buyFloor,
            " anchor=", buyFloorAnchor, " drift=", (buyFloorAnchor - buyFloor) / _Point,
            "pts fastMA=", fastMA, " slowMA=", slowMA, " spread=", (slowMA - fastMA) / _Point, "pts");
      DrawConsolidationMark(buyFloorAnchorTime, buyFloorAnchor, true, false);
      buyHugCount  = 0; buyFloor    = 0; buyFloorAnchor    = 0; buyFloorAnchorTime    = 0;
      sellHugCount = 0; sellCeiling = 0; sellCeilingAnchor = 0; sellCeilingAnchorTime = 0;
      return 1;
   }
   else if (buyHugCount >= MinConsolidationBars)
      Print("BUY reject: floor=", buyFloor, " anchor=", buyFloorAnchor,
            " drift=", (buyFloorAnchor - buyFloor) / _Point, "pts fastMA=", fastMA,
            " slowMA=", slowMA, " spread=", (slowMA - fastMA) / _Point, "pts (need floor<fastMA<slowMA-minSpread, drift<=", ConsolidationSpreadDollars, ")");

   return 0;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

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

   inTrade = true;
   entryCount++;
   DrawEntry(dir == 1);
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
   peakProfit       = 0.0;
   trailingActive   = false;
   firstEntryPrice  = 0.0;
   breakEvenDone    = false;
   zoneHigh         = -1;
   zoneLow          = -1;
   sellCeiling      = 0;  sellHugCount = 0;
   buyFloor         = 0;  buyHugCount  = 0;
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

      if (inTrade && !breakEvenDone && accountProfit >= StopLossDollars)
      {
         MoveToBreakEven();
         breakEvenDone = true;
      }

      if (peakProfit >= StopLossDollars * TrailingActivateFactor)
      {
         trailingActive = true;
         double drawback   = StopLossDollars * TrailingSpeedFactor;
         double trailingSL = DollarsToPriceDist(peakProfit - drawback);

         // Move the actual SL to lock in (peak - drawback); it only ever moves
         // in the profitable direction, so the blue line ratchets with the trade.
         for (int i = 0; i < PositionsTotal(); i++)
         {
            if (PositionGetSymbol(i) != _Symbol || PositionGetInteger(POSITION_MAGIC) != magicNumber)
               continue;
            double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
            long   posType   = PositionGetInteger(POSITION_TYPE);
            double newSL     = (posType == POSITION_TYPE_BUY)
                               ? openPrice + trailingSL
                               : openPrice - trailingSL;
            MoveSLToPrice(newSL);
         }
      }
   }

   // ── Per-bar logic ─────────────────────────────────────────────────────────
   datetime currentBar = iTime(_Symbol, _Period, 0);
   if (currentBar == lastProcessedBar) return;
   lastProcessedBar = currentBar;

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

      if (triggered)
         ExecMarket(pendingDir);

      return;
   }

   // ── Idle — update running consolidation state ────────────────────────────
   if (!inTrade && pendingDir == 0)
   {
double newHi, newLo;
      int zoneDir = UpdateConsolidation(newHi, newLo);
      if (zoneDir == 0) return;

      zoneHigh   = newHi;
      zoneLow    = newLo;
      pendingDir = zoneDir;
      watchBars  = 0;

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
