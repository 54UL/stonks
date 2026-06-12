#include <UI/UI.hpp>
#include <Charts/StrategyLayer.hpp>
#include <Market/MarketHours.hpp>
#include <Service/LocalStrategyService.hpp>
#include <Service/RemoteStrategyService.hpp>
#include <Dependencies/Globals.hpp>
#include <GlobalKeys.hpp>
#include <portable-file-dialogs.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <cstdio>

namespace stnks
{
    UI::UI(const std::shared_ptr<Engine>& engine) : engine_(engine) {}
    UI::~UI()
    {
        // Stop RT polling before MarketService/HttpClient are destroyed
        if (marketService_)
            marketService_->StopAllPolling();
    }

    // ── BuildContext ────────────────────────────────────────────────────────────

    void UI::BuildContext()
    {
        ctx_.engine          = engine_.get();
        ctx_.service         = service_.get();
        ctx_.marketService   = marketService_.get();
        ctx_.newsService     = newsService_.get();
        ctx_.analyzer        = analyzer_.get();

        ctx_.strategies      = &cachedStrategies_;
        ctx_.strategiesDirty = &strategiesDirty_;

        ctx_.charts          = &charts_;
        ctx_.detachedCharts  = &detachedCharts_;
        ctx_.wizard          = &wizard_;

        ctx_.graphEvents     = &graphEvents_;
        ctx_.signalService   = &signalService_;

        ctx_.recommendations = &cachedRecommendations_;
        ctx_.warnings        = &cachedWarnings_;
        ctx_.operations      = &cachedOperations_;
        ctx_.insightsLoading = &insightsLoading_;
        ctx_.insightRefreshTimer   = &insightRefreshTimer_;
        ctx_.insightRefreshInterval = &insightRefreshInterval_;

        ctx_.toasts          = &toasts_;
        ctx_.telemetry       = &telemetry_;
        ctx_.portfolioHistory = &portfolioHistory_;

        ctx_.selectedStrategyId = &selectedStrategyId_;
        ctx_.hoveredStrategyId  = &hoveredStrategyId_;
        ctx_.showStrategyWizard = &showStrategyWizard_;
        ctx_.showServerLauncher = &showServerLauncher_;

        ctx_.sharedCrosshair = &sharedCrosshair_;

        ctx_.liveTradingEnabled = &liveTradingEnabled_;
        ctx_.aiAutoTrade        = &aiAutoTrade_;
        ctx_.defaultBroker      = &defaultBroker_;
        ctx_.lastSelectedSource = &lastSelectedSource_;
        ctx_.eventTimeRange     = &eventTimeRange_;
        ctx_.systemToggles      = &systemToggles_;
        ctx_.envOverrides       = &envOverrides_;
        ctx_.marketRefreshInterval = &marketRefreshInterval_;
        ctx_.priceCacheInterval    = &priceCacheInterval_;

        ctx_.getCurrentPrice = [this](const std::string& sym) { return GetCurrentPrice(sym); };
        ctx_.fetchSymbol     = [this](const std::string& sym, const char* iv, const char* rng) {
            FetchSymbol(sym, iv, rng);
        };
        ctx_.refreshInsights = [this]() { RefreshInsights(); };
        ctx_.executeOrder    = [this](const std::string& sym, BrokerSource b, OrderSide s, float q, float p) {
            return ExecuteOrder(sym, b, s, q, p);
        };
        ctx_.connectToServer = [this](const std::string& url) { ConnectToServer(url); };
    }

    // ── Init ────────────────────────────────────────────────────────────────────

    void UI::Init()
    {
        ui::ApplyDefaultTheme();

        httpClient_    = std::make_unique<HttpClient>(engine_->threadRegistry_);
        marketService_ = std::make_unique<MarketService>(*httpClient_, engine_->threadRegistry_);

        // Strategy service: check STNKS_SERVER_URL env for remote mode, else monolith
        const char* serverUrl = std::getenv("STNKS_SERVER_URL");

        if (serverUrl && serverUrl[0] != '\0')
        {
            service_ = std::make_unique<RemoteStrategyService>(*httpClient_, serverUrl);
            spdlog::info("[UI] Remote mode: connecting to {}", serverUrl);
        }
        else
        {
            LocalStrategyService::Config localCfg;
            localCfg.server.pollIntervalSec = 60;
            localCfg.startMonitoring = true;
            service_ = std::make_unique<LocalStrategyService>(*httpClient_, engine_->threadRegistry_, localCfg);
            spdlog::info("[UI] Monolith mode: embedded server");
        }

        // News service
        const char* gnewsKey = std::getenv("GNEWS_API_KEY");
        newsService_ = std::make_unique<NewsService>(
            *httpClient_, engine_->threadRegistry_,
            gnewsKey ? gnewsKey : "");

        // AI analyzer
        ClaudeAnalyzer::Config aiConfig;
        const char* claudeKey = std::getenv("CLAUDE_API_KEY");
        if (claudeKey) aiConfig.apiKey = claudeKey;
        analyzer_ = std::make_unique<ClaudeAnalyzer>(*httpClient_, aiConfig);

        // Load persisted settings
        if (engine_->globals_)
        {
            std::string val = engine_->globals_->Get(gk::prefix::STATE, gk::key::AI_AUTO_TRADE);
            aiAutoTrade_ = (val == "1");
            std::string ltVal = engine_->globals_->Get(gk::prefix::STATE, gk::key::LIVE_TRADING);
            liveTradingEnabled_ = (ltVal == "1");
        }

        // Initialize env var registry (populate current values, mark secrets)
        auto regEnv = [this](const char* key, bool secret) {
            const char* val = std::getenv(key);
            envOverrides_.entries[key] = {val ? val : "", false, secret};
        };
        regEnv("CLAUDE_API_KEY",      true);
        regEnv("GNEWS_API_KEY",       true);
        regEnv("BINANCE_API_KEY",     true);
        regEnv("BINANCE_API_SECRET",  true);
        regEnv("BINANCE_SANDBOX",     false);
        regEnv("GBM_CLIENT_ID",       true);
        regEnv("GBM_CLIENT_SECRET",   true);
        regEnv("GBM_REFRESH_TOKEN",   true);
        regEnv("GBM_ACCOUNT_ID",      false);
        regEnv("GBM_SANDBOX",         false);
        regEnv("MT5_API_KEY",         true);
        regEnv("MT5_ACCOUNT_ID",      false);
        regEnv("STNKS_SERVER_URL",    false);
        regEnv("ASSETS_STNKS",        false);

        // Default system toggles based on key availability
        systemToggles_.ai       = envOverrides_.IsSet("CLAUDE_API_KEY");
        systemToggles_.news     = envOverrides_.IsSet("GNEWS_API_KEY");
        systemToggles_.binance  = envOverrides_.IsSet("BINANCE_API_KEY");
        systemToggles_.gbm      = envOverrides_.IsSet("GBM_CLIENT_ID");
        systemToggles_.metaTrader = envOverrides_.IsSet("MT5_API_KEY");

        // Build shared context and create all panels
        BuildContext();
        strategyTable_  = std::make_unique<StrategyTablePanel>(ctx_);
        portfolioPanel_ = std::make_unique<PortfolioPanel>(ctx_);
        signalsPanel_   = std::make_unique<MarketSignalsPanel>(ctx_);
        recsPanel_      = std::make_unique<RecommendationsPanel>(ctx_);
        warningsPanel_  = std::make_unique<MarketWarningsPanel>(ctx_);
        aiOpsPanel_     = std::make_unique<AIOperationsPanel>(ctx_);
        graphEventsPanel_ = std::make_unique<GraphEventsPanel>(ctx_);
        dashboardPanel_ = std::make_unique<DashboardPanel>(ctx_);
        serverLauncherPanel_ = std::make_unique<ServerLauncherPanel>(ctx_);

        // Activity bar: colored initials (IntelliJ style)
        activityPanels_ = {
            {"C",  "Stock Charts",     &showStockCharts_,     ui::kABCharts},
            {"S",  "Strategies",       &showStrategies_,      ui::kABStrategies},
            {"P",  "Portfolio",        &showPortfolio_,       ui::kABPortfolio},
            {"W",  "Strategy Wizard",  &showStrategyWizard_,  ui::kABWizard},
            {"D",  "Dashboard",        &showDashboard_,       ui::kABDashboard},
            {"R",  "Recommendations",  &showRecommendations_, ui::kABRecs},
            {"!",  "Market Warnings",  &showMarketWarnings_,  ui::kABWarnings},
            {"A",  "AI Operations",    &showAIOperations_,    ui::kABAIOps},
            {"E",  "Graph Events",     &showGraphEvents_,     ui::kABEvents},
            {"M",  "Market Signals",   &showMarketSignals_,   ui::kABSignals},
            {"L",  "Server Launcher",  &showServerLauncher_,  ui::kABServer},
            {"T",  "Threads Debugger", &showThreadsDebugger_, ui::kABThreads},
        };

        spdlog::info("[UI] Initialized");
    }

    void UI::Update() {}

    // ── Server Connection ─────────────────────────────────────────────────────

    void UI::ConnectToServer(const std::string& serverUrl)
    {
        if (serverUrl.empty())
        {
            // Switch to monolith mode
            LocalStrategyService::Config localCfg;
            localCfg.server.pollIntervalSec = 60;
            localCfg.startMonitoring = true;
            service_ = std::make_unique<LocalStrategyService>(*httpClient_, engine_->threadRegistry_, localCfg);
            spdlog::info("[UI] Switched to monolith mode");
            PushToast("Switched to local (embedded) server", ui::kToastInfo);
        }
        else
        {
            service_ = std::make_unique<RemoteStrategyService>(*httpClient_, serverUrl);
            spdlog::info("[UI] Connecting to remote server: {}", serverUrl);
            PushToast("Connecting to " + serverUrl + "...", ui::kToastInfo);
        }

        // Update context pointer so all panels see the new service
        ctx_.service = service_.get();
        strategiesDirty_ = true;
    }

    // ── Async Result Draining ──────────────────────────────────────────────────

    void UI::DrainAsyncResults()
    {
        marketService_->DrainQuoteResults([this](QuoteFetchResult&& result) {
            telemetry_.chartRefreshes++;

            if (!result.quote.candles.empty())
                marketService_->UpdatePriceCache(result.symbol, result.quote.candles.back().close);

            StockQuote detachedCopy = result.quote;

            for (auto& panel : charts_)
            {
                // Background refresh
                if (panel.symbol == result.symbol && panel.refreshing)
                {
                    panel.quote      = std::move(result.quote);
                    panel.refreshing = false;
                    panel.chart.SetData(panel.quote);
                    if (systemToggles_.graphEvents)
                    {
                        graphEvents_.Scan(panel.symbol, panel.quote);
                        auto newEvs = graphEvents_.ConsumeNew();
                        if (!newEvs.empty())
                        {
                            signalService_.IngestGraphEvents(newEvs);
                            auto pm = graphEvents_.GetPatternMatches(panel.symbol);
                            float price = GetCurrentPrice(panel.symbol);
                            if (!pm.empty()) signalService_.IngestPatternMatches(panel.symbol, pm, price);
                        }
                    }
                    break;
                }

                // Initial load
                if (panel.symbol == result.symbol && panel.loading)
                {
                    panel.quote   = std::move(result.quote);
                    panel.loading = false;

                    panel.chart.AddLayer<CandlestickLayer>();
                    auto& stratLayer = panel.chart.AddLayer<StrategyLayer>();
                    panel.chart.AddLayer<VolumeLayer>();
                    panel.chart.AddLayer<RSILayer>();
                    panel.chart.AddLayer<MACDLayer>();
                    panel.chart.SetData(panel.quote);
                    if (systemToggles_.graphEvents)
                    {
                        graphEvents_.Scan(panel.symbol, panel.quote);
                        auto newEvs = graphEvents_.ConsumeNew();
                        if (!newEvs.empty())
                        {
                            signalService_.IngestGraphEvents(newEvs);
                            auto pm = graphEvents_.GetPatternMatches(panel.symbol);
                            float price = GetCurrentPrice(panel.symbol);
                            if (!pm.empty()) signalService_.IngestPatternMatches(panel.symbol, pm, price);
                        }
                    }

                    WireStrategyLayerCallbacks(stratLayer, panel.symbol);
                    WireChartTearOutCallbacks(panel);

                    // Apply pending strategy view focus
                    if (pendingView_.active && pendingView_.symbol == panel.symbol)
                    {
                        panel.chart.FocusOnPriceRange(pendingView_.priceLo, pendingView_.priceHi);
                        pendingView_.active = false;
                    }

                    spdlog::info("[UI] Chart loaded for {}", panel.symbol);
                    break;
                }
            }

            // Route data to detached charts
            if (!detachedCopy.candles.empty())
            {
                for (auto& dc : detachedCharts_)
                {
                    if (dc.symbol == result.symbol && (dc.searchDirty || dc.refreshing))
                    {
                        dc.UpdateData(detachedCopy);
                        if (systemToggles_.graphEvents)
                        {
                            graphEvents_.Scan(dc.symbol, detachedCopy);
                            auto newEvs = graphEvents_.ConsumeNew();
                            if (!newEvs.empty())
                            {
                                signalService_.IngestGraphEvents(newEvs);
                                auto pm = graphEvents_.GetPatternMatches(dc.symbol);
                                float price = GetCurrentPrice(dc.symbol);
                                if (!pm.empty()) signalService_.IngestPatternMatches(dc.symbol, pm, price);
                            }
                        }
                    }
                }
            }
        });

        marketService_->DrainSearchResults([this](std::vector<SymbolMatch>&& matches) {
            searchResults_ = std::move(matches);
            searchPending_ = false;
        });

        DrainNewsResults();
        DrainAnalysisResults();
    }

    void UI::DrainNewsResults()
    {
        newsService_->DrainResults([this](NewsFetchResult&& result) {
            if (!result.ok)
            {
                spdlog::warn("[News] Fetch failed for '{}': {}", result.symbol, result.error);
                return;
            }

            std::vector<Strategy> activeForSymbol;
            for (auto& s : cachedStrategies_)
                if (s.symbol == result.symbol && s.IsActive())
                    activeForSymbol.push_back(s);

            ChartContext chartCtx;
            chartCtx.currentPrice = GetCurrentPrice(result.symbol);

            for (auto& panel : charts_)
            {
                if (panel.symbol != result.symbol || panel.quote.candles.empty()) continue;
                auto& candles = panel.quote.candles;
                auto& last = candles.back();
                chartCtx.open24h  = candles.front().open;
                chartCtx.high24h  = last.high;
                chartCtx.low24h   = last.low;
                for (auto& c : candles)
                {
                    if (c.high > chartCtx.high24h) chartCtx.high24h = c.high;
                    if (c.low < chartCtx.low24h)   chartCtx.low24h  = c.low;
                    chartCtx.volume24h += c.volume;
                }
                if (chartCtx.open24h > 0.f)
                    chartCtx.changePct24h = ((last.close - chartCtx.open24h) / chartCtx.open24h) * 100.f;
                break;
            }

            int64_t rangeSec = ui::EventTimeRangeSeconds(eventTimeRange_);
            auto allEvs = graphEvents_.GetEvents(result.symbol);
            if (rangeSec > 0)
            {
                int64_t cutoff = std::time(nullptr) - rangeSec;
                for (auto& ev : allEvs)
                    if (ev.timestamp >= cutoff) chartCtx.recentEvents.push_back(ev);
            }
            else
                chartCtx.recentEvents = std::move(allEvs);

            chartCtx.recentPatterns = graphEvents_.GetPatternMatches(result.symbol);

            // Only run AI analysis if AI system is enabled
            if (systemToggles_.ai)
            {
                // If news is disabled, pass empty articles so prompt excludes news section
                std::vector<NewsArticle> emptyArticles;
                const auto& articles = systemToggles_.news ? result.articles : emptyArticles;
                analyzer_->AnalyzeAsyncWithContext(result.symbol, articles,
                                                    activeForSymbol, chartCtx,
                                                    engine_->threadRegistry_);
            }
        });
    }

    void UI::DrainAnalysisResults()
    {
        if (!systemToggles_.ai) return;
        analyzer_->DrainResults([this](AnalysisResult&& result) {
            insightsLoading_ = false;
            if (!result.ok)
            {
                spdlog::warn("[AI] Analysis failed for '{}': {}", result.symbol, result.error);
                return;
            }

            auto removeSymbol = [&](std::vector<MarketInsight>& vec) {
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                    [&](const MarketInsight& i) { return i.symbol == result.symbol; }),
                    vec.end());
            };
            removeSymbol(cachedRecommendations_);
            removeSymbol(cachedWarnings_);

            for (auto& r : result.recommendations)
                cachedRecommendations_.push_back(std::move(r));
            for (auto& w : result.warnings)
                cachedWarnings_.push_back(std::move(w));

            cachedOperations_.erase(
                std::remove_if(cachedOperations_.begin(), cachedOperations_.end(),
                    [&](const AIOperation& op) { return op.symbol == result.symbol; }),
                cachedOperations_.end());
            for (auto& op : result.operations)
                cachedOperations_.push_back(std::move(op));

            if (aiAutoTrade_)
            {
                for (auto& op : cachedOperations_)
                {
                    if (op.symbol != result.symbol || op.executed) continue;
                    if (op.type == OperationType::Hold) continue;
                    ExecuteAIOperation(op);
                }
            }

            float price = GetCurrentPrice(result.symbol);
            signalService_.IngestAIResult(result, price);

            spdlog::info("[AI] Got {} recs + {} warnings + {} ops for '{}'",
                         result.recommendations.size(), result.warnings.size(),
                         result.operations.size(), result.symbol);
        });
    }

    // ── Strategy Layer Wiring ──────────────────────────────────────────────────

    void UI::WireStrategyLayerCallbacks(StrategyLayer& stratLayer, const std::string& /*symbol*/)
    {
        stratLayer.onStrategyChanged = [this](const Strategy& s, bool isNew) {
            if (isNew)
            {
                telemetry_.strategySaves++;
                int64_t id = service_->InsertStrategy(s);
                if (id > 0)
                {
                    spdlog::info("[Strategy] Created #{} for {} (type={} entry={:.2f})",
                                 id, s.symbol, StrategyTypeToString(s.type), s.entryPrice);
                    PushToast("Strategy created for " + s.symbol, ui::kToastSuccess);
                }
                else
                {
                    spdlog::error("[Strategy] Failed to insert for {} (server: {})",
                                  s.symbol, service_->GetServerUrl());
                    PushToast("Failed to create strategy for " + s.symbol, ui::kToastError);
                }
            }
            else
            {
                bool ok = service_->UpdateStrategy(s);
                if (ok)
                    spdlog::info("[Strategy] Updated #{} for {}", s.id, s.symbol);
                else
                {
                    spdlog::error("[Strategy] Failed to update #{}", s.id);
                    PushToast("Failed to update strategy #" + std::to_string(s.id), ui::kToastError);
                }

                if (strategyTable_)
                    strategyTable_->ClearCellEditForRow(s.id);
            }
            strategiesDirty_ = true;
        };

        stratLayer.onStrategyCancelled = [this](int64_t id) {
            float exitPrice = 0.f;
            for (auto& s : cachedStrategies_)
            {
                if (s.id == id)
                {
                    exitPrice = GetCurrentPrice(s.symbol);
                    graphEvents_.RecordStrategyEvent(s, StrategyStatus::Cancelled, exitPrice);
                    break;
                }
            }
            service_->CancelStrategy(id, exitPrice);
            strategiesDirty_ = true;
            spdlog::info("[Strategy] Cancelled #{}", id);
        };

        stratLayer.onStrategySelected = [this](int64_t id) {
            selectedStrategyId_ = id;
            showStrategies_     = true;

            if (strategyTable_)
                strategyTable_->SelectSingle(id);

            for (auto& s : cachedStrategies_)
            {
                if (s.id == id)
                {
                    wizard_.OpenEdit(s);
                    showStrategyWizard_ = true;
                    break;
                }
            }

            spdlog::info("[Strategy] Selected #{} for editing", id);
        };

        stratLayer.onEditingDismissed = [this]() {
            selectedStrategyId_ = -1;
        };

        stratLayer.onCreateRequested = [this](const std::string& symbol,
            StrategyType type, StrategyDirection dir, float price, float visibleRange) {
            wizard_.OpenCreate(symbol, type, dir, price, visibleRange);
            showStrategyWizard_ = true;
        };
    }

    void UI::WireChartTearOutCallbacks(ChartPanelData& panel)
    {
        std::string sym = panel.symbol;
        panel.chart.onIndicatorTearOut = [this, sym](const std::string& indicatorName) {
            for (auto& p : charts_)
            {
                if (p.symbol == sym)
                {
                    auto dc = DetachedChart::Create(sym, p.quote, indicatorName);
                    WireDetachedChart(dc);
                    detachedCharts_.push_back(std::move(dc));
                    spdlog::info("[UI] Detached indicator '{}' for {}", indicatorName, sym);
                    return;
                }
            }
        };

        panel.chart.onDuplicateChart = [this, sym]() {
            for (auto& p : charts_)
            {
                if (p.symbol == sym)
                {
                    auto dc = DetachedChart::Create(sym, p.quote);
                    WireDetachedChart(dc);
                    detachedCharts_.push_back(std::move(dc));
                    spdlog::info("[UI] Detached full chart for {}", sym);
                    return;
                }
            }
        };
    }

    // ── Strategy Trigger Checking ──────────────────────────────────────────────

    void UI::CheckStrategyTriggers()
    {
        if (!systemToggles_.strategyMonitor) return;

        bool changed = false;

        for (auto& strat : cachedStrategies_)
        {
            if (!strat.IsActive() || !strat.IsEnabled()) continue;
            if (strat.takeProfit <= 0.f && strat.stopLoss <= 0.f) continue;

            float price = GetCurrentPrice(strat.symbol);

            if (price <= 0.f)
            {
                for (auto& panel : charts_)
                {
                    if (panel.symbol == strat.symbol && !panel.quote.candles.empty())
                    {
                        price = panel.quote.candles.back().close;
                        break;
                    }
                }
            }

            if (price <= 0.f) continue;

            bool tpHit = false, slHit = false;
            if (strat.direction == StrategyDirection::Long)
            {
                if (strat.takeProfit > 0.f) tpHit = price >= strat.takeProfit;
                if (strat.stopLoss > 0.f)   slHit = price <= strat.stopLoss;
            }
            else
            {
                if (strat.takeProfit > 0.f) tpHit = price <= strat.takeProfit;
                if (strat.stopLoss > 0.f)   slHit = price >= strat.stopLoss;
            }

            if (tpHit)
            {
                HandleTrigger(strat, StrategyStatus::TPHit, price);
                changed = true;
            }
            else if (slHit)
            {
                HandleTrigger(strat, StrategyStatus::SLHit, price);
                changed = true;
            }
        }

        if (changed)
            strategiesDirty_ = true;
    }

    void UI::HandleTrigger(Strategy& strat, StrategyStatus trigger, float price)
    {
        int64_t now = std::time(nullptr);
        strat.status       = trigger;
        strat.triggeredAt  = now;
        strat.exitPrice    = price;
        strat.closedPnlPct = strat.UnrealizedPnLPercent(price);
        if (!service_->UpdateStrategy(strat))
            PushToast("Failed to save trigger for " + strat.symbol, ui::kToastError);
        graphEvents_.RecordStrategyEvent(strat, trigger, price);
        signalService_.IngestStrategyTrigger(strat, trigger, price);
        ExecuteStrategyTrigger(strat, trigger, price);

        const char* label = (trigger == StrategyStatus::TPHit) ? "TP HIT" : "SL HIT";
        ImVec4 color = (trigger == StrategyStatus::TPHit) ? ui::kToastSuccess : ui::kToastError;

        char toastBuf[128];
        char pBuf[32];
        FmtPrice(pBuf, sizeof(pBuf), price, strat.symbol);
        snprintf(toastBuf, sizeof(toastBuf), "%s  %s @ %s  (%+.2f%%)",
                 label, strat.symbol.c_str(), pBuf, strat.closedPnlPct);
        PushToast(toastBuf, color, 8.f);

        spdlog::info("[Strategy] {} {} @ {:.4f} (entry={:.4f})",
                    label, strat.symbol, price, strat.entryPrice);
    }

    // ── Insight Refresh ────────────────────────────────────────────────────────

    void UI::RefreshInsights()
    {
        // Skip if both AI and News are disabled
        if (!systemToggles_.ai && !systemToggles_.news) return;

        std::vector<std::string> symbols;
        for (auto& s : cachedStrategies_)
        {
            if (!s.IsActive()) continue;
            if (std::find(symbols.begin(), symbols.end(), s.symbol) == symbols.end())
                symbols.push_back(s.symbol);
        }

        if (symbols.empty()) return;

        insightsLoading_ = true;

        // Only fetch news if news system is enabled
        if (systemToggles_.news)
        {
            for (auto& sym : symbols)
                newsService_->FetchNewsAsync(sym, 5);
        }

        spdlog::info("[UI] Refreshing insights for {} symbols", symbols.size());
    }

    // ── TickUI ─────────────────────────────────────────────────────────────────

    void UI::TickUI()
    {
        TickMarketRefresh();
        TickStrategyReload();
        TickPortfolioSampler();
        CheckStrategyTriggers();
        TickPriceCache();
        TickInsightRefresh();

        // Clear shared crosshair each frame
        sharedCrosshair_.Clear();

        // Push strategies to charts
        PushDataToCharts();

        ShowDockSpace();

        wizard_.BeginFrame();

        if (showDashboard_)       dashboardPanel_->Draw(&showDashboard_);
        if (showServerLauncher_)  serverLauncherPanel_->Draw(&showServerLauncher_);
        if (showThreadsDebugger_) ShowThreadsDebugger();
        if (showStockCharts_)     ShowStockCharts();
        if (showStrategyWizard_)  ShowStrategyWizard();
        if (showStrategies_)      strategyTable_->DrawWindow(&showStrategies_);
        if (showPortfolio_)       portfolioPanel_->Draw(&showPortfolio_);
        if (showMarketSignals_)   signalsPanel_->Draw(&showMarketSignals_);
        if (showRecommendations_) recsPanel_->Draw(&showRecommendations_);
        if (showMarketWarnings_)  warningsPanel_->Draw(&showMarketWarnings_);
        if (showAIOperations_)    aiOpsPanel_->Draw(&showAIOperations_);
        if (showGraphEvents_)     graphEventsPanel_->Draw(&showGraphEvents_);

        RenderDetachedCharts();

        if (showOptions_) ShowOptions();

        if (showStyleEditor_)
        {
            ImGui::Begin("Style Editor", &showStyleEditor_);
            ImGui::ShowStyleEditor();
            ImGui::End();
        }

        RenderToasts();
    }

    void UI::TickMarketRefresh()
    {
        if (charts_.empty()) return;

        float dt = ImGui::GetIO().DeltaTime;
        for (auto& panel : charts_)
        {
            if (panel.loading || panel.quote.candles.empty())
                continue;

            auto& tf = kTimeframes[panel.timeframeIdx];

            if (tf.realtime)
            {
                float cached = marketService_->GetCachedPrice(panel.symbol);
                if (cached > 0.f && !panel.quote.candles.empty())
                {
                    auto& last = panel.quote.candles.back();
                    last.close = cached;
                    if (cached > last.high) last.high = cached;
                    if (cached < last.low)  last.low  = cached;
                    panel.chart.SetData(panel.quote);
                }

                if (!panel.refreshing)
                {
                    panel.refreshTimer -= dt;
                    if (panel.refreshTimer <= 0.f)
                    {
                        panel.refreshing = true;
                        marketService_->FetchQuoteAsync(panel.symbol, "1m", tf.range);
                        panel.refreshTimer = 30.f;
                    }
                }
            }
            else
            {
                if (panel.refreshing) continue;
                float effectiveInterval = (float)MarketHours::GetPollInterval(panel.symbol);
                effectiveInterval = std::max(effectiveInterval, marketRefreshInterval_);

                panel.refreshTimer -= dt;
                if (panel.refreshTimer <= 0.f)
                {
                    panel.refreshing = true;
                    marketService_->FetchQuoteAsync(panel.symbol, tf.interval, tf.range);
                    panel.refreshTimer = effectiveInterval;
                }
            }
        }

        // Auto-refresh detached charts
        for (auto& dc : detachedCharts_)
        {
            if (dc.symbol.empty() || dc.refreshing || dc.quote.candles.empty())
                continue;
            float effectiveInterval = (float)MarketHours::GetPollInterval(dc.symbol);
            effectiveInterval = std::max(effectiveInterval, marketRefreshInterval_);
            dc.refreshTimer -= ImGui::GetIO().DeltaTime;
            if (dc.refreshTimer <= 0.f)
            {
                dc.refreshing = true;
                auto& tf = kTimeframes[dc.timeframeIdx];
                const char* interval = tf.realtime ? "1m" : tf.interval;
                marketService_->FetchQuoteAsync(dc.symbol, interval, tf.range);
                dc.refreshTimer = effectiveInterval;
            }
        }

        // Update global timer display
        float minTimer = 9999.f;
        for (auto& p : charts_)
            if (p.refreshTimer < minTimer) minTimer = p.refreshTimer;
        marketRefreshTimer_ = minTimer;
    }

    void UI::TickStrategyReload()
    {
        if (strategiesDirty_ && service_->IsConnected())
        {
            cachedStrategies_ = service_->GetAllStrategies();
            strategiesDirty_  = false;
        }
    }

    void UI::TickPortfolioSampler()
    {
        portfolioSampleTimer_ -= ImGui::GetIO().DeltaTime;
        if (portfolioSampleTimer_ > 0.f) return;

        portfolioSampleTimer_ = ui::kPortfolioSampleSec;
        float usdVal = 0.f, mxnVal = 0.f;
        for (auto& s : cachedStrategies_)
        {
            if (!s.IsActive() || s.quantity <= 0.f) continue;
            float price = GetCurrentPrice(s.symbol);
            if (price <= 0.f) continue;
            float val = s.EffectiveEntryPrice() * s.quantity + s.UnrealizedPnL(price);
            if (MarketHours::ClassifySymbol(s.symbol) == MarketType::Mexico)
                mxnVal += val;
            else
                usdVal += val;
        }
        portfolioHistory_.push_back({telemetry_.uptimeSec, usdVal, mxnVal});
        if ((int)portfolioHistory_.size() > ui::kPortfolioMaxSamples)
            portfolioHistory_.erase(portfolioHistory_.begin());
    }

    void UI::TickPriceCache()
    {
        priceCacheTimer_ -= ImGui::GetIO().DeltaTime;
        if (priceCacheTimer_ > 0.f) return;

        priceCacheTimer_ = priceCacheInterval_;

        // Ensure all active strategy symbols have real-time polling running.
        // StartRealtimePolling is a no-op if already polling for a symbol.
        for (auto& s : cachedStrategies_)
        {
            if (s.symbol.empty() || !s.IsActive()) continue;
            if (!marketService_->IsRealtimePolling(s.symbol))
                marketService_->StartRealtimePolling(s.symbol);
        }
    }

    void UI::TickInsightRefresh()
    {
        if (!systemToggles_.ai && !systemToggles_.news) return;

        insightRefreshTimer_ -= ImGui::GetIO().DeltaTime;
        if (insightRefreshTimer_ <= 0.f)
        {
            RefreshInsights();
            insightRefreshTimer_ = insightRefreshInterval_;
        }
    }

    void UI::PushDataToCharts()
    {
        auto wireChartData = [&](StockChart& chart, const std::string& symbol) {
            chart.SetSharedCrosshair(&sharedCrosshair_);

            std::vector<Strategy> forSymbol;
            for (auto& s : cachedStrategies_)
                if (s.symbol == symbol)
                    forSymbol.push_back(s);
            chart.SetStrategies(forSymbol);
            chart.SetEventMarkers(graphEvents_.GetEvents(symbol));

            auto* sl = chart.GetStrategyLayer();
            if (sl)
            {
                sl->SetCurrentPrice(GetCurrentPrice(symbol));
                sl->SetHighlightedId(hoveredStrategyId_);
            }
        };

        for (auto& panel : charts_)
            wireChartData(panel.chart, panel.symbol);
        for (auto& dc : detachedCharts_)
            wireChartData(dc.chart, dc.symbol);
    }

    void UI::UpdateUI()
    {
        DrainAsyncResults();
        TickUI();
    }

    // ── Dock Space & Menu ──────────────────────────────────────────────────────

    void UI::DrawActivityBar(DockSide /*side*/)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float barW = kActivityBarWidth;
        float btnR = (barW - 6.f) * 0.5f; // circle radius

        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(barW, vp->WorkSize.y));
        ImGui::SetNextWindowViewport(vp->ID);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.f, 4.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(ui::kABBarBg));

        ImGui::Begin("##ActivityBar", nullptr, flags);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        for (int i = 0; i < (int)activityPanels_.size(); ++i)
        {
            auto& p = activityPanels_[i];
            bool active = *p.visible;

            ImGui::PushID(i);

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec2 center(cursor.x + barW * 0.5f - 1.5f, cursor.y + btnR);

            // Draw colored circle
            ImU32 circleCol = active ? p.color : ui::ABDimColor(p.color);
            dl->AddCircleFilled(center, btnR, circleCol);

            // Active: bright border ring
            if (active)
                dl->AddCircle(center, btnR + 1.f, ui::kABRingActive, 0, 1.5f);

            // Draw initial letter centered on the circle
            const char* txt = p.icon;
            ImVec2 textSize = ImGui::CalcTextSize(txt);
            ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
            dl->AddText(textPos, active ? ui::kABTextActive : ui::kABTextDim, txt);

            // Invisible button on top for interaction
            ImGui::InvisibleButton("##ab", ImVec2(barW - 6.f, btnR * 2.f));
            if (ImGui::IsItemClicked())
                *p.visible = !*p.visible;

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", p.name);

            ImGui::Spacing();
            ImGui::PopID();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    void UI::ShowDockSpace()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        float barW = kActivityBarWidth;

        // Single activity bar on the left edge
        DrawActivityBar(DockSide::Left);

        // Main dockspace — inset left to make room for activity bar
        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + barW, viewport->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - barW, viewport->WorkSize.y));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("##StnksDockSpace", nullptr, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("StnksDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        if (!dockLayoutBuilt_)
        {
            dockLayoutBuilt_ = true;
            ImVec2 dockSize(viewport->WorkSize.x - barW, viewport->WorkSize.y);

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, dockSize);

            ImGuiID dockCenter, dockRight;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);

            ImGuiID dockCenterTop, dockCenterBottom;
            ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.38f, &dockCenterBottom, &dockCenterTop);

            ImGuiID dockRightTop, dockRightBottom;
            ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f, &dockRightBottom, &dockRightTop);

            ImGui::DockBuilderDockWindow("Stock Charts",        dockCenterTop);
            ImGui::DockBuilderDockWindow("Strategies",          dockCenterBottom);
            ImGui::DockBuilderDockWindow("Portfolio",           dockCenterBottom);
            ImGui::DockBuilderDockWindow("###GraphEvents",      dockCenterBottom);
            ImGui::DockBuilderDockWindow("###MarketSignals",    dockCenterBottom);

            ImGui::DockBuilderDockWindow("###StrategyWizard",   dockRightTop);
            ImGui::DockBuilderDockWindow("Dashboard",           dockRightBottom);
            ImGui::DockBuilderDockWindow("Recommendations",     dockRightBottom);
            ImGui::DockBuilderDockWindow("Market Warnings",     dockRightBottom);
            ImGui::DockBuilderDockWindow("AI Operations",       dockRightBottom);
            ImGui::DockBuilderDockWindow("Server Launcher",     dockRightBottom);
            ImGui::DockBuilderDockWindow("Threads Debugger",    dockRightBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        ShowMenuBar();
        ImGui::End();
    }

    void UI::ShowMenuBar()
    {
        if (!ImGui::BeginMenuBar()) return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Options..."))
                showOptions_ = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Dashboard",        nullptr, &showDashboard_);
            ImGui::MenuItem("Stock Charts",     nullptr, &showStockCharts_);
            ImGui::MenuItem("Strategy Wizard",  nullptr, &showStrategyWizard_);
            ImGui::MenuItem("Strategies",       nullptr, &showStrategies_);
            ImGui::MenuItem("Portfolio",        nullptr, &showPortfolio_);
            ImGui::MenuItem("Market Signals",   nullptr, &showMarketSignals_);
            ImGui::Separator();
            ImGui::MenuItem("Recommendations",  nullptr, &showRecommendations_);
            ImGui::MenuItem("Market Warnings",  nullptr, &showMarketWarnings_);
            ImGui::MenuItem("AI Operations",    nullptr, &showAIOperations_);
            ImGui::MenuItem("Graph Events",     nullptr, &showGraphEvents_);
            ImGui::MenuItem("Server Launcher",  nullptr, &showServerLauncher_);
            ImGui::Separator();
            if (ImGui::MenuItem("New Chart Window"))
            {
                auto dc = DetachedChart::Create("", StockQuote{});
                WireDetachedChart(dc);
                detachedCharts_.push_back(std::move(dc));
            }
            if (ImGui::BeginMenu("New Indicator Window"))
            {
                const char* indicators[] = { "Candles", "Volume", "RSI", "MACD" };
                for (auto& ind : indicators)
                {
                    if (ImGui::MenuItem(ind))
                    {
                        auto dc = DetachedChart::Create("", StockQuote{}, ind);
                        WireDetachedChart(dc);
                        detachedCharts_.push_back(std::move(dc));
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem("Threads Debugger", nullptr, &showThreadsDebugger_);
            ImGui::MenuItem("Style Editor",     nullptr, &showStyleEditor_);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
            {
                dockLayoutBuilt_ = false;
                showDashboard_ = showStockCharts_ = showStrategyWizard_ = true;
                showStrategies_ = showPortfolio_ = showMarketSignals_ = true;
                showRecommendations_ = showMarketWarnings_ = showAIOperations_ = true;
                showGraphEvents_ = true;
                showServerLauncher_ = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Markets"))
        {
            if (ImGui::BeginMenu("US Stocks"))
            {
                for (auto& sym : kPresetUS)
                    if (ImGui::MenuItem(sym)) FetchSymbol(sym);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Mexican Stocks (BMV)"))
            {
                for (auto& sym : kPresetMEX)
                    if (ImGui::MenuItem(sym)) FetchSymbol(sym);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Crypto"))
            {
                for (auto& sym : kPresetCrypto)
                    if (ImGui::MenuItem(sym)) FetchSymbol(sym);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        float fps = ImGui::GetIO().Framerate;
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "%.0f FPS", fps);
        float textWidth = ImGui::CalcTextSize(fpsText).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 20.f);
        ImGui::TextColored(
            fps > 30.f ? ImVec4(0.4f, 0.8f, 0.4f, 1.f) : ImVec4(0.9f, 0.3f, 0.3f, 1.f),
            "%s", fpsText);

        ImGui::EndMenuBar();
    }

    // ── Strategy Wizard (Dockable) ─────────────────────────────────────────────

    void UI::ShowStrategyWizard()
    {
        const std::vector<Candle>* candles = nullptr;
        int focusedCandle = -1;
        int totalCandles = 0;

        const std::string& wizSymbol = wizard_.GetStrategy().symbol;
        for (auto& panel : charts_)
        {
            if (!panel.quote.candles.empty() &&
                (panel.symbol == wizSymbol || wizSymbol.empty()))
            {
                candles = &panel.quote.candles;
                focusedCandle = panel.chart.GetFocusedCandle();
                totalCandles = panel.chart.GetCandleCount();
                break;
            }
        }
        if (!candles)
        {
            for (auto& panel : charts_)
            {
                if (!panel.quote.candles.empty())
                {
                    candles = &panel.quote.candles;
                    focusedCandle = panel.chart.GetFocusedCandle();
                    totalCandles = panel.chart.GetCandleCount();
                    break;
                }
            }
        }

        wizard_.DrawDockable(&showStrategyWizard_, candles, focusedCandle, totalCandles);

        if (wizard_.WasConfirmed())
        {
            Strategy result = wizard_.GetStrategy();

            if (result.id == 0)
            {
                if (defaultBroker_ == BrokerSource::Auto)
                    result.broker = BrokerSourceFromName(lastSelectedSource_);
                else
                    result.broker = defaultBroker_;
            }

            bool handled = false;
            for (auto& panel : charts_)
            {
                auto* sl = panel.chart.GetStrategyLayer();
                if (!sl) continue;

                if (sl->GetEditingId() > 0 && sl->GetEditingId() == result.id)
                {
                    if (sl->onStrategyChanged)
                        sl->onStrategyChanged(result, false);
                    sl->StopEditing();
                    handled = true;
                    break;
                }
                else if (result.symbol == panel.symbol && result.id == 0)
                {
                    if (sl->onStrategyChanged)
                        sl->onStrategyChanged(result, true);
                    handled = true;
                    break;
                }
            }

            if (!handled && service_)
            {
                if (result.id > 0)
                {
                    if (!service_->UpdateStrategy(result))
                        PushToast("Failed to update strategy #" + std::to_string(result.id), ui::kToastError);
                }
                else
                {
                    if (service_->InsertStrategy(result) <= 0)
                        PushToast("Failed to create strategy for " + result.symbol, ui::kToastError);
                }
                strategiesDirty_ = true;
            }

            wizard_.ConsumeResult();
        }
        else if (wizard_.WasCancelled())
        {
            for (auto& panel : charts_)
            {
                auto* sl = panel.chart.GetStrategyLayer();
                if (sl && sl->GetEditingId() > 0)
                {
                    sl->StopEditing();
                    if (sl->onEditingDismissed)
                        sl->onEditingDismissed();
                }
            }
            wizard_.ConsumeResult();
        }
    }

    // ── Stock Charts ───────────────────────────────────────────────────────────

    void UI::ShowStockCharts()
    {
        ImGui::Begin("Stock Charts", &showStockCharts_,
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

        ShowSymbolSelector();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.f);

        ImGui::TextDisabled("Min interval:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.f);
        ImGui::InputFloat("##refreshRate", &marketRefreshInterval_, 0.f, 0.f, "%.0fs");
        marketRefreshInterval_ = std::max(1.f, marketRefreshInterval_);
        ImGui::SameLine();
        ImGui::TextDisabled("(next: %.0fs)", marketRefreshTimer_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Now"))
        {
            for (auto& p : charts_) p.refreshTimer = 0.f;
        }

        ImGui::Separator();

        if (ImGui::BeginTabBar("##ChartTabs", ImGuiTabBarFlags_AutoSelectNewTabs |
                                               ImGuiTabBarFlags_FittingPolicyScroll))
        {
            for (auto it = charts_.begin(); it != charts_.end();)
            {
                bool open = true;
                if (ImGui::BeginTabItem(it->symbol.c_str(), &open))
                {
                    DrawChartTab(*it);
                    ImGui::EndTabItem();
                }

                if (!open)
                {
                    if (kTimeframes[it->timeframeIdx].realtime)
                        marketService_->StopRealtimePolling(it->symbol);
                    it = charts_.erase(it);
                }
                else
                    ++it;
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void UI::DrawChartTab(ChartPanelData& panel)
    {
        ImGui::PushID(panel.symbol.c_str());

        // Timeframe selector
        for (int tf = 0; tf < kTimeframeCount; ++tf)
        {
            if (tf > 0) ImGui::SameLine(0.f, 2.f);
            bool selected = (panel.timeframeIdx == tf);
            if (selected)
            {
                ImVec4 btnCol = kTimeframes[tf].realtime ? ui::kTimeframeBtnRT : ui::kTimeframeBtnNorm;
                ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            }
            if (ImGui::SmallButton(kTimeframes[tf].label))
            {
                if (panel.timeframeIdx != tf)
                {
                    if (kTimeframes[panel.timeframeIdx].realtime)
                        marketService_->StopRealtimePolling(panel.symbol);

                    panel.timeframeIdx = tf;
                    panel.loading = true;
                    panel.quote = StockQuote{};
                    panel.chart = StockChart{};
                    const char* interval = kTimeframes[tf].realtime ? "1m" : kTimeframes[tf].interval;
                    marketService_->FetchQuoteAsync(panel.symbol, interval, kTimeframes[tf].range);

                    if (kTimeframes[tf].realtime)
                        marketService_->StartRealtimePolling(panel.symbol);

                    panel.refreshTimer = 0.f;
                    spdlog::info("[UI] Switching {} to {}", panel.symbol, kTimeframes[tf].label);
                }
            }
            if (selected)
                ImGui::PopStyleColor();
        }

        DrawChartFreshnessIndicator(panel);
        ImGui::PopID();

        if (panel.loading)
        {
            ImGui::TextDisabled("Loading %s...", panel.symbol.c_str());
            float time = (float)ImGui::GetTime();
            const char* spinner = "|/-\\";
            ImGui::SameLine();
            ImGui::Text("%c", spinner[(int)(time * 4.f) % 4]);
        }
        else if (panel.quote.candles.empty())
        {
            ImGui::TextColored(ui::kColorBearish,
                               "Failed to load data for %s", panel.symbol.c_str());
        }
        else
        {
            panel.chart.Draw(("##chart_" + panel.symbol).c_str());
            DrawChartOverlay(panel);
        }
    }

    void UI::DrawChartFreshnessIndicator(const ChartPanelData& panel)
    {
        if (panel.quote.candles.empty() || panel.quote.fetchedAt <= 0) return;

        int64_t now = (int64_t)std::time(nullptr);
        int64_t age = now - panel.quote.fetchedAt;
        int64_t candleAge = now - panel.quote.candles.back().timestamp;

        ImVec4 color = ui::FreshnessColor((float)age);

        char ageBuf[16], candleBuf[16];
        ui::FormatDuration(ageBuf, sizeof(ageBuf), age);
        ui::FormatDuration(candleBuf, sizeof(candleBuf), candleAge);

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
        ImGui::TextColored(color, "Data: %s ago", ageBuf);
        ImGui::SameLine();
        ImGui::TextDisabled("| Last candle: %s ago", candleBuf);

        auto* source = marketService_->GetActiveSource();
        if (kTimeframes[panel.timeframeIdx].realtime)
        {
            ImGui::SameLine();
            auto* broker = marketService_->GetActiveBrokerSource();
            if (broker)
                ImGui::TextColored(ui::kColorConnected, "(RT: %s)", broker->GetName());
            else
                ImGui::TextColored(ui::kColorWarning, "(RT: no broker — using %s)",
                    source ? source->GetName() : "none");
        }
        else if (source && !source->IsRealtime())
        {
            ImGui::SameLine();
            ImGui::TextColored(ui::kColorDisabled,
                "(%s ~%dm delay)", source->GetName(), source->GetDelaySeconds() / 60);
        }

        MarketState mktState = MarketHours::GetState(panel.symbol);
        MarketType mktType = MarketHours::ClassifySymbol(panel.symbol);
        ImGui::SameLine();
        ImGui::TextColored(ui::MarketStateColor(mktState), "[%s %s]",
            MarketHours::MarketTypeToString(mktType),
            MarketHours::StateToString(mktState));
    }

    void UI::DrawChartOverlay(ChartPanelData& panel)
    {
        ImVec2 chartMin = ImGui::GetItemRectMin();
        ImVec2 chartMax = ImGui::GetItemRectMax();
        auto* stratLayer = panel.chart.GetStrategyLayer();
        if (!stratLayer) return;

        const std::vector<Candle>* candles = panel.quote.candles.empty()
            ? nullptr : &panel.quote.candles;

        wizard_.PreUpdate(chartMin, chartMax,
            panel.chart.GetFocusedCandle(),
            panel.chart.GetCandleCount(),
            candles);

        auto& layerWiz = stratLayer->GetWizard();
        layerWiz.PreUpdate(chartMin, chartMax,
            panel.chart.GetFocusedCandle(),
            panel.chart.GetCandleCount(),
            candles);

        // "+ Strategy" button overlay
        if (!wizard_.IsOpen() && !layerWiz.IsOpen())
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const char* btnLabel = "+ Strategy";
            ImVec2 textSz = ImGui::CalcTextSize(btnLabel);
            float pad = 6.f;
            ImVec2 btnMin(chartMin.x + 8.f, chartMin.y + 8.f);
            ImVec2 btnMax(btnMin.x + textSz.x + pad * 2.f, btnMin.y + textSz.y + pad * 2.f);

            bool hovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);
            ImU32 bgCol  = hovered ? IM_COL32(30, 50, 90, 230)  : IM_COL32(15, 20, 35, 200);
            ImU32 border = hovered ? IM_COL32(80, 130, 220, 255) : IM_COL32(50, 65, 100, 180);
            ImU32 textCol = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 190, 210, 220);

            dl->AddRectFilled(btnMin, btnMax, bgCol, 5.f);
            dl->AddRect(btnMin, btnMax, border, 5.f, 0, 1.2f);
            dl->AddText(ImVec2(btnMin.x + pad, btnMin.y + pad), textCol, btnLabel);

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                float price = panel.quote.candles.empty() ? 0.f : panel.quote.candles.back().close;
                wizard_.OpenCreate(panel.symbol, StrategyType::TPSL, StrategyDirection::Long, price);
                showStrategyWizard_ = true;
            }
        }

        stratLayer->DrawWizard(chartMin, chartMax,
            panel.chart.GetFocusedCandle(),
            panel.chart.GetCandleCount(),
            candles);
    }

    // ── Detached Charts ────────────────────────────────────────────────────────

    void UI::RenderDetachedCharts()
    {
        for (auto it = detachedCharts_.begin(); it != detachedCharts_.end();)
        {
            if (!it->Draw())
                it = detachedCharts_.erase(it);
            else
                ++it;
        }
    }

    void UI::WireDetachedChart(DetachedChart& dc)
    {
        dc.onSymbolChanged = [this](int dcId, const std::string& newSymbol) {
            // Find this detached chart's current timeframe
            int tfIdx = kDefaultTimeframe;
            for (auto& d : detachedCharts_)
                if (d.id == dcId) { tfIdx = d.timeframeIdx; break; }
            auto& tf = kTimeframes[tfIdx];
            const char* interval = tf.realtime ? "1m" : tf.interval;
            marketService_->FetchQuoteAsync(newSymbol, interval, tf.range);
            spdlog::info("[UI] Detached chart #{} changing to {} ({})", dcId, newSymbol, tf.label);
        };

        dc.onTimeframeChanged = [this](int dcId, const std::string& sym,
                                        const char* interval, const char* range) {
            marketService_->FetchQuoteAsync(sym, interval, range);
            spdlog::info("[UI] Detached chart #{} switching {} to {}/{}", dcId, sym, interval, range);
        };

        dc.getSourceInfo = [this]() -> DetachedChart::SourceInfo {
            DetachedChart::SourceInfo si{};
            auto* source = marketService_->GetActiveSource();
            if (source)
            {
                si.name     = source->GetName();
                si.realtime = source->IsRealtime();
                si.delaySec = source->GetDelaySeconds();
            }
            auto* broker = marketService_->GetActiveBrokerSource();
            if (broker)
                si.brokerName = broker->GetName();
            return si;
        };

        dc.onWireStrategyLayer = [this](StrategyLayer& sl, const std::string& sym) {
            WireStrategyLayerCallbacks(sl, sym);
        };

        dc.onWireTearOut = [this](StockChart& chart, const std::string& sym) {
            chart.onIndicatorTearOut = [this, sym](const std::string& indicatorName) {
                StockQuote q;
                for (auto& p : charts_)
                    if (p.symbol == sym) { q = p.quote; break; }
                if (q.candles.empty())
                    for (auto& d : detachedCharts_)
                        if (d.symbol == sym) { q = d.quote; break; }
                auto newDc = DetachedChart::Create(sym, q, indicatorName);
                WireDetachedChart(newDc);
                detachedCharts_.push_back(std::move(newDc));
            };
            chart.onDuplicateChart = [this, sym]() {
                StockQuote q;
                for (auto& p : charts_)
                    if (p.symbol == sym) { q = p.quote; break; }
                if (q.candles.empty())
                    for (auto& d : detachedCharts_)
                        if (d.symbol == sym) { q = d.quote; break; }
                auto newDc = DetachedChart::Create(sym, q);
                WireDetachedChart(newDc);
                detachedCharts_.push_back(std::move(newDc));
            };
        };
    }

    // ── Symbol Selector ────────────────────────────────────────────────────────

    void UI::ShowSymbolSelector()
    {
        // Default broker / data source combo
        {
            ImGui::TextDisabled("Source:");
            ImGui::SameLine();

            ImVec4 col = ui::BrokerColor(defaultBroker_);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(col.x * 0.15f, col.y * 0.15f, col.z * 0.15f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, 0.9f));
            ImGui::SetNextItemWidth(100.f);
            if (ImGui::BeginCombo("##defaultBroker", BrokerSourceToString(defaultBroker_)))
            {
                for (int i = 0; i < kBrokerSourceCount; ++i)
                {
                    auto src = static_cast<BrokerSource>(i);
                    ImVec4 c = ui::BrokerColor(src);
                    bool selected = (defaultBroker_ == src);
                    ImGui::PushStyleColor(ImGuiCol_Text, c);
                    if (ImGui::Selectable(BrokerSourceToString(src), selected))
                    {
                        defaultBroker_ = src;
                        switch (src)
                        {
                        case BrokerSource::Binance:    marketService_->SetActiveSource("Binance"); break;
                        case BrokerSource::GBM:        marketService_->SetActiveSource("GBM+"); break;
                        case BrokerSource::MetaTrader:  marketService_->SetActiveSource("MT5"); break;
                        default:                        marketService_->SetActiveSource("Yahoo Finance"); break;
                        }
                    }
                    ImGui::PopStyleColor();
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Broker & data source for new strategies");
                if (defaultBroker_ == BrokerSource::Auto)
                    ImGui::TextDisabled("Auto: broker is set from the search result you pick");
                else
                    ImGui::TextDisabled("Fixed: all new strategies use this broker");
                auto* active = marketService_->GetActiveSource();
                if (active)
                    ImGui::Text("Active source: %s%s", active->GetName(),
                        active->IsRealtime() ? " (real-time)" : " (~15min delay)");
                if (!lastSelectedSource_.empty())
                    ImGui::Text("Last selected: %s", lastSelectedSource_.c_str());
                ImGui::EndTooltip();
            }
        }

        ImGui::SameLine(0.f, 12.f);
        ImGui::TextDisabled("Search:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(280.f);
        bool changed = ImGui::InputText("##symbolSearch", searchInput_, sizeof(searchInput_));

        std::string currentQuery(searchInput_);
        if (changed && currentQuery.length() >= 2)
        {
            searchDebounceTimer_ = 0.3f;
            lastSearchQuery_ = currentQuery;
        }

        if (searchDebounceTimer_ > 0.f)
        {
            searchDebounceTimer_ -= ImGui::GetIO().DeltaTime;
            if (searchDebounceTimer_ <= 0.f && !lastSearchQuery_.empty())
            {
                marketService_->SearchSymbolsAsync(lastSearchQuery_);
                searchPending_ = true;
                searchDebounceTimer_ = 0.f;
            }
        }

        DrawSearchDropdown(currentQuery);

        if (currentQuery.length() < 2)
            searchResults_.clear();

        ImGui::SameLine();
        ImGui::TextDisabled("Scroll: navigate | Shift+Scroll: zoom | Middle-drag: pan");
    }

    void UI::DrawSearchDropdown(const std::string& currentQuery)
    {
        bool hasResults = !searchResults_.empty() && currentQuery.length() >= 2;
        if (!hasResults && !searchPending_) return;

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
        ImGui::SetNextWindowSize(ImVec2(460.f, 0.f));

        ImGuiWindowFlags popupFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin("##SymbolDropdown", nullptr, popupFlags);

        if (searchPending_ && searchResults_.empty())
            ImGui::TextDisabled("Searching...");

        for (auto& match : searchResults_)
        {
            ImGui::PushID(match.symbol.c_str());

            ImVec4 badgeCol = ui::SourceNameColor(match.source);

            auto SourceTag = [](const std::string& src) -> const char* {
                if (src == "Binance")        return "BIN";
                if (src == "GBM+")           return "GBM";
                if (src == "MT5")            return "MT5";
                if (src == "Yahoo Finance")  return "YHO";
                return "???";
            };

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const char* tag = SourceTag(match.source);
            ImVec2 tagSize = ImGui::CalcTextSize(tag);
            float padX = 4.f, padY = 1.f;
            ImVec2 badgeMin(cursor.x, cursor.y + 1.f);
            ImVec2 badgeMax(badgeMin.x + tagSize.x + padX * 2.f, badgeMin.y + tagSize.y + padY * 2.f);
            dl->AddRectFilled(badgeMin, badgeMax,
                IM_COL32((int)(badgeCol.x*255*0.3f), (int)(badgeCol.y*255*0.3f), (int)(badgeCol.z*255*0.3f), 200), 3.f);
            dl->AddRect(badgeMin, badgeMax,
                IM_COL32((int)(badgeCol.x*255), (int)(badgeCol.y*255), (int)(badgeCol.z*255), 140), 3.f);
            dl->AddText(ImVec2(badgeMin.x + padX, badgeMin.y + padY),
                IM_COL32((int)(badgeCol.x*255), (int)(badgeCol.y*255), (int)(badgeCol.z*255), 255), tag);

            float badgeWidth = tagSize.x + padX * 2.f + 6.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + badgeWidth);

            char label[256];
            snprintf(label, sizeof(label), "%-12s  %-28s  %-10s  %s",
                     match.symbol.c_str(), match.name.c_str(),
                     match.exchange.c_str(), match.type.c_str());

            if (ImGui::Selectable(label))
            {
                lastSelectedSource_ = match.source;
                FetchSymbol(match.symbol);
                searchResults_.clear();
                searchInput_[0] = '\0';
            }

            ImGui::PopID();
        }

        ImGui::End();
    }

    // ── FetchSymbol ────────────────────────────────────────────────────────────

    void UI::FetchSymbol(const std::string& symbol,
                         const char* interval,
                         const char* range)
    {
        telemetry_.marketFetches++;

        for (auto& panel : charts_)
        {
            if (panel.symbol == symbol)
            {
                panel.loading = true;
                panel.quote = StockQuote{};
                panel.chart = StockChart{};
                marketService_->FetchQuoteAsync(symbol, interval, range);
                spdlog::info("[UI] Re-fetching {} ({})", symbol, interval);
                return;
            }
        }

        ChartPanelData panel;
        panel.symbol  = symbol;
        panel.loading = true;
        charts_.push_back(std::move(panel));

        marketService_->FetchQuoteAsync(symbol, interval, range);
        spdlog::info("[UI] Fetching {} ({})", symbol, interval);
    }

    // ── Trade Execution ────────────────────────────────────────────────────────

    bool UI::ExecuteOrder(const std::string& symbol, BrokerSource broker,
                          OrderSide side, float quantity, float price)
    {
        std::string brokerName = BrokerSourceToString(broker);

        if (!liveTradingEnabled_)
        {
            spdlog::info("[DRY-RUN] {} {} x{:.4f} {} @ {:.4f} (broker: {})",
                         OrderSideStr(side), symbol, quantity, price > 0.f ? "LIMIT" : "MARKET",
                         price, brokerName);
            PushToast("DRY-RUN: " + std::string(OrderSideStr(side)) + " " + symbol,
                      ui::kToastDryRun);
            return true;
        }

        IBrokerDataSource* ds = nullptr;
        if (broker != BrokerSource::Auto)
            ds = marketService_->FindBrokerSource(brokerName);
        else
            ds = marketService_->GetActiveBrokerSource();

        auto* connector = dynamic_cast<IBrokerConnector*>(ds);
        if (!connector)
        {
            spdlog::warn("[Trade] No broker connector for '{}' — cannot execute order", brokerName);
            PushToast("No broker for " + symbol + " — order not sent", ui::kToastWarning);
            return false;
        }

        if (connector->GetConnectionState() != WsState::Connected)
        {
            spdlog::warn("[Trade] Broker '{}' not connected", brokerName);
            PushToast("Broker disconnected — order not sent", ui::kToastError);
            return false;
        }

        OrderType orderType = (price > 0.f) ? OrderType::Limit : OrderType::Market;
        auto result = connector->PlaceOrder(symbol, side, orderType, quantity, price);

        if (result.status == OrderStatus::Rejected)
        {
            spdlog::error("[Trade] Order rejected: {}", result.errorMsg);
            PushToast("Order REJECTED: " + result.errorMsg, ui::kToastError);
            return false;
        }

        spdlog::info("[Trade] {} {} {} x{:.4f} — orderId: {}",
                     OrderSideStr(side), symbol, OrderTypeStr(orderType), quantity, result.orderId);
        PushToast(std::string(OrderSideStr(side)) + " " + symbol + " sent (ID: " + result.orderId + ")",
                  ui::kToastSuccess);
        return true;
    }

    bool UI::ExecuteAIOperation(AIOperation& op)
    {
        if (op.executed) return false;
        if (op.type == OperationType::Hold) return false;

        OrderSide side = (op.type == OperationType::Buy) ? OrderSide::Buy : OrderSide::Sell;
        BrokerSource broker = BrokerSource::Auto;
        float quantity = 0.f;

        if (op.strategyId > 0)
        {
            for (auto& s : cachedStrategies_)
            {
                if (s.id == op.strategyId)
                {
                    broker = s.broker;
                    quantity = s.quantity;
                    break;
                }
            }
        }

        if (op.strategyId == 0)
            broker = defaultBroker_;
        if (quantity <= 0.f) quantity = 1.f;

        float price = (op.type == OperationType::AdjustTP || op.type == OperationType::AdjustSL)
                    ? op.suggestedPrice : 0.f;

        bool ok = ExecuteOrder(op.symbol, broker, side, quantity, price);
        if (ok) op.executed = true;
        return ok;
    }

    bool UI::ExecuteStrategyTrigger(const Strategy& strategy, StrategyStatus trigger, float exitPrice)
    {
        OrderSide side = (strategy.direction == StrategyDirection::Long)
                       ? OrderSide::Sell : OrderSide::Buy;
        float quantity = strategy.quantity > 0.f ? strategy.quantity : 1.f;

        spdlog::info("[Trigger] {} {} for {} @ {:.4f} (strategy #{})",
                     trigger == StrategyStatus::TPHit ? "TP" : "SL",
                     OrderSideStr(side), strategy.symbol, exitPrice, strategy.id);

        return ExecuteOrder(strategy.symbol, strategy.broker, side, quantity);
    }

    // ── ViewStrategy ───────────────────────────────────────────────────────────

    void UI::ViewStrategy(const Strategy& s)
    {
        float lo = s.entryPrice, hi = s.entryPrice;
        if (s.takeProfit > 0.f) { lo = std::min(lo, s.takeProfit); hi = std::max(hi, s.takeProfit); }
        if (s.stopLoss > 0.f)   { lo = std::min(lo, s.stopLoss);   hi = std::max(hi, s.stopLoss); }

        int64_t now = std::time(nullptr);
        int64_t created = s.createdAt > 0 ? s.createdAt : (s.entryDate > 0 ? s.entryDate : now);
        int64_t ageSec = now - created;

        const char* interval = "1d";
        const char* range    = "6mo";
        int timeframeIdx     = kDefaultTimeframe;

        if (ageSec < 3600 * 4)           { interval = "1m";  range = "1d";  timeframeIdx = 0; }
        else if (ageSec < 86400)          { interval = "5m";  range = "5d";  timeframeIdx = 2; }
        else if (ageSec < 86400 * 5)      { interval = "15m"; range = "5d";  timeframeIdx = 3; }
        else if (ageSec < 86400 * 30)     { interval = "1h";  range = "1mo"; timeframeIdx = 5; }
        else if (ageSec < 86400 * 90)     { interval = "1d";  range = "6mo"; timeframeIdx = 7; }

        pendingView_.active = true;
        pendingView_.symbol = s.symbol;
        pendingView_.priceLo = lo;
        pendingView_.priceHi = hi;

        for (auto& panel : charts_)
        {
            if (panel.symbol == s.symbol && !panel.loading && !panel.quote.candles.empty())
            {
                panel.timeframeIdx = timeframeIdx;
                panel.chart.FocusOnPriceRange(lo, hi);
                pendingView_.active = false;

                auto* sl = panel.chart.GetStrategyLayer();
                if (sl) sl->StartEditing(s.id);
                wizard_.OpenEdit(s);
                showStrategyWizard_ = true;
                showStockCharts_ = true;
                return;
            }
        }

        FetchSymbol(s.symbol, interval, range);
        if (!charts_.empty() && charts_.back().symbol == s.symbol)
            charts_.back().timeframeIdx = timeframeIdx;
    }

    // ── GetCurrentPrice ────────────────────────────────────────────────────────

    float UI::GetCurrentPrice(const std::string& symbol) const
    {
        // Try cached price first (from broker / RT polling)
        float cached = marketService_->GetCachedPrice(symbol);
        if (cached > 0.f) return cached;

        // Fallback to last candle close from any chart
        for (auto& panel : charts_)
        {
            if (panel.symbol == symbol && !panel.quote.candles.empty())
                return panel.quote.candles.back().close;
        }
        for (auto& dc : detachedCharts_)
        {
            if (dc.symbol == symbol && !dc.quote.candles.empty())
                return dc.quote.candles.back().close;
        }
        return 0.f;
    }

    // ── Options ────────────────────────────────────────────────────────────────

    void UI::ShowOptions()
    {
        ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Options", &showOptions_))
        {
            ImGui::End();
            return;
        }

        auto* app = engine_->appInstance_.get();
        auto& globals = engine_->globals_;
        bool changed = false;

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int wm = static_cast<int>(app->GetWindowMode());
            const char* modeLabels[] = { "Windowed", "Fullscreen", "Borderless Fullscreen" };
            if (ImGui::Combo("Window Mode", &wm, modeLabels, 3))
            {
                app->SetWindowMode(static_cast<WindowMode>(wm));
                if (globals)
                    globals->Set(gk::prefix::APP, gk::key::WINDOW_MODE, std::to_string(wm));
                changed = true;
            }

            if (app->GetWindowMode() == WindowMode::Windowed)
            {
                glm::ivec2 sz = app->GetMainWindowSize();
                int res[2] = { sz.x, sz.y };
                const char* presets[] = {
                    "Custom", "1280x720", "1366x768", "1600x900",
                    "1920x1080", "2560x1440", "3840x2160"
                };
                const int presetW[] = { 0, 1280, 1366, 1600, 1920, 2560, 3840 };
                const int presetH[] = { 0, 720, 768, 900, 1080, 1440, 2160 };
                int presetIdx = 0;
                for (int i = 1; i < 7; ++i)
                    if (res[0] == presetW[i] && res[1] == presetH[i]) { presetIdx = i; break; }

                if (ImGui::Combo("Resolution", &presetIdx, presets, 7))
                {
                    if (presetIdx > 0)
                    {
                        res[0] = presetW[presetIdx]; res[1] = presetH[presetIdx];
                        app->SetResolution(res[0], res[1]);
                        if (globals)
                        {
                            globals->Set(gk::prefix::APP, gk::key::RESOLUTION_W, std::to_string(res[0]));
                            globals->Set(gk::prefix::APP, gk::key::RESOLUTION_H, std::to_string(res[1]));
                        }
                        changed = true;
                    }
                }
                if (presetIdx == 0 && ImGui::InputInt2("Width x Height", res))
                {
                    res[0] = std::max(640, res[0]); res[1] = std::max(480, res[1]);
                    app->SetResolution(res[0], res[1]);
                    if (globals)
                    {
                        globals->Set(gk::prefix::APP, gk::key::RESOLUTION_W, std::to_string(res[0]));
                        globals->Set(gk::prefix::APP, gk::key::RESOLUTION_H, std::to_string(res[1]));
                    }
                    changed = true;
                }
            }
            else
            {
                glm::ivec2 sz = app->GetMainWindowSize();
                ImGui::TextDisabled("Current: %dx%d (native)", sz.x, sz.y);
            }
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool vsync = app->GetVSync();
            if (ImGui::Checkbox("VSync", &vsync))
            {
                app->SetVSync(vsync);
                if (globals) globals->Set(gk::prefix::APP, gk::key::VSYNC, vsync ? "1" : "0");
                changed = true;
            }
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Syncs rendering to monitor refresh rate.\nReduces tearing but may increase input latency.");

            int fpsTarget = app->GetFPSTarget();
            const char* fpsLabels[] = { "Unlimited", "30", "60", "120", "144", "240" };
            const int   fpsValues[] = { 0, 30, 60, 120, 144, 240 };
            int fpsIdx = 0;
            for (int i = 1; i < 6; ++i)
                if (fpsTarget == fpsValues[i]) { fpsIdx = i; break; }
            if (ImGui::Combo("FPS Limit", &fpsIdx, fpsLabels, 6))
            {
                app->SetFPSTarget(fpsValues[fpsIdx]);
                if (globals) globals->Set(gk::prefix::APP, gk::key::FPS_TARGET, std::to_string(fpsValues[fpsIdx]));
                changed = true;
            }
            if (vsync) ImGui::TextDisabled("FPS limit has no effect while VSync is on.");

            ImGui::Separator();
            ImGui::TextDisabled("Performance Stats");
            float fps = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f (%.2f ms/frame)", fps, 1000.f / fps);
            auto& td = engine_->threadDebugInfo_;
            ImGui::Text("Update:  %.2f ms", td.updatePhaseMs);
            ImGui::Text("Render:  %.2f ms", td.presentPhaseMs);
            glm::ivec2 sz = app->GetMainWindowSize();
            ImGui::Text("Viewport: %dx%d", sz.x, sz.y);
        }

        if (ImGui::CollapsingHeader("Appearance"))
        {
            if (ImGui::Button("Open Style Editor")) showStyleEditor_ = true;
            ImGui::SameLine(); ImGui::TextDisabled("Full ImGui style customization");
            ImGui::Separator(); ImGui::TextDisabled("Quick Themes");
            if (ImGui::Button("Dark (Default)"))
            {
                ImGui::StyleColorsDark();
                ImVec4* colors = ImGui::GetStyle().Colors;
                colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
                colors[ImGuiCol_TitleBg]  = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Light")) ImGui::StyleColorsLight();
            ImGui::SameLine();
            if (ImGui::Button("Classic")) ImGui::StyleColorsClassic();
        }

        // Trading and Data & Connections settings are now in the Dashboard panel

        if (changed)
            ImGui::TextColored(ui::kColorConnected, "Settings saved.");

        ImGui::End();
    }

    // ── Threads Debugger ───────────────────────────────────────────────────────

    void UI::ShowThreadsDebugger()
    {
        ImGui::Begin("Threads Debugger", &showThreadsDebugger_);

        auto& td  = engine_->threadDebugInfo_;
        int   off = td.historyOffset;
        const float avail = ImGui::GetContentRegionAvail().x;

        DrawChannelTimingsTable(td, avail);
        DrawFrameTimeline(td, avail);
        DrawHistoryPlots(td, off, avail);

        ImGui::End();
    }

    void UI::DrawChannelTimingsTable(const ThreadDebugInfo& td, float /*avail*/)
    {
        ImGui::SeparatorText("Channel Timings");

        struct Row { const char* label; float ms; bool async; ImVec4 col; };
        std::vector<Row> allRows = {
            { "MAIN",       td.main.durationMs,       false, {0.30f, 0.65f, 1.00f, 1.f} },
            { "RENDERING",  td.rendering.durationMs,  false, {1.00f, 0.55f, 0.20f, 1.f} },
        };

        auto workerStatus = engine_->threadRegistry_.GetStatus();
        ImVec4 workerColors[] = {
            {0.40f, 0.90f, 0.80f, 1.f}, {0.55f, 0.85f, 0.40f, 1.f},
            {0.90f, 0.40f, 0.85f, 1.f}, {0.85f, 0.85f, 0.20f, 1.f},
            {0.90f, 0.55f, 0.55f, 1.f}, {0.55f, 0.55f, 0.90f, 1.f},
        };
        int colorIdx = 0;
        for (auto& w : workerStatus)
        {
            allRows.push_back({ w.name.c_str(), w.lastDurationMs, true, workerColors[colorIdx % 6] });
            colorIdx++;
        }

        if (ImGui::BeginTable("##channeltable", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, 120.f);
            ImGui::TableSetupColumn("ms",      ImGuiTableColumnFlags_WidthFixed,  60.f);
            ImGui::TableSetupColumn("Thread",  ImGuiTableColumnFlags_WidthFixed,  70.f);
            ImGui::TableSetupColumn("Bar",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            float maxMs = 0.1f;
            for (auto& r : allRows) maxMs = std::max(maxMs, r.ms);

            for (auto& r : allRows)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(r.col, "%s", r.label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", r.ms);
                ImGui::TableSetColumnIndex(2); ImGui::TextDisabled(r.async ? "worker" : "main");
                ImGui::TableSetColumnIndex(3);
                float barW = (r.ms / maxMs) * ImGui::GetContentRegionAvail().x;
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p, {p.x + barW, p.y + 12.f},
                    ImGui::ColorConvertFloat4ToU32(r.col), 2.f);
                ImGui::Dummy({ImGui::GetContentRegionAvail().x, 12.f});
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Text("Update:  %.2f ms  |  Present: %.2f ms  |  Frame: %.2f ms  (%.1f fps)",
                    td.updatePhaseMs, td.presentPhaseMs,
                    td.updatePhaseMs + td.presentPhaseMs,
                    (td.updatePhaseMs + td.presentPhaseMs) > 0.f
                        ? 1000.f / (td.updatePhaseMs + td.presentPhaseMs) : 0.f);
    }

    void UI::DrawFrameTimeline(const ThreadDebugInfo& td, float avail)
    {
        ImGui::SeparatorText("Frame Timeline");

        float total = td.updatePhaseMs + td.presentPhaseMs;
        if (total <= 0.f) { ImGui::TextDisabled("No frame data yet"); return; }

        const float tlW  = avail - 8.f;
        const float rowH = 20.f;
        const float gap  = 4.f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin  = ImGui::GetCursorScreenPos();

        auto drawSegment = [&](float xStart, float dur, ImVec4 col, const char* lbl, float rowY) {
            float x0 = origin.x + (xStart / total) * tlW;
            float x1 = origin.x + ((xStart + dur) / total) * tlW;
            if (x1 <= x0 + 1.f) x1 = x0 + 2.f;
            ImU32 c = ImGui::ColorConvertFloat4ToU32(col);
            dl->AddRectFilled({x0, rowY}, {x1, rowY + rowH}, c, 3.f);
            dl->AddRect({x0, rowY}, {x1, rowY + rowH}, IM_COL32(0,0,0,120), 3.f);
            ImVec2 tsz = ImGui::CalcTextSize(lbl);
            if (x1 - x0 > tsz.x + 4.f)
                dl->AddText({x0 + (x1 - x0 - tsz.x) * 0.5f, rowY + (rowH - tsz.y) * 0.5f},
                            IM_COL32(255,255,255,230), lbl);
        };

        float row0 = origin.y;
        float row1 = row0 + rowH + gap;

        drawSegment(0.f, td.main.durationMs, {0.30f, 0.65f, 1.00f, 0.9f}, "MAIN", row0);
        drawSegment(td.updatePhaseMs, td.rendering.durationMs, {1.00f, 0.55f, 0.20f, 0.9f}, "RENDER", row0);

        ImVec4 workerColors[] = {
            {0.40f, 0.90f, 0.80f, 0.9f}, {0.55f, 0.85f, 0.40f, 0.9f},
            {0.90f, 0.40f, 0.85f, 0.9f}, {0.85f, 0.85f, 0.20f, 0.9f},
            {0.90f, 0.55f, 0.55f, 0.9f}, {0.55f, 0.55f, 0.90f, 0.9f},
        };
        auto workerStatus = engine_->threadRegistry_.GetStatus();
        float workerOffset = 0.f;
        int colorIdx = 0;
        for (auto& w : workerStatus)
        {
            float dur = std::min(w.lastDurationMs, total);
            drawSegment(workerOffset, dur, workerColors[colorIdx % 6], w.name.c_str(), row1);
            workerOffset += dur;
            colorIdx++;
        }

        dl->AddText({origin.x, row0 + rowH + 2.f}, IM_COL32(180,180,180,160), "main");
        dl->AddText({origin.x, row1 + rowH + 2.f}, IM_COL32(180,180,180,160), "workers");
        ImGui::Dummy({tlW, rowH * 2.f + gap + 16.f});
    }

    void UI::DrawHistoryPlots(const ThreadDebugInfo& td, int off, float avail)
    {
        ImGui::SeparatorText("History");

        struct Plot { const char* lbl; const float* data; ImVec4 col; };
        Plot plots[] = {
            { "MAIN (ms)",      td.mainHistory.data(),      {0.30f, 0.65f, 1.00f, 1.f} },
            { "RENDERING (ms)", td.renderingHistory.data(), {1.00f, 0.55f, 0.20f, 1.f} },
            { "Frame (ms)",     td.frameHistory.data(),     {0.85f, 0.85f, 0.20f, 1.f} },
        };

        for (auto& p : plots)
        {
            float maxV = 0.1f;
            for (int i = 0; i < ThreadDebugInfo::kHistorySize; ++i)
                maxV = std::max(maxV, p.data[i]);
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.2f ms", p.data[
                (off + ThreadDebugInfo::kHistorySize - 1) % ThreadDebugInfo::kHistorySize]);
            ImGui::PushStyleColor(ImGuiCol_PlotLines,
                                  ImGui::ColorConvertFloat4ToU32(p.col));
            ImGui::PlotLines(p.lbl, p.data, ThreadDebugInfo::kHistorySize,
                             off, overlay, 0.f, maxV * 1.2f,
                             ImVec2(avail, 45.f));
            ImGui::PopStyleColor();
        }
    }

    // ── Toast Notifications ────────────────────────────────────────────────────

    void UI::PushToast(const std::string& msg, const ImVec4& color, float duration)
    {
        toasts_.push_back({msg, color, duration, duration});
    }

    void UI::RenderToasts()
    {
        if (toasts_.empty()) return;

        float dt = ImGui::GetIO().DeltaTime;
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;

        float menuBarHeight = ImGui::GetFrameHeight() + 4.f;
        float offsetY = menuBarHeight + 8.f;
        const float padding = 12.f;
        const float toastWidth = 360.f;

        for (int i = 0; i < (int)toasts_.size(); ++i)
        {
            auto& t = toasts_[i];
            t.lifetime -= dt;
            if (t.lifetime <= 0.f) continue;

            float alpha = 1.f;
            if (t.lifetime < 1.f)
                alpha = t.lifetime;
            else if (t.lifetime > t.maxLife - 0.3f)
                alpha = (t.maxLife - t.lifetime) / 0.3f;

            ImGui::SetNextWindowPos(
                ImVec2(12.f, offsetY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(toastWidth, 0.f));
            ImGui::SetNextWindowBgAlpha(0.85f * alpha);

            char winId[32];
            snprintf(winId, sizeof(winId), "##Toast%d", i);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(t.color.x, t.color.y, t.color.z, 0.6f * alpha));

            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;

            if (ImGui::Begin(winId, nullptr, flags))
            {
                // Colored accent bar on the left edge
                ImVec2 wMin = ImGui::GetWindowPos();
                ImVec2 wMax = ImVec2(wMin.x + 4.f, wMin.y + ImGui::GetWindowSize().y);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    wMin, wMax,
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(t.color.x, t.color.y, t.color.z, 0.9f * alpha)),
                    2.f);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.f);
                ImVec4 col = t.color;
                col.w = alpha;
                ImGui::TextColored(col, "%s", t.message.c_str());
            }
            ImGui::End();

            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(2);

            offsetY += ImGui::GetTextLineHeightWithSpacing() + padding * 2.f + 6.f;
        }

        toasts_.erase(
            std::remove_if(toasts_.begin(), toasts_.end(),
                [](const Toast& t) { return t.lifetime <= 0.f; }),
            toasts_.end());
    }

} // namespace stnks
