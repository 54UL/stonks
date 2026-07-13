#pragma once

#include <UI/UIContext.hpp>
#include <UI/GraphEventsPanel.hpp>
#include <UI/MarketSignalsPanel.hpp>
#include <UI/AIPanels.hpp>
#include <imgui.h>

namespace stnks
{
    // Which tab is active in the unified Market Insights panel
    enum class InsightsTab : int
    {
        Events   = 0,
        Signals  = 1,
        Warnings = 2,
    };

    // Unified panel combining Graph Events, Market Signals, and Market Warnings
    // into a single dockable window with tabs.
    class MarketInsightsPanel
    {
    public:
        explicit MarketInsightsPanel(UIContext& ctx);
        void Draw(bool* open);

        // Programmatically switch to a tab (e.g., from chart event click)
        void FocusTab(InsightsTab tab) { pendingTab_ = (int)tab; focusRequested_ = true; }

    private:
        UIContext& ctx_;

        // Sub-panel renderers (reuse existing implementations)
        GraphEventsPanel    eventsPanel_;
        MarketSignalsPanel  signalsPanel_;
        MarketWarningsPanel warningsPanel_;

        // Tab state
        int  pendingTab_    = -1;
        bool focusRequested_ = false;

        // Badge counts for tab headers
        int GetEventCount() const;
        int GetSignalCount() const;
        int GetWarningCount() const;
    };

} // namespace stnks
