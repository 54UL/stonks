#pragma once

#include <UI/UIContext.hpp>
#include <Strategy/Strategy.hpp>
#include <Market/MarketHours.hpp>
#include <imgui.h>
#include <vector>
#include <string>

namespace stnks
{
    class PortfolioPanel
    {
    public:
        explicit PortfolioPanel(UIContext& ctx);
        void Draw(bool* open);

    private:
        UIContext& ctx_;

        struct Holding
        {
            std::string symbol;
            MarketType  market       = MarketType::Unknown;
            float       totalQty     = 0.f;
            float       avgEntry     = 0.f;
            float       currentPrice = 0.f;
            float       totalCost    = 0.f;
            float       marketValue  = 0.f;
            float       pnl          = 0.f;
            float       pnlPct       = 0.f;
            int         activeStrats = 0;
            int         aiStrats     = 0;
        };

        struct PeriodPnL
        {
            float realized = 0.f;
            int   trades   = 0;
            int   wins     = 0;
        };

        void ComputeHoldings(std::vector<Holding>& holdings);
        PeriodPnL ComputePeriodPnL(bool isMexican, int64_t cutoff);

        void DrawSection(const char* title, const char* currSym,
                         std::vector<Holding*>& hlist,
                         float secValue, float secCost, float secPnl,
                         bool isMexican, ImVec4 accentCol);

        void DrawSummaryCards(const char* currSym, float secValue,
                              float secPnl, float secPnlPct, int positions);
        void DrawCumulativePnLChart(const char* currSym, bool isMexican);
        void DrawLiveValueChart(const char* currSym, bool isMexican);
        void DrawPeriodTabs(const char* currSym, bool isMexican);
        void DrawHoldingsTable(std::vector<Holding*>& hlist,
                               const char* currSym, bool isMexican);
    };

} // namespace stnks
