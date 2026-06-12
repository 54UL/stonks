#pragma once

#include <Engine.hpp>
#include <App/ExecutionPipeline.hpp>
#include <Http/HttpClient.hpp>
#include <Market/MarketService.hpp>
#include <Charts/StockChart.hpp>
#include <Strategy/Strategy.hpp>
#include <Strategy/StrategyStore.hpp>
#include <Service/IStrategyService.hpp>
#include <News/NewsService.hpp>
#include <AI/IMarketAnalyzer.hpp>
#include <AI/ClaudeAnalyzer.hpp>
#include <Charts/StrategyWizard.hpp>
#include <Charts/DetachedChart.hpp>
#include <Broker/IBrokerConnector.hpp>
#include <Events/GraphEventService.hpp>
#include <Signals/MarketSignalService.hpp>
#include <UI/UIContext.hpp>
#include <UI/UIConstants.hpp>
#include <UI/StrategyTablePanel.hpp>
#include <UI/PortfolioPanel.hpp>
#include <UI/MarketSignalsPanel.hpp>
#include <UI/AIPanels.hpp>
#include <UI/GraphEventsPanel.hpp>
#include <UI/DashboardPanel.hpp>
#include <UI/ServerLauncherPanel.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace stnks
{
    // Forward
    class StrategyLayer;

    // Activity bar panel entry — colored initial like IntelliJ
    struct PanelEntry
    {
        const char* icon;      // 1-2 char initial
        const char* name;      // Full name (tooltip)
        bool*       visible;   // Pointer to showXxx_ flag
        ImU32       color;     // Circle background color
    };

    enum class DockSide { Left };

    class UI : public ExecutionPipeline
    {
    public:
        explicit UI(const std::shared_ptr<Engine>& engine);
        ~UI();

        void Init() override;
        void UpdateUI() override;
        void Update() override;

    private:
        // ── Top-level orchestration ──────────────────────────────────────
        void ShowDockSpace();
        void ShowMenuBar();
        void DrawActivityBar(DockSide side);
        void ShowStockCharts();
        void ShowStrategyWizard();
        void ShowSymbolSelector();
        void ShowOptions();
        void ShowThreadsDebugger();

        void DrainAsyncResults();
        void DrainNewsResults();
        void DrainAnalysisResults();
        void CheckStrategyTriggers();
        void RefreshInsights();
        void TickUI();

        // ── TickUI sub-ticks ─────────────────────────────────────────────
        void TickMarketRefresh();
        void TickStrategyReload();
        void TickPortfolioSampler();
        void TickPriceCache();
        void TickInsightRefresh();
        void PushDataToCharts();

        // ── Chart helpers ────────────────────────────────────────────────
        void DrawChartTab(ChartPanelData& panel);
        void DrawChartFreshnessIndicator(const ChartPanelData& panel);
        void DrawChartOverlay(ChartPanelData& panel);
        void DrawSearchDropdown(const std::string& currentQuery);

        // ── Wiring helpers ───────────────────────────────────────────────
        void WireStrategyLayerCallbacks(StrategyLayer& sl, const std::string& symbol);
        void WireChartTearOutCallbacks(ChartPanelData& panel);
        void RenderDetachedCharts();
        void WireDetachedChart(DetachedChart& dc);

        // ── Threads debugger sub-draws ───────────────────────────────────
        void DrawChannelTimingsTable(const ThreadDebugInfo& td, float avail);
        void DrawFrameTimeline(const ThreadDebugInfo& td, float avail);
        void DrawHistoryPlots(const ThreadDebugInfo& td, int off, float avail);

        // ── Strategy trigger handling ────────────────────────────────────
        void HandleTrigger(Strategy& strat, StrategyStatus trigger, float price);

        // ── Fetch & View ─────────────────────────────────────────────────
        void FetchSymbol(const std::string& symbol,
                         const char* interval = "1d",
                         const char* range = "6mo");
        void ViewStrategy(const Strategy& s);

        // ── Trade execution ──────────────────────────────────────────────
        bool ExecuteOrder(const std::string& symbol, BrokerSource broker,
                          OrderSide side, float quantity, float price = 0.f);
        bool ExecuteAIOperation(AIOperation& op);
        bool ExecuteStrategyTrigger(const Strategy& strategy, StrategyStatus trigger, float exitPrice);

        float GetCurrentPrice(const std::string& symbol) const;
        void  ConnectToServer(const std::string& serverUrl);

        // ── Toast notifications ──────────────────────────────────────────
        void PushToast(const std::string& msg, const ImVec4& color, float duration = 5.f);
        void RenderToasts();

        // ── Core systems ─────────────────────────────────────────────────
        std::shared_ptr<Engine> engine_;

        // Panel visibility
        bool showDashboard_       = true;
        bool showThreadsDebugger_ = false;
        bool showStockCharts_     = true;
        bool showStrategyWizard_  = true;
        bool showStrategies_      = true;
        bool showPortfolio_       = true;
        bool showRecommendations_ = true;
        bool showMarketWarnings_  = true;
        bool showAIOperations_    = true;
        bool showStyleEditor_     = false;
        bool showOptions_         = false;
        bool showMarketSignals_   = true;
        bool showGraphEvents_     = true;
        bool showServerLauncher_  = false;
        bool dockLayoutBuilt_     = false;

        // Market data
        std::unique_ptr<HttpClient>    httpClient_;
        std::unique_ptr<MarketService> marketService_;
        float                          marketRefreshTimer_    = 0.f;
        float                          marketRefreshInterval_ = 10.f;
        float                          priceCacheTimer_       = 0.f;
        float                          priceCacheInterval_    = 15.f;

        // Strategy service
        std::unique_ptr<IStrategyService> service_;

        // Wizard
        StrategyWizard wizard_;

        // Strategy cache
        std::vector<Strategy> cachedStrategies_;
        bool                  strategiesDirty_ = true;

        // Selection
        int64_t selectedStrategyId_ = -1;
        int64_t hoveredStrategyId_  = -1;

        // Portfolio
        std::vector<PortfolioSample> portfolioHistory_;
        float portfolioSampleTimer_ = 0.f;

        // Events & signals
        GraphEventService    graphEvents_;
        MarketSignalService  signalService_;
        ui::EventTimeRange   eventTimeRange_ = ui::EventTimeRange::Last1D;

        // Crosshair
        SharedCrosshair sharedCrosshair_;

        // AI
        std::unique_ptr<NewsService>    newsService_;
        std::unique_ptr<ClaudeAnalyzer> analyzer_;
        std::vector<MarketInsight>      cachedRecommendations_;
        std::vector<MarketInsight>      cachedWarnings_;
        std::vector<AIOperation>        cachedOperations_;
        float                           insightRefreshTimer_ = 0.f;
        float                           insightRefreshInterval_ = 300.f;
        bool                            insightsLoading_ = false;

        // Trading
        bool aiAutoTrade_        = false;
        bool liveTradingEnabled_ = false;

        // System toggles & env overrides
        SystemToggles systemToggles_;
        EnvOverrides  envOverrides_;

        // Charts
        std::vector<ChartPanelData> charts_;
        std::vector<DetachedChart>  detachedCharts_;

        // Symbol search
        char searchInput_[64] = "";
        std::vector<SymbolMatch> searchResults_;
        bool  searchPending_       = false;
        float searchDebounceTimer_ = 0.f;
        std::string lastSearchQuery_;

        // Broker
        BrokerSource defaultBroker_ = BrokerSource::Auto;
        std::string  lastSelectedSource_;

        // Toasts
        std::vector<Toast> toasts_;

        // Telemetry
        Telemetry telemetry_;

        // Pending view
        PendingStrategyView pendingView_;

        // ── Activity Bar ────────────────────────────────────────────────
        std::vector<PanelEntry> activityPanels_;
        static constexpr float kActivityBarWidth = 30.f;

        // ── Refactored Panels ────────────────────────────────────────────
        UIContext ctx_;
        void BuildContext();

        std::unique_ptr<StrategyTablePanel>    strategyTable_;
        std::unique_ptr<PortfolioPanel>        portfolioPanel_;
        std::unique_ptr<MarketSignalsPanel>    signalsPanel_;
        std::unique_ptr<RecommendationsPanel>  recsPanel_;
        std::unique_ptr<MarketWarningsPanel>   warningsPanel_;
        std::unique_ptr<AIOperationsPanel>     aiOpsPanel_;
        std::unique_ptr<GraphEventsPanel>      graphEventsPanel_;
        std::unique_ptr<DashboardPanel>        dashboardPanel_;
        std::unique_ptr<ServerLauncherPanel>   serverLauncherPanel_;

        // Preset symbols
        static constexpr const char* kPresetUS[] = {
            "AAPL", "MSFT", "TSLA", "NVDA", "AMZN", "GOOGL", "META", "NFLX", "AMD", "INTC"
        };
        static constexpr const char* kPresetMEX[] = {
            "CEMEXCPO.MX", "AMXL.MX", "WALMEX.MX", "BIMBOA.MX", "GFNORTEO.MX",
            "FEMSAUBD.MX", "GRUMAB.MX", "TABORAMX.MX", "GCARSOA1.MX", "KOFUBL.MX",
            "PABORAMX.MX", "LIVEPOLC-1.MX", "ASURB.MX", "MEGACPO.MX", "ALPEKA.MX"
        };
        static constexpr const char* kPresetCrypto[] = {
            "BTC-USD", "ETH-USD", "SOL-USD", "BNB-USD", "XRP-USD",
            "ADA-USD", "DOGE-USD", "AVAX-USD", "DOT-USD", "MATIC-USD"
        };
    };
} // namespace stnks
