#pragma once

#include <Strategy/Strategy.hpp>
#include <Service/IStrategyService.hpp>
#include <Market/MarketService.hpp>
#include <Charts/StrategyWizard.hpp>
#include <Charts/StockChart.hpp>
#include <Charts/Timeframes.hpp>
#include <Charts/DetachedChart.hpp>
#include <Charts/ChartLayer.hpp>
#include <Events/GraphEventService.hpp>
#include <Signals/MarketSignalService.hpp>
#include <AI/IMarketAnalyzer.hpp>
#include <AI/ClaudeAnalyzer.hpp>
#include <News/NewsService.hpp>
#include <Engine.hpp>
#include <Broker/IBrokerConnector.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>

#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdlib>

namespace stnks
{
    // Runtime env var overrides — checked before std::getenv.
    // Editable from Dashboard, not persisted to OS env.
    struct EnvOverrides
    {
        struct Entry
        {
            std::string value;
            bool        overridden = false;  // true = user edited at runtime
            bool        isSecret   = false;  // mask display
        };
        std::unordered_map<std::string, Entry> entries;

        // Get value: override if set, else std::getenv, else empty
        std::string Get(const std::string& key) const
        {
            auto it = entries.find(key);
            if (it != entries.end() && it->second.overridden)
                return it->second.value;
            const char* env = std::getenv(key.c_str());
            return env ? env : "";
        }

        bool IsSet(const std::string& key) const
        {
            auto it = entries.find(key);
            if (it != entries.end() && it->second.overridden)
                return !it->second.value.empty();
            return std::getenv(key.c_str()) != nullptr;
        }
    };

    // Subsystem on/off toggles — gate runtime behavior
    struct SystemToggles
    {
        bool ai              = true;   // Claude AI analysis
        bool news            = true;   // GNews feed
        bool strategyMonitor = true;   // TP/SL trigger checking
        bool graphEvents     = true;   // Chart pattern detection
        bool binance         = false;  // Broker connectors
        bool gbm             = false;
        bool metaTrader      = false;
    };
    // Forward
    struct Toast
    {
        std::string message;
        ImVec4      color;
        float       lifetime = 5.f;
        float       maxLife  = 5.f;
    };

    // Timeframe definitions (shared with DetachedChart)
    // Defined in Charts/Timeframes.hpp

    // Chart panel struct
    struct ChartPanelData
    {
        std::string  symbol;
        StockQuote   quote;
        StockChart   chart;
        bool         loading      = false;
        bool         refreshing   = false;
        bool         open         = true;
        int          timeframeIdx = kDefaultTimeframe;
        float        refreshTimer = 0.f;
    };

    // Telemetry counters
    struct Telemetry
    {
        int64_t marketFetches   = 0;
        int64_t strategySaves   = 0;
        int64_t strategyDeletes = 0;
        int64_t chartRefreshes  = 0;
        float   uptimeSec       = 0.f;
        float   avgFrameMs      = 0.f;
    };

    // Portfolio sample point
    struct PortfolioSample
    {
        float timestamp = 0.f;
        float usdValue  = 0.f;
        float mxnValue  = 0.f;
    };

    // Pending "view strategy" after chart loads
    struct PendingStrategyView
    {
        bool active = false;
        std::string symbol;
        float priceLo = 0.f;
        float priceHi = 0.f;
    };

    // Shared context passed to all panels — non-owning pointers/references.
    // UI owns all the data; panels just borrow it.
    struct UIContext
    {
        // Core services (non-owning)
        Engine*              engine          = nullptr;
        IStrategyService*    service         = nullptr;
        MarketService*       marketService   = nullptr;
        NewsService*         newsService     = nullptr;
        ClaudeAnalyzer*      analyzer        = nullptr;

        // Strategy data
        std::vector<Strategy>*  strategies   = nullptr;
        bool*                   strategiesDirty = nullptr;

        // Chart panels
        std::vector<ChartPanelData>* charts         = nullptr;
        std::vector<DetachedChart>*  detachedCharts  = nullptr;

        // Wizard
        StrategyWizard*       wizard         = nullptr;

        // Events & signals
        GraphEventService*    graphEvents    = nullptr;
        MarketSignalService*  signalService  = nullptr;

        // AI results
        std::vector<MarketInsight>* recommendations = nullptr;
        std::vector<MarketInsight>* warnings        = nullptr;
        std::vector<AIOperation>*   operations      = nullptr;
        bool*                       insightsLoading = nullptr;
        float*                      insightRefreshTimer = nullptr;
        float*                      insightRefreshInterval = nullptr;

        // Toasts
        std::vector<Toast>*   toasts         = nullptr;

        // Telemetry
        Telemetry*            telemetry      = nullptr;

        // Portfolio history
        std::vector<PortfolioSample>* portfolioHistory = nullptr;

        // Shared selection state
        int64_t*              selectedStrategyId = nullptr;
        int64_t*              hoveredStrategyId  = nullptr;
        bool*                 showStrategyWizard = nullptr;
        bool*                 showServerLauncher = nullptr;

        // Crosshair
        SharedCrosshair*      sharedCrosshair = nullptr;

        // Trading toggles
        bool*                 liveTradingEnabled = nullptr;
        bool*                 aiAutoTrade        = nullptr;

        // Default broker
        BrokerSource*         defaultBroker  = nullptr;
        std::string*          lastSelectedSource = nullptr;

        // Event time range
        ui::EventTimeRange*   eventTimeRange = nullptr;

        // System toggles & env overrides
        SystemToggles*        systemToggles  = nullptr;
        EnvOverrides*         envOverrides   = nullptr;

        // Data intervals (editable from dashboard)
        float*                marketRefreshInterval = nullptr;
        float*                priceCacheInterval    = nullptr;

        // Helper: get current price for a symbol
        std::function<float(const std::string&)> getCurrentPrice;

        // Helper: push toast notification
        void PushToast(const std::string& msg, const ImVec4& color, float duration = 5.f)
        {
            if (toasts)
                toasts->push_back({msg, color, duration, duration});
        }

        // Helper: fetch a symbol into charts
        std::function<void(const std::string&, const char*, const char*)> fetchSymbol;

        // Helper: refresh AI insights
        std::function<void()> refreshInsights;

        // Helper: execute trade order
        std::function<bool(const std::string&, BrokerSource, OrderSide, float, float)> executeOrder;

        // Helper: reconnect to a remote server (or switch to monolith if empty)
        std::function<void(const std::string& serverUrl)> connectToServer;
    };

} // namespace stnks
