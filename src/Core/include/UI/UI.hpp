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
#include <imgui.h>
#include <imgui_internal.h>
#include <memory>
#include <vector>
#include <string>
#include <set>
#include <functional>

namespace stnks
{
    class UI : public ExecutionPipeline
    {
    public:
        explicit UI(const std::shared_ptr<Engine>& engine);
        ~UI();

        void Init() override;
        void UpdateUI() override;
        void Update() override;

    private:
        void ShowDockSpace();
        void ShowMenuBar();
        void ShowDashboard();
        void ShowThreadsDebugger();
        void ShowStockCharts();
        void ShowStrategyWizard();
        void ShowSymbolSelector();
        void ShowStrategies();
        void ShowPortfolio();
        void ShowRecommendations();
        void ShowMarketWarnings();
        void ShowAIOperations();
        void DrainAsyncResults();
        void CheckStrategyTriggers();
        void RefreshInsights();
        void DrawStrategyTable(const std::function<bool(const Strategy&)>& filter);

        void FetchSymbol(const std::string& symbol,
                         const char* interval = "1d",
                         const char* range = "6mo");

        std::shared_ptr<Engine> engine_;
        bool showDashboard_       = true;
        bool showThreadsDebugger_ = false;
        bool showStockCharts_     = true;
        bool showStrategyWizard_  = true;
        bool showStrategies_      = true;
        bool showPortfolio_       = true;
        bool showRecommendations_ = true;
        bool showMarketWarnings_  = true;
        bool showAIOperations_    = true;
        bool dockLayoutBuilt_     = false;

        // Market data
        std::unique_ptr<HttpClient>    httpClient_;
        std::unique_ptr<MarketService> marketService_;
        float                          marketRefreshTimer_    = 0.f;
        float                          marketRefreshInterval_ = 10.f; // seconds

        // Strategy service (local or remote)
        std::unique_ptr<IStrategyService> service_;

        // Dockable strategy wizard (always-open panel)
        StrategyWizard wizard_;

        // Strategy cache
        std::vector<Strategy>          cachedStrategies_;
        bool                           strategiesDirty_ = true;
        float                          strategyRefreshTimer_ = 0.f;
        float                          strategyRefreshInterval_ = 30.f; // seconds

        // Strategy selection / inline editing
        int64_t  selectedStrategyId_ = -1;   // Selected from chart gizmo
        bool     focusStrategiesTab_ = false; // One-shot flag to focus the tab
        bool     scrollToStrategy_   = false; // One-shot flag to scroll table
        Strategy editingStrategy_;            // Copy being edited inline
        bool     isEditingInline_    = false; // True when row is in edit mode
        char     editNotesBuf_[256]  = "";    // Buffer for inline notes editing
        bool     editNotesInit_      = false; // Whether notes buffer was initialized

        // Excel-like table state
        std::set<int64_t> tableSelection_;        // Multi-selected strategy IDs
        int64_t           lastClickedId_ = -1;    // For shift-click range select

        // Sorting
        enum class SortColumn { None, Symbol, Type, Dir, Entry, TP, SL, Qty, RR, PnL, Exit, Status, Notes };
        SortColumn tableSortCol_   = SortColumn::None;
        bool       tableSortAsc_   = true;

        // Cell editing (double-click to edit any cell)
        int64_t    editCellRowId_  = -1;    // Which row's cell is being edited
        int        editCellCol_    = -1;    // Which column (0-based table column index)
        char       cellEditBuf_[256] = "";  // Text buffer for cell editing
        float      cellEditFloat_  = 0.f;   // Float buffer for numeric cells

        // Helper methods
        void SortStrategies(std::vector<Strategy*>& ptrs);
        void StartCellEdit(const Strategy& s, int col);
        void CommitCellEdit(Strategy& s, int col);
        void ExportCSV();
        void ImportCSV();
        float GetCurrentPrice(const std::string& symbol) const;

        // News + AI
        std::unique_ptr<NewsService>    newsService_;
        std::unique_ptr<ClaudeAnalyzer> analyzer_;
        std::vector<MarketInsight>      cachedRecommendations_;
        std::vector<MarketInsight>      cachedWarnings_;
        std::vector<AIOperation>        cachedOperations_;
        float                           insightRefreshTimer_ = 0.f;
        float                           insightRefreshInterval_ = 300.f; // 5 min
        bool                            insightsLoading_ = false;

        // AI auto-trade toggle (persisted in globals)
        bool                            aiAutoTrade_ = false;

        // Timeframe definitions (interval → Yahoo API params)
        struct Timeframe
        {
            const char* label;     // Display label
            const char* interval;  // Yahoo interval param
            const char* range;     // Yahoo range param
        };
        static constexpr Timeframe kTimeframes[] = {
            {"1m",  "1m",  "1d"},
            {"5m",  "5m",  "5d"},
            {"15m", "15m", "5d"},
            {"30m", "30m", "1mo"},
            {"1H",  "1h",  "1mo"},
            {"4H",  "1h",  "6mo"},   // Yahoo max for 1h is 730d; use 6mo
            {"1D",  "1d",  "6mo"},
            {"1W",  "1wk", "2y"},
            {"1M",  "1mo", "5y"},
            {"1Y",  "1mo", "max"},
        };
        static constexpr int kDefaultTimeframe = 6; // "1D"

        // Chart panels
        struct ChartPanel
        {
            std::string  symbol;
            StockQuote   quote;
            StockChart   chart;
            bool         loading    = false;  // Initial load (shows spinner, no chart)
            bool         refreshing = false;  // Background refresh (keeps showing old chart)
            bool         open       = true;
            int          timeframeIdx = kDefaultTimeframe;
            float        refreshTimer = 0.f;  // Per-panel adaptive refresh countdown
        };
        std::vector<ChartPanel> charts_;

        // Symbol search / autocomplete
        char  searchInput_[64]               = "";
        std::vector<SymbolMatch> searchResults_;
        bool  searchPending_                 = false;
        float searchDebounceTimer_           = 0.f;
        std::string lastSearchQuery_;

        // Telemetry counters
        struct Telemetry
        {
            int64_t marketFetches    = 0;
            int64_t strategySaves    = 0;
            int64_t strategyDeletes  = 0;
            int64_t chartRefreshes   = 0;
            float   uptimeSec        = 0.f;
            float   avgFrameMs       = 0.f;
        };
        Telemetry telemetry_;

        // Preset symbols
        static constexpr const char* kPresetUS[]  = {"AAPL", "MSFT", "TSLA", "NVDA", "AMZN", "GOOGL"};
        static constexpr const char* kPresetMEX[] = {"CEMEXCPO.MX", "AMXL.MX", "WALMEX.MX", "BIMBOA.MX", "GFNORTEO.MX"};
    };
} // namespace stnks
