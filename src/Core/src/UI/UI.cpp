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
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <cstdio>

namespace stnks
{
    UI::UI(const std::shared_ptr<Engine>& engine) : engine_(engine) {}
    UI::~UI() = default;

    void UI::Init()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding    = 4.0f;
        style.FrameRounding     = 2.0f;
        style.GrabRounding      = 2.0f;
        style.ScrollbarRounding = 4.0f;
        style.TabRounding       = 3.0f;
        style.DockingSeparatorSize = 2.0f;

        // When viewports are enabled, undocked windows are native OS windows — no rounding
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f; // Opaque bg for OS windows
        }

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBg]        = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_Header]         = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
        colors[ImGuiCol_HeaderHovered]  = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
        colors[ImGuiCol_Button]         = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
        colors[ImGuiCol_ButtonHovered]  = ImVec4(0.20f, 0.28f, 0.40f, 1.00f);
        colors[ImGuiCol_Tab]            = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered]     = ImVec4(0.22f, 0.28f, 0.40f, 1.00f);
        colors[ImGuiCol_TabActive]      = ImVec4(0.16f, 0.20f, 0.30f, 1.00f);
        colors[ImGuiCol_DockingPreview] = ImVec4(0.22f, 0.35f, 0.55f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_Separator]      = ImVec4(0.18f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.25f, 0.35f, 0.55f, 1.00f);
        colors[ImGuiCol_SeparatorActive]  = ImVec4(0.30f, 0.45f, 0.65f, 1.00f);

        httpClient_    = std::make_unique<HttpClient>(engine_->threadRegistry_);
        marketService_ = std::make_unique<MarketService>(*httpClient_, engine_->threadRegistry_);

        // Strategy service: check STNKS_SERVER_URL env for remote mode, else monolith
        const char* serverUrl = std::getenv("STNKS_SERVER_URL");
        if (serverUrl==nullptr) serverUrl = "localhost:8099";
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

        // News service (GNews API key from env)
        const char* gnewsKey = std::getenv("GNEWS_API_KEY");
        // const char* gnewsKey = "58b092311e0d69f7cb02d73c1fa563b4";

        newsService_ = std::make_unique<NewsService>(
            *httpClient_, engine_->threadRegistry_,
            gnewsKey ? gnewsKey : "");

        // AI analyzer (Claude API key from env)
        ClaudeAnalyzer::Config aiConfig;
        const char* claudeKey = std::getenv("CLAUDE_API_KEY");
        if (claudeKey) aiConfig.apiKey = claudeKey;
        analyzer_ = std::make_unique<ClaudeAnalyzer>(*httpClient_, aiConfig);

        // Load persisted AI settings from globals
        if (engine_->globals_)
        {
            std::string val = engine_->globals_->Get(gk::prefix::STATE, gk::key::AI_AUTO_TRADE);
            aiAutoTrade_ = (val == "1");
        }

        spdlog::info("[UI] Initialized");
    }

    void UI::Update() {}

    void UI::DrainAsyncResults()
    {
        marketService_->DrainQuoteResults([this](QuoteFetchResult&& result) {
            telemetry_.chartRefreshes++;
            for (auto& panel : charts_)
            {
                // Background refresh — just update data, keep layers intact
                if (panel.symbol == result.symbol && panel.refreshing)
                {
                    panel.quote      = std::move(result.quote);
                    panel.refreshing = false;
                    panel.chart.SetData(panel.quote);
                    break;
                }

                if (panel.symbol == result.symbol && panel.loading)
                {
                    panel.quote   = std::move(result.quote);
                    panel.loading = false;

                    panel.chart.AddLayer<CandlestickLayer>();
                    auto& stratLayer = panel.chart.AddLayer<StrategyLayer>();
                    panel.chart.AddLayer<VolumeLayer>();
                    panel.chart.AddLayer<RSILayer>();
                    panel.chart.SetData(panel.quote);

                    // Wire strategy callbacks for visual creation/editing
                    stratLayer.onStrategyChanged = [this](const Strategy& s, bool isNew) {
                        if (isNew)
                        {
                            telemetry_.strategySaves++;
                            int64_t id = service_->InsertStrategy(s);
                            if (id > 0)
                                spdlog::info("[Strategy] Created #{} for {} (type={} entry={:.2f})",
                                             id, s.symbol, StrategyTypeToString(s.type), s.entryPrice);
                            else
                                spdlog::error("[Strategy] Failed to insert for {} (server: {})",
                                              s.symbol, service_->GetServerUrl());
                        }
                        else
                        {
                            bool ok = service_->UpdateStrategy(s);
                            if (ok)
                                spdlog::info("[Strategy] Updated #{} for {}", s.id, s.symbol);
                            else
                                spdlog::error("[Strategy] Failed to update #{}", s.id);

                            // Cancel any cell edit on the same row (gizmo takes priority)
                            if (editCellRowId_ == s.id)
                            {
                                editCellRowId_ = -1;
                                editCellCol_ = -1;
                            }
                        }
                        strategiesDirty_ = true;
                    };

                    stratLayer.onStrategyCancelled = [this](int64_t id) {
                        // Find strategy symbol to get exit price
                        float exitPrice = 0.f;
                        for (auto& s : cachedStrategies_)
                            if (s.id == id) { exitPrice = GetCurrentPrice(s.symbol); break; }
                        service_->CancelStrategy(id, exitPrice);
                        strategiesDirty_ = true;
                        spdlog::info("[Strategy] Cancelled #{}", id);
                    };

                    stratLayer.onStrategySelected = [this](int64_t id) {
                        selectedStrategyId_ = id;
                        focusStrategiesTab_ = true;
                        scrollToStrategy_   = true;
                        showStrategies_     = true;

                        // Select in table
                        tableSelection_.clear();
                        tableSelection_.insert(id);
                        lastClickedId_ = id;

                        // Also open in the dockable wizard panel
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

                    spdlog::info("[UI] Chart loaded for {}", panel.symbol);
                    break;
                }
            }
        });

        marketService_->DrainSearchResults([this](std::vector<SymbolMatch>&& matches) {
            searchResults_ = std::move(matches);
            searchPending_ = false;
        });

        // Drain news results
        newsService_->DrainResults([this](NewsFetchResult&& result) {
            if (!result.ok)
            {
                spdlog::warn("[News] Fetch failed for '{}': {}", result.symbol, result.error);
                return;
            }

            // Feed news into analyzer
            std::vector<Strategy> activeForSymbol;
            for (auto& s : cachedStrategies_)
                if (s.symbol == result.symbol && s.IsActive())
                    activeForSymbol.push_back(s);

            analyzer_->AnalyzeAsync(result.symbol, result.articles,
                                     activeForSymbol, engine_->threadRegistry_);
        });

        // Drain analysis results
        analyzer_->DrainResults([this](AnalysisResult&& result) {
            insightsLoading_ = false;
            if (!result.ok)
            {
                spdlog::warn("[AI] Analysis failed for '{}': {}", result.symbol, result.error);
                return;
            }

            // Merge into cached insights (replace per-symbol)
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

            // Collect operations (replace per-symbol)
            cachedOperations_.erase(
                std::remove_if(cachedOperations_.begin(), cachedOperations_.end(),
                    [&](const AIOperation& op) { return op.symbol == result.symbol; }),
                cachedOperations_.end());
            for (auto& op : result.operations)
                cachedOperations_.push_back(std::move(op));

            spdlog::info("[AI] Got {} recs + {} warnings + {} ops for '{}'",
                         result.recommendations.size(), result.warnings.size(),
                         result.operations.size(), result.symbol);
        });
    }

    void UI::CheckStrategyTriggers()
    {
        bool changed = false;

        for (auto& strat : cachedStrategies_)
        {
            if (!strat.IsActive() || !strat.IsTPSL()) continue;

            for (auto& panel : charts_)
            {
                if (panel.symbol != strat.symbol || panel.quote.candles.empty()) continue;

                float lastHigh  = panel.quote.candles.back().high;
                float lastLow   = panel.quote.candles.back().low;
                float lastClose = panel.quote.candles.back().close;

                bool tpHit = false;
                bool slHit = false;

                if (strat.direction == StrategyDirection::Long)
                {
                    tpHit = lastHigh >= strat.takeProfit;
                    slHit = lastLow  <= strat.stopLoss;
                }
                else
                {
                    tpHit = lastLow  <= strat.takeProfit;
                    slHit = lastHigh >= strat.stopLoss;
                }

                if (tpHit)
                {
                    int64_t now = std::time(nullptr);
                    strat.status      = StrategyStatus::TPHit;
                    strat.triggeredAt = now;
                    service_->UpdateStrategy(strat);
                    changed = true;
                    spdlog::info("[Strategy] TP HIT for {} @ {:.2f} (target {:.2f})",
                                strat.symbol, lastClose, strat.takeProfit);
                }
                else if (slHit)
                {
                    int64_t now = std::time(nullptr);
                    strat.status      = StrategyStatus::SLHit;
                    strat.triggeredAt = now;
                    service_->UpdateStrategy(strat);
                    changed = true;
                    spdlog::info("[Strategy] SL HIT for {} @ {:.2f} (stop {:.2f})",
                                strat.symbol, lastClose, strat.stopLoss);
                }

                break;
            }
        }

        if (changed)
            strategiesDirty_ = true;
    }

    void UI::RefreshInsights()
    {
        // Collect unique symbols from active strategies
        std::vector<std::string> symbols;
        for (auto& s : cachedStrategies_)
        {
            if (!s.IsActive()) continue;
            if (std::find(symbols.begin(), symbols.end(), s.symbol) == symbols.end())
                symbols.push_back(s.symbol);
        }

        if (symbols.empty()) return;

        insightsLoading_ = true;
        for (auto& sym : symbols)
            newsService_->FetchNewsAsync(sym, 5);

        spdlog::info("[UI] Refreshing insights for {} symbols", symbols.size());
    }

    void UI::UpdateUI()
    {
        DrainAsyncResults();

        // Auto-refresh market data — per-panel adaptive intervals
        if (!charts_.empty())
        {
            float dt = ImGui::GetIO().DeltaTime;
            for (auto& panel : charts_)
            {
                if (panel.loading || panel.refreshing || panel.quote.candles.empty())
                    continue;

                // Determine effective interval: market hours aware
                float effectiveInterval = (float)MarketHours::GetPollInterval(panel.symbol);
                // Allow user override (manual refresh rate) as minimum floor
                effectiveInterval = std::max(effectiveInterval, marketRefreshInterval_);

                panel.refreshTimer -= dt;
                if (panel.refreshTimer <= 0.f)
                {
                    auto& tf = kTimeframes[panel.timeframeIdx];
                    panel.refreshing = true;
                    marketService_->FetchQuoteAsync(panel.symbol, tf.interval, tf.range);
                    panel.refreshTimer = effectiveInterval;
                }
            }
            // Update global timer display (show minimum across panels)
            float minTimer = 9999.f;
            for (auto& p : charts_)
                if (p.refreshTimer < minTimer) minTimer = p.refreshTimer;
            marketRefreshTimer_ = minTimer;
        }

        // Auto-refresh strategy timer (skip when disconnected to avoid blocking)
        strategyRefreshTimer_ -= ImGui::GetIO().DeltaTime;
        if (strategyRefreshTimer_ <= 0.f && service_->IsConnected())
        {
            strategiesDirty_ = true;
            strategyRefreshTimer_ = strategyRefreshInterval_;
        }
        else if (strategyRefreshTimer_ <= 0.f)
        {
            strategyRefreshTimer_ = 2.f; // Retry check in 2s when disconnected
        }

        // Reload strategies from service if dirty (skip if disconnected to avoid blocking)
        if (strategiesDirty_ && service_->IsConnected())
        {
            cachedStrategies_ = service_->GetAllStrategies();
            strategiesDirty_  = false;
        }

        // Check if any active strategy's TP/SL was hit
        CheckStrategyTriggers();

        // Auto-refresh insights timer
        insightRefreshTimer_ -= ImGui::GetIO().DeltaTime;
        if (insightRefreshTimer_ <= 0.f)
        {
            RefreshInsights();
            insightRefreshTimer_ = insightRefreshInterval_;
        }

        // Push strategies to each chart's strategy layer
        for (auto& panel : charts_)
        {
            std::vector<Strategy> forSymbol;
            for (auto& s : cachedStrategies_)
                if (s.symbol == panel.symbol)
                    forSymbol.push_back(s);
            panel.chart.SetStrategies(forSymbol);

            // Set current price for position P&L rendering
            auto* sl = panel.chart.GetStrategyLayer();
            if (sl)
                sl->SetCurrentPrice(GetCurrentPrice(panel.symbol));
        }

        ShowDockSpace();

        // Reset wizard click detection for this frame
        wizard_.BeginFrame();

        if (showDashboard_)
            ShowDashboard();
        if (showThreadsDebugger_)
            ShowThreadsDebugger();
        if (showStockCharts_)
            ShowStockCharts();
        if (showStrategyWizard_)
            ShowStrategyWizard();
        if (showStrategies_)
            ShowStrategies();
        if (showPortfolio_)
            ShowPortfolio();
        if (showRecommendations_)
            ShowRecommendations();
        if (showMarketWarnings_)
            ShowMarketWarnings();
        if (showAIOperations_)
            ShowAIOperations();
    }

    // ── Dock Space & Menu ──────────────────────────────────────────────────────

    void UI::ShowDockSpace()
    {
        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("##StnksDockSpace", nullptr, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("StnksDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        // Build default layout on first run (matches UILAYOUT.png)
        if (!dockLayoutBuilt_)
        {
            dockLayoutBuilt_ = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            // ┌──────────┬────────────────────────┬──────────────┐
            // │Dashboard │ Stock Charts            │Strategy Wiz  │
            // │          │                         │              │
            // │          │                         ├──────────────┤
            // │          │                         │Recom|Warn|AI │
            // │          ├────────────────────────┤              │
            // │          │ Strategies | Portfolio  │              │
            // └──────────┴────────────────────────┴──────────────┘

            // Split: left (18%) | rest
            ImGuiID dockLeft, dockRest;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f, &dockLeft, &dockRest);

            // Split rest: center | right (22%)
            ImGuiID dockCenter, dockRight;
            ImGui::DockBuilderSplitNode(dockRest, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);

            // Split center: top (charts, 62%) | bottom (strategies/portfolio, 38%)
            ImGuiID dockCenterTop, dockCenterBottom;
            ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.38f, &dockCenterBottom, &dockCenterTop);

            // Split right: top (wizard, 55%) | bottom (AI panels, 45%)
            ImGuiID dockRightTop, dockRightBottom;
            ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f, &dockRightBottom, &dockRightTop);

            // Assign windows to docks
            ImGui::DockBuilderDockWindow("Dashboard",        dockLeft);
            ImGui::DockBuilderDockWindow("Threads Debugger", dockLeft);      // tabbed behind Dashboard

            ImGui::DockBuilderDockWindow("Stock Charts",     dockCenterTop);

            ImGui::DockBuilderDockWindow("Strategies",       dockCenterBottom);
            ImGui::DockBuilderDockWindow("Portfolio",         dockCenterBottom); // tabbed

            ImGui::DockBuilderDockWindow("Strategy Wizard",  dockRightTop);

            ImGui::DockBuilderDockWindow("Recommendations",  dockRightBottom);
            ImGui::DockBuilderDockWindow("Market Warnings",  dockRightBottom); // tabbed
            ImGui::DockBuilderDockWindow("AI Operations",    dockRightBottom); // tabbed

            ImGui::DockBuilderFinish(dockspaceId);
        }

        ShowMenuBar();

        ImGui::End();
    }

    void UI::ShowMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit"))
                {
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Dashboard", nullptr, &showDashboard_);
                ImGui::MenuItem("Stock Charts", nullptr, &showStockCharts_);
                ImGui::MenuItem("Strategy Wizard", nullptr, &showStrategyWizard_);
                ImGui::MenuItem("Strategies", nullptr, &showStrategies_);
                ImGui::MenuItem("Portfolio", nullptr, &showPortfolio_);
                ImGui::MenuItem("Recommendations", nullptr, &showRecommendations_);
                ImGui::MenuItem("Market Warnings", nullptr, &showMarketWarnings_);
                ImGui::MenuItem("AI Operations", nullptr, &showAIOperations_);
                ImGui::Separator();
                ImGui::MenuItem("Threads Debugger", nullptr, &showThreadsDebugger_);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                {
                    dockLayoutBuilt_ = false; // Rebuild on next frame
                    // Re-enable all panels
                    showDashboard_      = true;
                    showStockCharts_    = true;
                    showStrategyWizard_ = true;
                    showStrategies_     = true;
                    showPortfolio_      = true;
                    showRecommendations_ = true;
                    showMarketWarnings_ = true;
                    showAIOperations_   = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Markets"))
            {
                ImGui::TextDisabled("-- US Market --");
                for (auto& sym : kPresetUS)
                {
                    if (ImGui::MenuItem(sym))
                        FetchSymbol(sym);
                }
                ImGui::Separator();
                ImGui::TextDisabled("-- MEX Market --");
                for (auto& sym : kPresetMEX)
                {
                    if (ImGui::MenuItem(sym))
                        FetchSymbol(sym);
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
    }

    // ── Strategies Tab ─────────────────────────────────────────────────────────

    void UI::ShowStrategies()
    {
        // If a gizmo was clicked, focus this window
        // if (focusStrategiesTab_)
        // {
        //     ImGui::SetNextWindowFocus();
        //     focusStrategiesTab_ = false;
        // }

        ImGui::Begin("Strategies", &showStrategies_);

        ImGui::TextDisabled("Right-click on a chart to create a strategy visually");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 380.f);

        if (ImGui::SmallButton("Export CSV"))
            ExportCSV();
        ImGui::SameLine();
        if (ImGui::SmallButton("Import CSV"))
            ImportCSV();
        ImGui::SameLine();

        if (!tableSelection_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.f));
            char delLabel[32];
            snprintf(delLabel, sizeof(delLabel), "Del (%d)", (int)tableSelection_.size());
            if (ImGui::SmallButton(delLabel))
            {
                for (int64_t id : tableSelection_)
                {
                    bool active = false;
                    for (auto& s : cachedStrategies_)
                        if (s.id == id && s.IsActive()) { active = true; break; }

                    if (active)
                    {
                        float exitPrice = 0.f;
                        for (auto& s : cachedStrategies_)
                            if (s.id == id) { exitPrice = GetCurrentPrice(s.symbol); break; }
                        service_->CancelStrategy(id, exitPrice);
                        spdlog::info("[Strategy] Cancelled #{}", id);
                    }
                    else
                    {
                        service_->DeleteStrategy(id);
                        spdlog::info("[Strategy] Deleted #{}", id);
                    }
                }
                tableSelection_.clear();
                selectedStrategyId_ = -1;
                isEditingInline_ = false;
                editCellRowId_ = -1;
                strategiesDirty_ = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        if (ImGui::SmallButton("Refresh"))
        {
            strategiesDirty_ = true;
            strategyRefreshTimer_ = strategyRefreshInterval_;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##interval", &strategyRefreshInterval_, 5.f, 120.f, "%.0fs");
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0fs)", strategyRefreshTimer_);

        ImGui::Separator();

        // When a strategy is selected from chart, force the Active tab
        // bool forceActiveTab = (selectedStrategyId_ > 0);

        if (ImGui::BeginTabBar("##StratTabs"))
        {
            // ImGuiTabItemFlags activeFlags = forceActiveTab
            //     ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            ImGuiTabItemFlags activeFlags =  ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("Active", nullptr, activeFlags))
            {
                DrawStrategyTable([](const Strategy& s) { return s.IsActive(); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Positions"))
            {
                DrawStrategyTable([](const Strategy& s) { return s.IsPosition() && s.IsActive(); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("AI"))
            {
                DrawStrategyTable([](const Strategy& s) { return s.IsAI() && s.IsActive(); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Triggered"))
            {
                DrawStrategyTable([](const Strategy& s) {
                    return s.status == StrategyStatus::TPHit || s.status == StrategyStatus::SLHit;
                });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("All"))
            {
                DrawStrategyTable([](const Strategy&) { return true; });
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    // ── Portfolio ───────────────────────────────────────────────────────────────

    void UI::ShowPortfolio()
    {
        ImGui::Begin("Portfolio", &showPortfolio_);

        // Aggregate holdings by symbol from active positions/strategies
        struct Holding
        {
            std::string symbol;
            float       totalQty      = 0.f;
            float       avgEntry      = 0.f;
            float       currentPrice  = 0.f;
            float       totalCost     = 0.f;
            float       marketValue   = 0.f;
            float       pnl           = 0.f;
            float       pnlPct        = 0.f;
            int         activeStrats  = 0;
            int         aiStrats      = 0;
        };

        std::vector<Holding> holdings;
        std::unordered_map<std::string, size_t> symbolIdx;

        for (auto& s : cachedStrategies_)
        {
            if (!s.IsActive()) continue;
            if (s.quantity <= 0.f && !s.IsAI()) continue;

            auto it = symbolIdx.find(s.symbol);
            Holding* h;
            if (it == symbolIdx.end())
            {
                symbolIdx[s.symbol] = holdings.size();
                holdings.push_back({});
                h = &holdings.back();
                h->symbol = s.symbol;
            }
            else
            {
                h = &holdings[it->second];
            }

            h->totalCost += s.entryPrice * s.quantity;
            h->totalQty  += s.quantity;
            h->activeStrats++;
            if (s.IsAI()) h->aiStrats++;
        }

        // Compute current values using per-strategy P/L (respects direction)
        for (auto& h : holdings)
        {
            if (h.totalQty > 0.f)
                h.avgEntry = h.totalCost / h.totalQty;

            h.currentPrice = GetCurrentPrice(h.symbol);
            if (h.currentPrice > 0.f)
            {
                // Sum P/L per strategy to respect Long/Short direction
                h.pnl = 0.f;
                for (auto& s : cachedStrategies_)
                {
                    if (!s.IsActive() || s.symbol != h.symbol || s.quantity <= 0.f) continue;
                    h.pnl += s.UnrealizedPnL(h.currentPrice);
                }
                h.marketValue = h.totalCost + h.pnl;
                h.pnlPct = (h.totalCost > 0.f) ? (h.pnl / h.totalCost) * 100.f : 0.f;
            }
        }

        // Total portfolio value
        float totalValue = 0.f, totalCost = 0.f, totalPnl = 0.f;
        for (auto& h : holdings)
        {
            totalValue += h.marketValue;
            totalCost  += h.totalCost;
            totalPnl   += h.pnl;
        }
        float totalPnlPct = (totalCost > 0.f) ? (totalPnl / totalCost) * 100.f : 0.f;

        // Header
        ImVec4 totalCol = totalPnl >= 0.f
            ? ImVec4(0.2f, 0.85f, 0.4f, 1.f)
            : ImVec4(0.9f, 0.25f, 0.25f, 1.f);

        ImGui::Text("GBM Portfolio");
        ImGui::SameLine();
        ImGui::TextColored(totalCol, "$%.2f", totalValue);
        ImGui::SameLine();
        ImGui::TextColored(totalCol, "(%+.2f%%)", totalPnlPct);
        ImGui::Separator();

        if (holdings.empty())
        {
            ImGui::TextDisabled("No active positions with quantity.");
            ImGui::TextDisabled("Create Position or AI strategies with quantity > 0.");
            ImGui::End();
            return;
        }

        // Table
        ImGui::PushStyleColor(ImGuiCol_TableRowBg,    ImVec4(0.09f, 0.09f, 0.12f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.11f, 0.11f, 0.14f, 1.f));

        if (ImGui::BeginTable("##Portfolio", 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_HighlightHoveredColumn))
        {
            ImGui::TableSetupColumn("Symbol",   ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("Qty",      ImGuiTableColumnFlags_WidthFixed, 60.f);
            ImGui::TableSetupColumn("Avg Entry",ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableSetupColumn("Price",    ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableSetupColumn("Value",    ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("P/L",      ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("P/L%",     ImGuiTableColumnFlags_WidthFixed, 65.f);
            ImGui::TableSetupColumn("Strategies",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int ri = 0; ri < (int)holdings.size(); ++ri)
            {
                auto& h = holdings[ri];
                ImGui::TableNextRow();
                ImGui::PushID(ri);

                bool positive = h.pnl >= 0.f;
                ImVec4 pnlCol = positive
                    ? ImVec4(0.2f, 0.85f, 0.4f, 1.f)
                    : ImVec4(0.9f, 0.25f, 0.25f, 1.f);

                // Row hover via selectable
                ImGui::TableNextColumn();
                ImGui::Selectable("##prow", false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
                bool rowHovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::GetIO().MouseClickedCount[0] == 2)
                    FetchSymbol(h.symbol.c_str());
                ImGui::SameLine(0.f, 0.f);

                // Symbol color by market type
                auto mkt = MarketHours::ClassifySymbol(h.symbol);
                ImVec4 symCol;
                switch (mkt)
                {
                case MarketType::US:      symCol = ImVec4(0.90f, 0.92f, 0.96f, 1.f); break;
                case MarketType::Mexico:  symCol = ImVec4(0.95f, 0.75f, 0.25f, 1.f); break;
                case MarketType::Crypto:  symCol = ImVec4(0.30f, 0.85f, 0.90f, 1.f); break;
                default:                  symCol = ImVec4(0.70f, 0.70f, 0.70f, 1.f); break;
                }
                ImGui::TextColored(symCol, "%s", h.symbol.c_str());

                // Qty
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", h.totalQty);

                // Avg Entry
                ImGui::TableNextColumn();
                ImGui::Text("$%.2f", h.avgEntry);

                // Current Price
                ImGui::TableNextColumn();
                if (h.currentPrice > 0.f)
                    ImGui::TextColored(pnlCol, "$%.2f", h.currentPrice);
                else
                    ImGui::TextDisabled("--");

                // Market Value
                ImGui::TableNextColumn();
                ImGui::Text("$%.2f", h.marketValue);

                // P/L
                ImGui::TableNextColumn();
                ImGui::TextColored(pnlCol, "%+.2f", h.pnl);

                // P/L%
                ImGui::TableNextColumn();
                ImGui::TextColored(pnlCol, "%+.2f%%", h.pnlPct);

                // Strategies info
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d active", h.activeStrats);
                if (h.aiStrats > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.9f, 0.5f, 1.f, 1.f), "(%d AI)", h.aiStrats);
                }

                if (rowHovered)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                        IM_COL32(30, 40, 60, 160));

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::End();
    }

    // ── Sorting helper ──────────────────────────────────────────────────────────

    void UI::SortStrategies(std::vector<Strategy*>& ptrs)
    {
        if (tableSortCol_ == SortColumn::None) return;

        auto cmp = [&](const Strategy* a, const Strategy* b) -> bool {
            int result = 0;
            switch (tableSortCol_)
            {
            case SortColumn::Symbol: result = a->symbol.compare(b->symbol); break;
            case SortColumn::Type:   result = (int)a->type - (int)b->type; break;
            case SortColumn::Dir:    result = (int)a->direction - (int)b->direction; break;
            case SortColumn::Entry:  result = (a->entryPrice < b->entryPrice) ? -1 : (a->entryPrice > b->entryPrice) ? 1 : 0; break;
            case SortColumn::TP:     result = (a->takeProfit < b->takeProfit) ? -1 : (a->takeProfit > b->takeProfit) ? 1 : 0; break;
            case SortColumn::SL:     result = (a->stopLoss < b->stopLoss) ? -1 : (a->stopLoss > b->stopLoss) ? 1 : 0; break;
            case SortColumn::Qty:    result = (a->quantity < b->quantity) ? -1 : (a->quantity > b->quantity) ? 1 : 0; break;
            case SortColumn::RR:     result = (a->RiskReward() < b->RiskReward()) ? -1 : (a->RiskReward() > b->RiskReward()) ? 1 : 0; break;
            case SortColumn::PnL:   {
                auto getPnl = [this](const Strategy* s) -> float {
                    if (s->IsActive())
                    {
                        float p = GetCurrentPrice(s->symbol);
                        return (p > 0.f && s->entryPrice > 0.f) ? s->UnrealizedPnLPercent(p) : 0.f;
                    }
                    if (s->closedPnlPct != 0.f) return s->closedPnlPct;
                    if (s->exitPrice > 0.f && s->entryPrice > 0.f)
                        return s->UnrealizedPnLPercent(s->exitPrice);
                    return 0.f;
                };
                float pnlA = getPnl(a), pnlB = getPnl(b);
                result = (pnlA < pnlB) ? -1 : (pnlA > pnlB) ? 1 : 0;
            } break;
            case SortColumn::Exit:
                result = (a->exitPrice < b->exitPrice) ? -1 : (a->exitPrice > b->exitPrice) ? 1 : 0; break;
            case SortColumn::Status: result = (int)a->status - (int)b->status; break;
            case SortColumn::Notes:  result = a->notes.compare(b->notes); break;
            default: break;
            }
            return tableSortAsc_ ? (result < 0) : (result > 0);
        };
        std::sort(ptrs.begin(), ptrs.end(), cmp);
    }

    // ── Cell editing helpers ────────────────────────────────────────────────────

    void UI::StartCellEdit(const Strategy& s, int col)
    {
        editCellRowId_ = s.id;
        editCellCol_   = col;
        cellEditBuf_[0] = '\0';

        // Pre-fill the buffer/float based on column
        switch (col)
        {
        case 0:  snprintf(cellEditBuf_, sizeof(cellEditBuf_), "%s", s.symbol.c_str()); break;
        case 3:  cellEditFloat_ = s.entryPrice; break;
        case 4:  cellEditFloat_ = s.takeProfit; break;
        case 5:  cellEditFloat_ = s.stopLoss; break;
        case 6:  cellEditFloat_ = s.quantity; break;
        case 11: snprintf(cellEditBuf_, sizeof(cellEditBuf_), "%s", s.notes.c_str()); break;
        }
    }

    void UI::CommitCellEdit(Strategy& s, int col)
    {
        switch (col)
        {
        case 0:  s.symbol = cellEditBuf_; break;
        case 1:  break; // Type — handled by combo directly
        case 2:  break; // Direction — handled by combo directly
        case 3:  s.entryPrice = cellEditFloat_; break;
        case 4:  s.takeProfit = cellEditFloat_; break;
        case 5:  s.stopLoss   = cellEditFloat_; break;
        case 6:  s.quantity   = cellEditFloat_; break;
        case 11: s.notes = cellEditBuf_; break;
        }

        service_->UpdateStrategy(s);
        strategiesDirty_ = true;
        editCellRowId_ = -1;
        editCellCol_   = -1;
        spdlog::info("[Strategy] Cell edit committed for #{}", s.id);
    }

    float UI::GetCurrentPrice(const std::string& symbol) const
    {
        // In remote mode, prefer live ENet price from server
        auto* remote = dynamic_cast<RemoteStrategyService*>(service_.get());
        if (remote)
        {
            float livePrice = remote->GetLivePrice(symbol);
            if (livePrice > 0.f) return livePrice;
        }

        // Fallback to local chart data
        for (auto& panel : charts_)
        {
            if (panel.symbol == symbol && !panel.quote.candles.empty())
                return panel.quote.candles.back().close;
        }
        return 0.f;
    }

    // ── Main strategy table (Excel-like) ────────────────────────────────────────

    void UI::DrawStrategyTable(const std::function<bool(const Strategy&)>& filter)
    {
        // Build filtered + sorted pointer list
        std::vector<Strategy*> filtered;
        for (auto& s : cachedStrategies_)
            if (filter(s)) filtered.push_back(&s);

        if (filtered.empty())
        {
            ImGui::TextDisabled("No strategies");
            return;
        }

        SortStrategies(filtered);

        // ── Toolbar ────────────────────────────────────────────────────────
        {
            int selCount = (int)tableSelection_.size();
            if (selCount > 0)
                ImGui::Text("%d selected", selCount);
            else
                ImGui::TextDisabled("Select rows with checkboxes");

            ImGui::SameLine(200.f);

            bool hasSel = selCount > 0;
            if (!hasSel) ImGui::BeginDisabled();

            if (ImGui::SmallButton("Enable"))
            {
                for (auto& s : cachedStrategies_)
                    if (tableSelection_.count(s.id)) { s.enabled = true; service_->UpdateStrategy(s); }
                strategiesDirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Disable"))
            {
                for (auto& s : cachedStrategies_)
                    if (tableSelection_.count(s.id)) { s.enabled = false; service_->UpdateStrategy(s); }
                strategiesDirty_ = true;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.f));
            if (ImGui::SmallButton("Delete Selected"))
            {
                for (int64_t id : tableSelection_)
                {
                    bool active = false;
                    for (auto& s : cachedStrategies_)
                        if (s.id == id && s.IsActive()) { active = true; break; }
                    if (active)
                    {
                        float ep = 0.f;
                        for (auto& s : cachedStrategies_)
                            if (s.id == id) { ep = GetCurrentPrice(s.symbol); break; }
                        service_->CancelStrategy(id, ep);
                    }
                    else
                        service_->DeleteStrategy(id);
                }
                tableSelection_.clear();
                strategiesDirty_ = true;
            }
            ImGui::PopStyleColor();

            if (!hasSel) ImGui::EndDisabled();

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
            if (ImGui::SmallButton("Clear"))
                tableSelection_.clear();
        }

        ImGui::Spacing();

        // ── Table ──────────────────────────────────────────────────────────
        ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Hideable |
            ImGuiTableFlags_HighlightHoveredColumn;

        // Push table row colors for better hover/selection UX
        ImGui::PushStyleColor(ImGuiCol_TableRowBg,        ImVec4(0.09f, 0.09f, 0.12f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,     ImVec4(0.11f, 0.11f, 0.14f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,     ImVec4(0.22f, 0.28f, 0.42f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,      ImVec4(0.26f, 0.34f, 0.52f, 0.9f));

        constexpr int kColCount = 15;
        if (!ImGui::BeginTable("##StratTable", kColCount, tableFlags, ImVec2(0.f, 0.f)))
        {
            ImGui::PopStyleColor(4);
            return;
        }

        constexpr auto F = ImGuiTableColumnFlags_WidthFixed;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##chk",   F | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, 18.f);
        ImGui::TableSetupColumn("Symbol",  F | ImGuiTableColumnFlags_DefaultSort, 90.f);
        ImGui::TableSetupColumn("Type",    F, 55.f);
        ImGui::TableSetupColumn("Dir",     F, 50.f);
        ImGui::TableSetupColumn("Entry",   F, 75.f);
        ImGui::TableSetupColumn("Price",   F | ImGuiTableColumnFlags_NoSort, 75.f);  // Current price
        ImGui::TableSetupColumn("TP",      F, 75.f);
        ImGui::TableSetupColumn("SL",      F, 75.f);
        ImGui::TableSetupColumn("Qty",     F, 50.f);
        ImGui::TableSetupColumn("R:R",     F | ImGuiTableColumnFlags_NoSort, 45.f);
        ImGui::TableSetupColumn("P/L%",    F, 60.f);
        ImGui::TableSetupColumn("Exit",    F, 70.f);
        ImGui::TableSetupColumn("Status",  F, 70.f);
        ImGui::TableSetupColumn("Notes",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("",        F | ImGuiTableColumnFlags_NoSort, 80.f);  // Actions

        // Custom header row with select-all checkbox
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int col = 0; col < kColCount; ++col)
        {
            ImGui::TableSetColumnIndex(col);
            if (col == 0)
            {
                // Select-all checkbox
                bool allSelected = !filtered.empty() && tableSelection_.size() == filtered.size();
                bool someSelected = !tableSelection_.empty() && !allSelected;
                if (someSelected)
                    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox("##all", &allSelected))
                {
                    if (allSelected)
                        for (auto* p : filtered) tableSelection_.insert(p->id);
                    else
                        tableSelection_.clear();
                }
                if (someSelected)
                    ImGui::PopItemFlag();
            }
            else
            {
                ImGui::TableHeader(ImGui::TableGetColumnName(col));
            }
        }

        // Handle ImGui sort specs (offset by 1 for checkbox column)
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                auto& spec = specs->Specs[0];
                // Column indices: 0=check, 1=sym, 2=type, 3=dir, 4=entry, 5=price, 6=tp, 7=sl, 8=qty, 9=rr, 10=pnl, 11=exit, 12=status, 13=notes, 14=actions
                static const SortColumn colMap[] = {
                    SortColumn::None, SortColumn::Symbol, SortColumn::Type, SortColumn::Dir,
                    SortColumn::Entry, SortColumn::None, SortColumn::TP, SortColumn::SL,
                    SortColumn::Qty, SortColumn::RR, SortColumn::PnL,
                    SortColumn::Exit, SortColumn::Status, SortColumn::Notes, SortColumn::None
                };
                if (spec.ColumnIndex >= 0 && spec.ColumnIndex < 15)
                    tableSortCol_ = colMap[spec.ColumnIndex];
                else
                    tableSortCol_ = SortColumn::None;
                tableSortAsc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                specs->SpecsDirty = false;
                SortStrategies(filtered);
            }
        }

        int64_t deleteId = -1;
        std::string viewSymbol;

        ImGuiIO& io = ImGui::GetIO();

        for (int rowIdx = 0; rowIdx < (int)filtered.size(); ++rowIdx)
        {
            Strategy& s = *filtered[rowIdx];
            bool isInSelection = tableSelection_.count(s.id) > 0;
            bool isCellEditing = (editCellRowId_ == s.id);
            bool isActiveEdit  = (s.id == selectedStrategyId_);

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(s.id));

            // Dim disabled strategies
            if (!s.enabled)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

            // Row background colors based on state
            if (isActiveEdit)
            {
                // Currently being edited (from chart) — distinct gold highlight
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    IM_COL32(60, 55, 20, 200));
            }
            else if (isInSelection)
            {
                // Multi-selected — blue tint
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    IM_COL32(35, 55, 100, 180));
            }

            // Scroll to selected row (one-shot from chart gizmo)
            if (s.id == selectedStrategyId_ && scrollToStrategy_)
            {
                ImGui::SetScrollHereY(0.5f);
                scrollToStrategy_ = false;
            }

            // Helper lambda: handle click on any cell for row selection
            auto HandleRowClick = [&]() {
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    if (io.KeyCtrl)
                    {
                        if (isInSelection) tableSelection_.erase(s.id);
                        else tableSelection_.insert(s.id);
                    }
                    else if (io.KeyShift && lastClickedId_ > 0)
                    {
                        bool inRange = false;
                        for (auto* p : filtered)
                        {
                            if (p->id == lastClickedId_ || p->id == s.id)
                            {
                                inRange = !inRange;
                                tableSelection_.insert(p->id);
                                if (!inRange) break;
                            }
                            else if (inRange)
                                tableSelection_.insert(p->id);
                        }
                    }
                    else
                    {
                        tableSelection_.clear();
                        tableSelection_.insert(s.id);
                        selectedStrategyId_ = s.id;
                    }
                    lastClickedId_ = s.id;
                }
            };

            auto HandleCellDblClick = [&](int colIdx) {
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    StartCellEdit(s, colIdx);
            };

            // ── Col 0: Checkbox (with row-span selectable for hover) ──
            ImGui::TableNextColumn();
            {
                // Invisible selectable for row hover highlight
                ImGui::Selectable("##rowsel", isInSelection,
                    ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
                bool rowHovered = ImGui::IsItemHovered();
                if (rowHovered && !isInSelection && !isActiveEdit)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                        IM_COL32(28, 38, 58, 140));
                ImGui::SameLine(0.f, 0.f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX());

                bool checked = isInSelection;
                if (ImGui::Checkbox("##chk", &checked))
                {
                    if (checked) tableSelection_.insert(s.id);
                    else tableSelection_.erase(s.id);
                    lastClickedId_ = s.id;
                }
            }

            // ── Col 1: Symbol (color-coded by market) ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 0)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##sym", cellEditBuf_, sizeof(cellEditBuf_),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    CommitCellEdit(s, 0);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = -1; }
            }
            else
            {
                // Color by market type: US=white, MX=amber, Crypto=cyan
                auto mkt = MarketHours::ClassifySymbol(s.symbol);
                ImVec4 symCol;
                switch (mkt)
                {
                case MarketType::US:      symCol = ImVec4(0.90f, 0.92f, 0.96f, 1.f); break;
                case MarketType::Mexico:  symCol = ImVec4(0.95f, 0.75f, 0.25f, 1.f); break;
                case MarketType::Crypto:  symCol = ImVec4(0.30f, 0.85f, 0.90f, 1.f); break;
                default:                  symCol = ImVec4(0.70f, 0.70f, 0.70f, 1.f); break;
                }
                ImGui::TextColored(symCol, "%s", s.symbol.c_str());
                HandleRowClick();
                HandleCellDblClick(0);
            }

            // ── Col 2: Type ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 1)
            {
                int typeVal = static_cast<int>(s.type);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##type", &typeVal, "TP/SL\0Position\0AI\0"))
                {
                    s.type = static_cast<StrategyType>(typeVal);
                    service_->UpdateStrategy(s);
                    strategiesDirty_ = true;
                    editCellRowId_ = -1; editCellCol_ = -1;
                }
            }
            else
            {
                if (s.IsAI())
                    ImGui::TextColored(ImVec4(0.9f, 0.5f, 1.f, 1.f), "AI");
                else if (s.IsPosition())
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f), "POS");
                else
                    ImGui::TextDisabled("TP/SL");
                HandleRowClick();
                HandleCellDblClick(1);
            }

            // ── Col 3: Direction ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 2)
            {
                int dir = static_cast<int>(s.direction);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##dir", &dir, "Long\0Short\0"))
                {
                    s.direction = static_cast<StrategyDirection>(dir);
                    service_->UpdateStrategy(s);
                    strategiesDirty_ = true;
                    editCellRowId_ = -1; editCellCol_ = -1;
                }
            }
            else
            {
                if (s.direction == StrategyDirection::Long)
                    ImGui::TextColored(ImVec4(0.15f, 0.65f, 0.36f, 1.f), "LONG");
                else
                    ImGui::TextColored(ImVec4(0.84f, 0.19f, 0.19f, 1.f), "SHORT");
                HandleRowClick();
                HandleCellDblClick(2);
            }

            // ── Col 4: Entry ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 3)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputFloat("##entry", &cellEditFloat_, 0.f, 0.f, "%.2f",
                    ImGuiInputTextFlags_EnterReturnsTrue))
                    CommitCellEdit(s, 3);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = -1; }
            }
            else
            {
                ImGui::Text("%.2f", s.entryPrice);
                HandleRowClick();
                HandleCellDblClick(3);
            }

            // ── Col 5: Current Price (virtual, read-only) ──
            ImGui::TableNextColumn();
            {
                float curPrice = GetCurrentPrice(s.symbol);
                if (curPrice > 0.f)
                {
                    bool up = curPrice >= s.entryPrice;
                    ImVec4 col = up ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
                                    : ImVec4(0.84f, 0.19f, 0.19f, 1.f);
                    ImGui::TextColored(col, "%.2f", curPrice);
                }
                else
                    ImGui::TextDisabled("-");
            }
            HandleRowClick();

            // ── Col 6: TP ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 4)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputFloat("##tp", &cellEditFloat_, 0.f, 0.f, "%.2f",
                    ImGuiInputTextFlags_EnterReturnsTrue))
                    CommitCellEdit(s, 4);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = -1; }
            }
            else
            {
                if (s.takeProfit > 0.f)
                    ImGui::TextColored(ImVec4(0.15f, 0.65f, 0.36f, 1.f), "%.2f", s.takeProfit);
                else
                    ImGui::TextDisabled("-");
                HandleRowClick();
                HandleCellDblClick(4);
            }

            // ── Col 7: SL ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 5)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputFloat("##sl", &cellEditFloat_, 0.f, 0.f, "%.2f",
                    ImGuiInputTextFlags_EnterReturnsTrue))
                    CommitCellEdit(s, 5);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = -1; }
            }
            else
            {
                if (s.stopLoss > 0.f)
                    ImGui::TextColored(ImVec4(0.84f, 0.19f, 0.19f, 1.f), "%.2f", s.stopLoss);
                else
                    ImGui::TextDisabled("-");
                HandleRowClick();
                HandleCellDblClick(5);
            }

            // ── Col 8: Qty ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 6)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputFloat("##qty", &cellEditFloat_, 0.f, 0.f, "%.2f",
                    ImGuiInputTextFlags_EnterReturnsTrue))
                    CommitCellEdit(s, 6);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = -1; }
            }
            else
            {
                if (s.quantity > 0.f)
                    ImGui::Text("%.0f", s.quantity);
                else
                    ImGui::TextDisabled("-");
                HandleRowClick();
                HandleCellDblClick(6);
            }

            // ── Col 9: R:R ──
            ImGui::TableNextColumn();
            if (s.IsTPSL() && s.stopLoss > 0.f && s.takeProfit > 0.f)
            {
                float rr = s.RiskReward();
                ImVec4 rrCol = rr >= 2.f ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
                             : rr >= 1.f ? ImVec4(0.9f, 0.8f, 0.2f, 1.f)
                                         : ImVec4(0.84f, 0.19f, 0.19f, 1.f);
                ImGui::TextColored(rrCol, "%.1f", rr);
            }
            else
                ImGui::TextDisabled("-");
            HandleRowClick();

            // ── Col 10: P/L% ──
            ImGui::TableNextColumn();
            {
                float pnlPct = 0.f;
                bool hasPnl = false;

                if (!s.IsActive())
                {
                    // Closed: use frozen P/L, or recompute from exitPrice if missing
                    if (s.closedPnlPct != 0.f)
                    {
                        pnlPct = s.closedPnlPct;
                        hasPnl = true;
                    }
                    else if (s.exitPrice > 0.f && s.entryPrice > 0.f)
                    {
                        // Fallback: compute from exit price (for legacy data)
                        pnlPct = s.UnrealizedPnLPercent(s.exitPrice);
                        hasPnl = true;
                    }
                }
                else
                {
                    // Active: compute live P/L from current market price
                    float curPrice = GetCurrentPrice(s.symbol);
                    if (curPrice > 0.f && s.entryPrice > 0.f)
                    {
                        pnlPct = s.UnrealizedPnLPercent(curPrice);
                        hasPnl = true;
                    }
                }

                if (hasPnl)
                {
                    ImVec4 pnlCol = pnlPct >= 0.f
                        ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
                        : ImVec4(0.84f, 0.19f, 0.19f, 1.f);
                    ImGui::TextColored(pnlCol, "%+.1f%%", pnlPct);

                    if (ImGui::IsItemHovered())
                    {
                        if (s.IsActive())
                        {
                            float curPrice = GetCurrentPrice(s.symbol);
                            float pnlAbs = s.UnrealizedPnL(curPrice);
                            ImGui::SetTooltip("P/L: %+.2f\nCurrent: %.2f\nEntry: %.2f",
                                pnlAbs, curPrice, s.entryPrice);
                        }
                        else
                        {
                            ImGui::SetTooltip("Closed P/L (frozen)\nExit: %.2f\nEntry: %.2f",
                                s.exitPrice, s.entryPrice);
                        }
                    }
                }
                else
                    ImGui::TextDisabled("-");
            }
            HandleRowClick();

            // ── Col 11: Exit Price ──
            ImGui::TableNextColumn();
            if (s.exitPrice > 0.f)
                ImGui::Text("%.2f", s.exitPrice);
            else
                ImGui::TextDisabled("-");
            HandleRowClick();

            // ── Col 12: Status ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 10)
            {
                int st = static_cast<int>(s.status);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##status", &st, "Active\0TP Hit\0SL Hit\0Cancelled\0"))
                {
                    s.status = static_cast<StrategyStatus>(st);
                    if (s.status != StrategyStatus::Active && s.triggeredAt == 0)
                        s.triggeredAt = std::time(nullptr);
                    service_->UpdateStrategy(s);
                    strategiesDirty_ = true;
                    editCellRowId_ = -1; editCellCol_ = -1;
                }
            }
            else
            {
                if (!s.enabled)
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "Disabled");
                else switch (s.status)
                {
                case StrategyStatus::Active:
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.f, 1.f), "Active");
                    break;
                case StrategyStatus::TPHit:
                    ImGui::TextColored(ImVec4(0.15f, 0.65f, 0.36f, 1.f), "TP Hit");
                    break;
                case StrategyStatus::SLHit:
                    ImGui::TextColored(ImVec4(0.84f, 0.19f, 0.19f, 1.f), "SL Hit");
                    break;
                case StrategyStatus::Cancelled:
                    ImGui::TextDisabled("Cancelled");
                    break;
                }
                HandleRowClick();
                HandleCellDblClick(10);
            }

            // ── Col 13: Notes ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 11)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##notes", cellEditBuf_, sizeof(cellEditBuf_),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    CommitCellEdit(s, 11);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = -1; }
            }
            else
            {
                if (!s.notes.empty())
                    ImGui::TextWrapped("%s", s.notes.c_str());
                else
                    ImGui::TextDisabled("-");
                HandleRowClick();
                HandleCellDblClick(11);
            }

            // ── Col 14: Actions ──
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("View"))
                viewSymbol = s.symbol;
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                deleteId = s.id;

            if (!s.enabled) ImGui::PopStyleVar(); // Alpha

            ImGui::PopID();
        }

        ImGui::EndTable();
        ImGui::PopStyleColor(4); // table row colors

        // Process deferred delete
        if (deleteId > 0)
        {
            tableSelection_.erase(deleteId);
            if (deleteId == selectedStrategyId_)
            { isEditingInline_ = false; selectedStrategyId_ = -1; }
            if (deleteId == editCellRowId_)
            { editCellRowId_ = -1; editCellCol_ = -1; }

            bool wasActive = false;
            for (auto& s : cachedStrategies_)
                if (s.id == deleteId && s.IsActive()) { wasActive = true; break; }

            if (wasActive)
            {
                float exitPrice = 0.f;
                for (auto& s : cachedStrategies_)
                    if (s.id == deleteId) { exitPrice = GetCurrentPrice(s.symbol); break; }
                service_->CancelStrategy(deleteId, exitPrice);
            }
            else
                service_->DeleteStrategy(deleteId);
            strategiesDirty_ = true;
            telemetry_.strategyDeletes++;
        }
        if (!viewSymbol.empty())
            FetchSymbol(viewSymbol);
    }

    // ── CSV Export / Import ──────────────────────────────────────────────────────

    void UI::ExportCSV()
    {
        auto dest = pfd::save_file("Export Strategies CSV", "strategies.csv",
            {"CSV Files", "*.csv", "All Files", "*"});

        std::string path = dest.result();
        if (path.empty()) return;

        FILE* f = fopen(path.c_str(), "w");
        if (!f)
        {
            spdlog::error("[CSV] Failed to open '{}' for writing", path);
            return;
        }

        // Header (LOL FIX THIS AI TRASH XDXD)
        fprintf(f, "id,symbol,type,direction,entry_price,take_profit,stop_loss,quantity,status,priority,parent_id,exit_price,closed_pnl,notes\n");

        // Rows: export selected if any, otherwise all
        auto& source = tableSelection_.empty() ? cachedStrategies_ : cachedStrategies_;
        for (auto& s : source)
        {
            if (!tableSelection_.empty() && tableSelection_.count(s.id) == 0)
                continue;

            // Escape notes (double quotes for CSV)
            std::string escapedNotes = s.notes;
            size_t pos = 0;
            while ((pos = escapedNotes.find('"', pos)) != std::string::npos)
            {
                escapedNotes.insert(pos, "\"");
                pos += 2;
            }

            fprintf(f, "%lld,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%d,%d,%lld,%.4f,%.4f,\"%s\"\n",
                (long long)s.id, s.symbol.c_str(),
                (int)s.type, (int)s.direction,
                s.entryPrice, s.takeProfit, s.stopLoss, s.quantity,
                (int)s.status, s.priority, (long long)s.parentId,
                s.exitPrice, s.closedPnlPct,
                escapedNotes.c_str());
        }

        fclose(f);
        spdlog::info("[CSV] Exported to '{}'", path);
    }

    void UI::ImportCSV()
    {
        auto src = pfd::open_file("Import Strategies CSV", "",
            {"CSV Files", "*.csv", "All Files", "*"});

        auto paths = src.result();
        if (paths.empty()) return;

        //TODO: TF IS THIS PICE OF SHIT USE IFSTREAM
        FILE* f = fopen(paths[0].c_str(), "r");
        if (!f)
        {
            spdlog::error("[CSV] Failed to open '{}' for reading", paths[0]);
            return;
        }

        char line[1024];
        int lineNum = 0;
        int imported = 0;

        while (fgets(line, sizeof(line), f))
        {
            lineNum++;
            if (lineNum == 1) continue; // Skip header

            Strategy s;
            char symbol[128] = "";
            char notes[512] = "";
            int type = 0, dir = 0, status = 0, priority = 0;
            long long parentId = 0;
            long long id = 0;

            // Parse CSV line (handles quoted notes at the end)
            int parsed = sscanf(line, "%lld,%127[^,],%d,%d,%f,%f,%f,%f,%d,%d,%lld",
                &id, symbol, &type, &dir,
                &s.entryPrice, &s.takeProfit, &s.stopLoss, &s.quantity,
                &status, &priority, &parentId);

            if (parsed < 7) continue; // At minimum: id, symbol, type, dir, entry, tp, sl

            s.symbol    = symbol;
            s.type      = static_cast<StrategyType>(type);
            s.direction = static_cast<StrategyDirection>(dir);
            s.status    = static_cast<StrategyStatus>(status);
            s.priority  = priority;
            s.parentId  = parentId;
            s.createdAt = std::time(nullptr);

            // Extract notes from the quoted field at the end
            const char* quoteStart = strchr(line, '"');
            if (quoteStart)
            {
                quoteStart++; // skip opening quote
                const char* quoteEnd = strrchr(quoteStart, '"');
                if (quoteEnd && quoteEnd > quoteStart)
                    s.notes = std::string(quoteStart, quoteEnd);
            }

            int64_t newId = service_->InsertStrategy(s);
            if (newId > 0) imported++;
        }

        fclose(f);
        strategiesDirty_ = true;
        spdlog::info("[CSV] Imported {} strategies from '{}'", imported, paths[0]);
    }

    // ── Recommendations ─────────────────────────────────────────────────────────

    void UI::ShowRecommendations()
    {
        ImGui::Begin("Recommendations", &showRecommendations_);

        // Header
        if (ImGui::SmallButton("Refresh Insights"))
        {
            RefreshInsights();
            insightRefreshTimer_ = insightRefreshInterval_;
        }
        ImGui::SameLine();
        if (insightsLoading_)
        {
            float time = (float)ImGui::GetTime();
            const char* spinner = "|/-\\";
            ImGui::Text("Loading %c", spinner[(int)(time * 4.f) % 4]);
        }
        else
        {
            ImGui::TextDisabled("(%zu insights)", cachedRecommendations_.size());
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.f);
        ImGui::TextDisabled("%.0fs", insightRefreshTimer_);

        if (!newsService_->HasApiKey())
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.f),
                "Set GNEWS_API_KEY env var for live news data");
        }

        ImGui::Separator();

        if (cachedRecommendations_.empty())
        {
            ImGui::TextDisabled("No recommendations yet. Add active strategies and refresh.");
        }
        else
        {
            for (auto& rec : cachedRecommendations_)
            {
                ImGui::PushID(&rec);

                // Symbol badge
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f), "[%s]", rec.symbol.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.5f, 1.f), "%s", rec.title.c_str());

                if (!rec.body.empty())
                {
                    ImGui::Indent(20.f);
                    ImGui::TextWrapped("%s", rec.body.c_str());
                    ImGui::Unindent(20.f);
                }

                ImGui::Separator();
                ImGui::PopID();
            }
        }

        ImGui::End();
    }

    // ── Market Warnings ─────────────────────────────────────────────────────────

    void UI::ShowMarketWarnings()
    {
        // Color the title bar if there are alerts
        bool hasAlerts = std::any_of(cachedWarnings_.begin(), cachedWarnings_.end(),
            [](const MarketInsight& w) { return w.severity == InsightSeverity::Alert; });

        if (hasAlerts)
        {
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.5f, 0.1f, 0.1f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.3f, 0.08f, 0.08f, 1.f));
        }

        ImGui::Begin("Market Warnings", &showMarketWarnings_);

        if (hasAlerts)
            ImGui::PopStyleColor(2);

        ImGui::TextDisabled("(%zu warnings)", cachedWarnings_.size());
        ImGui::Separator();

        if (cachedWarnings_.empty())
        {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.5f, 1.f), "No warnings - all clear");
        }
        else
        {
            for (auto& warn : cachedWarnings_)
            {
                ImGui::PushID(&warn);

                // Severity icon + color
                ImVec4 sevColor;
                const char* sevIcon;
                switch (warn.severity)
                {
                case InsightSeverity::Alert:
                    sevColor = ImVec4(0.9f, 0.2f, 0.2f, 1.f);
                    sevIcon  = "[!]";
                    break;
                case InsightSeverity::Warning:
                    sevColor = ImVec4(0.9f, 0.7f, 0.2f, 1.f);
                    sevIcon  = "[*]";
                    break;
                default:
                    sevColor = ImVec4(0.6f, 0.6f, 0.7f, 1.f);
                    sevIcon  = "[i]";
                    break;
                }

                ImGui::TextColored(sevColor, "%s", sevIcon);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f), "[%s]", warn.symbol.c_str());
                ImGui::SameLine();
                ImGui::TextColored(sevColor, "%s", warn.title.c_str());

                if (!warn.body.empty())
                {
                    ImGui::Indent(20.f);
                    ImGui::TextWrapped("%s", warn.body.c_str());
                    ImGui::Unindent(20.f);
                }

                ImGui::Separator();
                ImGui::PopID();
            }
        }

        ImGui::End();
    }

    // ── AI Operations ────────────────────────────────────────────────────────────

    void UI::ShowAIOperations()
    {
        ImGui::Begin("AI Operations", &showAIOperations_);

        // Auto-trade toggle (persisted)
        if (ImGui::Checkbox("Auto-Trade", &aiAutoTrade_))
        {
            if (engine_->globals_)
                engine_->globals_->Set(gk::prefix::STATE, gk::key::AI_AUTO_TRADE, aiAutoTrade_ ? "1" : "0");
        }
        ImGui::SameLine();
        if (aiAutoTrade_)
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.f), "LIVE - AI will execute trades!");
        else
            ImGui::TextDisabled("Disabled - AI suggestions only");

        ImGui::Separator();

        if (cachedOperations_.empty())
        {
            ImGui::TextDisabled("No AI operations pending.");
            ImGui::TextDisabled("Add AI-type strategies and wait for analysis cycle.");
        }
        else
        {
            // Group by urgency
            for (int sev = (int)InsightSeverity::Alert; sev >= (int)InsightSeverity::Info; --sev)
            {
                auto severity = (InsightSeverity)sev;
                bool hasAny = false;
                for (auto& op : cachedOperations_)
                    if (op.urgency == severity) { hasAny = true; break; }
                if (!hasAny) continue;

                // Section header with color
                ImVec4 headerCol;
                const char* headerLabel;
                switch (severity)
                {
                case InsightSeverity::Alert:
                    headerCol = ImVec4(0.9f, 0.15f, 0.15f, 1.f);
                    headerLabel = "EMERGENCY";
                    break;
                case InsightSeverity::Warning:
                    headerCol = ImVec4(0.9f, 0.7f, 0.15f, 1.f);
                    headerLabel = "SHOULD ACT";
                    break;
                default:
                    headerCol = ImVec4(0.3f, 0.8f, 0.4f, 1.f);
                    headerLabel = "RECOMMENDATIONS";
                    break;
                }

                ImGui::TextColored(headerCol, "--- %s ---", headerLabel);
                ImGui::Spacing();

                for (auto& op : cachedOperations_)
                {
                    if (op.urgency != severity) continue;

                    ImGui::PushID(&op);

                    // Operation type badge
                    ImVec4 typeCol = (op.type == OperationType::Sell)
                        ? ImVec4(0.9f, 0.3f, 0.3f, 1.f)
                        : (op.type == OperationType::Buy)
                            ? ImVec4(0.3f, 0.8f, 0.4f, 1.f)
                            : ImVec4(0.7f, 0.7f, 0.8f, 1.f);

                    ImGui::TextColored(typeCol, "[%s]", OperationTypeToString(op.type));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f), "%s", op.symbol.c_str());
                    ImGui::SameLine();

                    if (op.suggestedPrice > 0.f)
                        ImGui::Text("@ %.2f", op.suggestedPrice);

                    if (op.strategyId > 0)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(#%lld)", (long long)op.strategyId);
                    }

                    // Confidence bar
                    if (op.confidence > 0.f)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%.0f%%", op.confidence * 100.f);
                    }

                    // Reason
                    if (!op.reason.empty())
                    {
                        ImGui::Indent(20.f);
                        ImGui::TextWrapped("%s", op.reason.c_str());
                        ImGui::Unindent(20.f);
                    }

                    // Status / action button
                    if (op.executed)
                    {
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
                        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.f), "DONE");
                    }
                    else if (!aiAutoTrade_)
                    {
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.f);
                        if (ImGui::SmallButton("Execute"))
                        {
                            // Manual execution of single operation
                            op.executed = true;
                            // TODO: Wire to broker action
                        }
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }

                ImGui::Spacing();
            }
        }

        ImGui::End();
    }

    // ── Strategy Wizard (Dockable) ─────────────────────────────────────────────

    void UI::ShowStrategyWizard()
    {
        // Get candle data from the chart matching the wizard's symbol (or first available)
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
        // Fallback to any chart if symbol not found
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

        // Handle wizard results at UI level
        if (wizard_.WasConfirmed())
        {
            Strategy result = wizard_.GetStrategy();

            // Find matching StrategyLayer and apply
            bool handled = false;
            for (auto& panel : charts_)
            {
                auto* sl = panel.chart.GetStrategyLayer();
                if (!sl) continue;

                if (sl->GetEditingId() > 0 && sl->GetEditingId() == result.id)
                {
                    // Edit mode: update existing
                    if (sl->onStrategyChanged)
                        sl->onStrategyChanged(result, false);
                    sl->StopEditing();
                    handled = true;
                    break;
                }
                else if (result.symbol == panel.symbol && result.id == 0)
                {
                    // Create mode: insert new via layer callback
                    if (sl->onStrategyChanged)
                        sl->onStrategyChanged(result, true);
                    handled = true;
                    break;
                }
            }

            // Fallback: save directly if no chart panel matched
            if (!handled && service_)
            {
                if (result.id > 0)
                    service_->UpdateStrategy(result);
                else
                    service_->InsertStrategy(result);
                strategiesDirty_ = true;
            }

            wizard_.ConsumeResult();
        }
        else if (wizard_.WasCancelled())
        {
            // Notify any editing layer
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
                    // Timeframe selector row
                    ImGui::PushID(it->symbol.c_str());
                    for (int tf = 0; tf < (int)(sizeof(kTimeframes) / sizeof(kTimeframes[0])); ++tf)
                    {
                        if (tf > 0) ImGui::SameLine(0.f, 2.f);
                        bool selected = (it->timeframeIdx == tf);
                        if (selected)
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.28f, 0.45f, 1.f));
                        if (ImGui::SmallButton(kTimeframes[tf].label))
                        {
                            if (it->timeframeIdx != tf)
                            {
                                it->timeframeIdx = tf;
                                it->loading = true;
                                it->quote = StockQuote{};
                                it->chart = StockChart{};
                                marketService_->FetchQuoteAsync(
                                    it->symbol,
                                    kTimeframes[tf].interval,
                                    kTimeframes[tf].range);
                                spdlog::info("[UI] Switching {} to {}", it->symbol, kTimeframes[tf].label);
                            }
                        }
                        if (selected)
                            ImGui::PopStyleColor();
                    }
                    // Latency / data freshness indicator
                    if (!it->quote.candles.empty() && it->quote.fetchedAt > 0)
                    {
                        int64_t now = (int64_t)std::time(nullptr);
                        int64_t age = now - it->quote.fetchedAt;
                        int64_t lastCandle = it->quote.candles.back().timestamp;
                        int64_t candleAge = now - lastCandle;

                        // Color: green < 60s, yellow < 5min, orange < 15min, red > 15min
                        ImVec4 color;
                        if (age < 60)        color = ImVec4(0.3f, 0.9f, 0.3f, 1.f);
                        else if (age < 300)  color = ImVec4(0.9f, 0.9f, 0.3f, 1.f);
                        else if (age < 900)  color = ImVec4(0.9f, 0.6f, 0.2f, 1.f);
                        else                 color = ImVec4(0.9f, 0.3f, 0.3f, 1.f);

                        auto fmtAge = [](int64_t secs) -> std::string {
                            if (secs < 60)   return std::to_string(secs) + "s";
                            if (secs < 3600) return std::to_string(secs / 60) + "m";
                            return std::to_string(secs / 3600) + "h";
                        };

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
                        ImGui::TextColored(color, "Data: %s ago", fmtAge(age).c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("| Last candle: %s ago", fmtAge(candleAge).c_str());

                        auto* source = marketService_->GetActiveSource();
                        if (source && !source->IsRealtime())
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f),
                                "(%s ~%dm delay)", source->GetName(), source->GetDelaySeconds() / 60);
                        }

                        // Market state badge
                        MarketState mktState = MarketHours::GetState(it->symbol);
                        MarketType mktType = MarketHours::ClassifySymbol(it->symbol);
                        ImVec4 mktColor;
                        switch (mktState)
                        {
                        case MarketState::Open:       mktColor = ImVec4(0.2f, 0.9f, 0.3f, 1.f); break;
                        case MarketState::PreMarket:  mktColor = ImVec4(0.9f, 0.8f, 0.2f, 1.f); break;
                        case MarketState::AfterHours: mktColor = ImVec4(0.8f, 0.6f, 0.2f, 1.f); break;
                        case MarketState::Closed:     mktColor = ImVec4(0.6f, 0.3f, 0.3f, 1.f); break;
                        }
                        ImGui::SameLine();
                        ImGui::TextColored(mktColor, "[%s %s]",
                            MarketHours::MarketTypeToString(mktType),
                            MarketHours::StateToString(mktState));
                    }
                    ImGui::PopID();

                    if (it->loading)
                    {
                        ImGui::TextDisabled("Loading %s...", it->symbol.c_str());
                        float time = (float)ImGui::GetTime();
                        const char* spinner = "|/-\\";
                        ImGui::SameLine();
                        ImGui::Text("%c", spinner[(int)(time * 4.f) % 4]);
                    }
                    else if (it->quote.candles.empty())
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.f),
                                           "Failed to load data for %s", it->symbol.c_str());
                    }
                    else
                    {
                        it->chart.Draw(("##chart_" + it->symbol).c_str());

                        // Chart bounds for wizard interaction
                        ImVec2 chartMin = ImGui::GetItemRectMin();
                        ImVec2 chartMax = ImGui::GetItemRectMax();
                        auto* stratLayer = it->chart.GetStrategyLayer();
                        if (stratLayer)
                        {
                            const std::vector<Candle>* candles = it->quote.candles.empty()
                                ? nullptr : &it->quote.candles;

                            // PreUpdate on the UI-level dockable wizard for historical click detection
                            wizard_.PreUpdate(chartMin, chartMax,
                                it->chart.GetFocusedCandle(),
                                it->chart.GetCandleCount(),
                                candles);

                            // Also PreUpdate the layer wizard (for overlay mode redundancy)
                            auto& layerWiz = stratLayer->GetWizard();
                            layerWiz.PreUpdate(chartMin, chartMax,
                                it->chart.GetFocusedCandle(),
                                it->chart.GetCandleCount(),
                                candles);

                            // "+ Strategy" overlay button on chart (opens dockable wizard)
                            if (!wizard_.IsOpen() && !layerWiz.IsOpen())
                            {
                                ImVec2 btnPos(chartMin.x + 8.f, chartMin.y + 8.f);
                                ImGui::SetNextWindowPos(btnPos, ImGuiCond_Always);
                                ImGui::SetNextWindowSize(ImVec2(0, 0));
                                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
                                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
                                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.9f));

                                std::string btnWinId = "##StratBtn_" + it->symbol;
                                ImGui::Begin(btnWinId.c_str(), nullptr,
                                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus);

                                if (ImGui::SmallButton("+ Strategy"))
                                {
                                    float price = it->quote.candles.empty() ? 0.f : it->quote.candles.back().close;
                                    wizard_.OpenCreate(it->symbol, StrategyType::TPSL, StrategyDirection::Long, price);
                                    showStrategyWizard_ = true; // Ensure panel is visible
                                }

                                ImGui::End();
                                ImGui::PopStyleColor();
                                ImGui::PopStyleVar(2);
                            }

                            // Draw layer wizard overlay (redundancy — for right-click context menu created strats)
                            stratLayer->DrawWizard(chartMin, chartMax,
                                it->chart.GetFocusedCandle(),
                                it->chart.GetCandleCount(),
                                candles);
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (!open)
                    it = charts_.erase(it);
                else
                    ++it;
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void UI::ShowSymbolSelector()
    {
        ImGui::TextDisabled("Search & Add Symbol");

        ImGui::SetNextItemWidth(280.f);
        bool changed = ImGui::InputText("##symbolSearch", searchInput_, sizeof(searchInput_),
                                         ImGuiInputTextFlags_None);

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

        bool hasResults = !searchResults_.empty() && currentQuery.length() >= 2;

        if (hasResults || searchPending_)
        {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
            ImGui::SetNextWindowSize(ImVec2(460.f, 0.f));

            ImGuiWindowFlags popupFlags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_AlwaysAutoResize;

            ImGui::Begin("##SymbolDropdown", nullptr, popupFlags);

            if (searchPending_ && searchResults_.empty())
                ImGui::TextDisabled("Searching...");

            for (auto& match : searchResults_)
            {
                char label[256];
                snprintf(label, sizeof(label), "%-12s  %-30s  %-10s  %s",
                         match.symbol.c_str(),
                         match.name.c_str(),
                         match.exchange.c_str(),
                         match.type.c_str());

                if (ImGui::Selectable(label))
                {
                    FetchSymbol(match.symbol);
                    searchResults_.clear();
                    searchInput_[0] = '\0';
                }
            }

            ImGui::End();
        }

        if (currentQuery.length() < 2)
            searchResults_.clear();

        ImGui::SameLine();
        ImGui::TextDisabled("Scroll: navigate | Shift+Scroll: zoom | Middle-drag: pan");
    }

    void UI::FetchSymbol(const std::string& symbol,
                         const char* interval,
                         const char* range)
    {
        telemetry_.marketFetches++;

        // If chart already exists for this symbol, don't duplicate — just refetch
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

        ChartPanel panel;
        panel.symbol  = symbol;
        panel.loading = true;
        charts_.push_back(std::move(panel));

        marketService_->FetchQuoteAsync(symbol, interval, range);
        spdlog::info("[UI] Fetching {} ({})", symbol, interval);
    }

    // ── Dashboard & Threads ────────────────────────────────────────────────────

    void UI::ShowDashboard()
    {
        ImGui::Begin("Dashboard", &showDashboard_);

        // Update uptime
        telemetry_.uptimeSec += ImGui::GetIO().DeltaTime;
        telemetry_.avgFrameMs = 1000.0f / ImGui::GetIO().Framerate;

        // ── Connection Status ──────────────────────────────────────────────
        {
            bool connected = service_->IsConnected();
            ImVec4 statusCol = connected
                ? ImVec4(0.15f, 0.85f, 0.40f, 1.f)
                : ImVec4(0.85f, 0.20f, 0.20f, 1.f);

            ImGui::TextColored(statusCol, "%s", connected ? "CONNECTED" : "DISCONNECTED");
            ImGui::SameLine();

            std::string url = service_->GetServerUrl();
            if (url == "local://embedded")
                ImGui::TextDisabled("(monolith)");
            else
            {
                ImGui::TextDisabled("(%s)", url.c_str());
                auto* remote = dynamic_cast<RemoteStrategyService*>(service_.get());
                if (remote)
                {
                    float latency = remote->GetLatencyMs();
                    if (latency >= 0.f)
                    {
                        ImGui::SameLine();
                        ImVec4 latCol = latency < 50.f ? ImVec4(0.15f, 0.85f, 0.40f, 1.f)
                                      : latency < 150.f ? ImVec4(0.9f, 0.8f, 0.2f, 1.f)
                                      : ImVec4(0.85f, 0.20f, 0.20f, 1.f);
                        ImGui::TextColored(latCol, "%.0fms", latency);
                    }
                }
            }

            bool monitoring = service_->IsMonitoring();
            ImGui::SameLine();
            if (monitoring)
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.f, 1.f), "[Monitoring]");
            else
                ImGui::TextDisabled("[Idle]");
        }

        ImGui::Separator();

        // ── Performance ────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("FPS: %.1f  (%.2f ms/frame)", ImGui::GetIO().Framerate, telemetry_.avgFrameMs);

            auto& dbg = engine_->threadDebugInfo_;
            if (dbg.historyOffset > 0 || dbg.frameHistory[0] > 0.f)
            {
                ImGui::PlotLines("##frame", dbg.frameHistory.data(),
                                 ThreadDebugInfo::kHistorySize, dbg.historyOffset,
                                 nullptr, 0.f, 33.3f, ImVec2(-1, 40));
            }

            // Uptime
            int upH = (int)(telemetry_.uptimeSec / 3600.f);
            int upM = (int)(std::fmod(telemetry_.uptimeSec, 3600.f) / 60.f);
            int upS = (int)std::fmod(telemetry_.uptimeSec, 60.f);
            ImGui::Text("Uptime: %02d:%02d:%02d", upH, upM, upS);
        }

        // ── Telemetry ──────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Client");
            ImGui::Text("Market Fetches:    %lld", (long long)telemetry_.marketFetches);
            ImGui::Text("Chart Refreshes:   %lld", (long long)telemetry_.chartRefreshes);
            ImGui::Text("Strategy Saves:    %lld", (long long)telemetry_.strategySaves);
            ImGui::Text("Strategy Deletes:  %lld", (long long)telemetry_.strategyDeletes);
            ImGui::Text("Open Charts:       %d", (int)charts_.size());

            // Server telemetry (remote mode only, fetched with status polling)
            auto* remote = dynamic_cast<RemoteStrategyService*>(service_.get());
            if (remote && service_->IsConnected())
            {
                auto st = remote->GetServerTelemetry();
                ImGui::Spacing();
                ImGui::TextDisabled("Server");
                ImGui::Text("Requests Served:   %lld", (long long)st.requestsServed);
                ImGui::Text("Ticks Broadcast:   %lld", (long long)st.ticksBroadcast);
                ImGui::Text("Connected Clients: %d", st.clients);

                if (st.uptimeSec > 0)
                {
                    int sH = st.uptimeSec / 3600;
                    int sM = (st.uptimeSec % 3600) / 60;
                    int sS = st.uptimeSec % 60;
                    ImGui::Text("Server Uptime:     %02d:%02d:%02d", sH, sM, sS);
                }
            }
        }

        // ── Strategies Summary ─────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Strategies", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int activeCount = (int)std::count_if(
                cachedStrategies_.begin(), cachedStrategies_.end(),
                [](const Strategy& s) { return s.IsActive(); });
            int posCount = (int)std::count_if(
                cachedStrategies_.begin(), cachedStrategies_.end(),
                [](const Strategy& s) { return s.IsPosition() && s.IsActive(); });
            int aiCount = (int)std::count_if(
                cachedStrategies_.begin(), cachedStrategies_.end(),
                [](const Strategy& s) { return s.IsAI() && s.IsActive(); });

            ImGui::Text("Active: %d  |  Positions: %d  |  AI: %d", activeCount, posCount, aiCount);
            ImGui::Text("Total: %d", (int)cachedStrategies_.size());

            if (!cachedRecommendations_.empty() || !cachedWarnings_.empty() || !cachedOperations_.empty())
                ImGui::Text("Insights: %zu recs, %zu warnings, %zu ops",
                             cachedRecommendations_.size(), cachedWarnings_.size(), cachedOperations_.size());
        }

        ImGui::End();
    }

    void UI::ShowThreadsDebugger()
    {
        ImGui::Begin("Threads Debugger", &showThreadsDebugger_);

        auto& td  = engine_->threadDebugInfo_;
        int   off = td.historyOffset;
        const float avail = ImGui::GetContentRegionAvail().x;

        // ── Channel Timings table with colored bars ─────────────────────────
        ImGui::SeparatorText("Channel Timings");

        struct Row { const char* label; float ms; bool async; ImVec4 col; };
        Row rows[] = {
            { "MAIN",       td.main.durationMs,       false, {0.30f, 0.65f, 1.00f, 1.f} },
            { "RENDERING",  td.rendering.durationMs,  false, {1.00f, 0.55f, 0.20f, 1.f} },
        };

        // Add worker threads dynamically
        auto workerStatus = engine_->threadRegistry_.GetStatus();
        std::vector<Row> allRows(std::begin(rows), std::end(rows));

        // Predefined colors for workers
        ImVec4 workerColors[] = {
            {0.40f, 0.90f, 0.80f, 1.f},  // cyan
            {0.55f, 0.85f, 0.40f, 1.f},  // green
            {0.90f, 0.40f, 0.85f, 1.f},  // purple
            {0.85f, 0.85f, 0.20f, 1.f},  // yellow
            {0.90f, 0.55f, 0.55f, 1.f},  // red
            {0.55f, 0.55f, 0.90f, 1.f},  // blue
        };
        int colorIdx = 0;
        for (auto& w : workerStatus)
        {
            ImVec4 col = workerColors[colorIdx % 6];
            colorIdx++;
            allRows.push_back({ w.name.c_str(), w.lastDurationMs,
                                true, col });
        }

        if (ImGui::BeginTable("##channeltable", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingFixedFit))
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
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(r.col, "%s", r.label);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f", r.ms);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled(r.async ? "worker" : "main");

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

        // ── Frame Timeline (horizontal stacked bars) ────────────────────────
        ImGui::SeparatorText("Frame Timeline");

        float total = td.updatePhaseMs + td.presentPhaseMs;
        if (total > 0.f)
        {
            const float tlW  = avail - 8.f;
            const float rowH = 20.f;
            const float gap  = 4.f;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 origin  = ImGui::GetCursorScreenPos();

            auto drawSegment = [&](float xStart, float dur, ImVec4 col,
                                    const char* lbl, float rowY)
            {
                float x0 = origin.x + (xStart / total) * tlW;
                float x1 = origin.x + ((xStart + dur) / total) * tlW;
                if (x1 <= x0 + 1.f) x1 = x0 + 2.f;
                ImU32 c = ImGui::ColorConvertFloat4ToU32(col);
                dl->AddRectFilled({x0, rowY}, {x1, rowY + rowH}, c, 3.f);
                dl->AddRect({x0, rowY}, {x1, rowY + rowH}, IM_COL32(0,0,0,120), 3.f);
                ImVec2 tsz = ImGui::CalcTextSize(lbl);
                if (x1 - x0 > tsz.x + 4.f)
                    dl->AddText({x0 + (x1 - x0 - tsz.x) * 0.5f,
                                 rowY + (rowH - tsz.y) * 0.5f},
                                IM_COL32(255,255,255,230), lbl);
            };

            float row0 = origin.y;           // Main thread
            float row1 = row0 + rowH + gap;  // Workers

            // Main thread: MAIN then RENDERING
            drawSegment(0.f, td.main.durationMs,
                        {0.30f, 0.65f, 1.00f, 0.9f}, "MAIN", row0);
            drawSegment(td.updatePhaseMs, td.rendering.durationMs,
                        {1.00f, 0.55f, 0.20f, 0.9f}, "RENDER", row0);

            // Workers row: show each worker as segment
            float workerOffset = 0.f;
            colorIdx = 0;
            for (auto& w : workerStatus)
            {
                ImVec4 col = workerColors[colorIdx % 6];
                col.w = 0.9f;
                colorIdx++;
                // Scale worker time proportionally
                float dur = std::min(w.lastDurationMs, total);
                drawSegment(workerOffset, dur, col, w.name.c_str(), row1);
                workerOffset += dur;
            }

            // Row labels
            dl->AddText({origin.x, row0 + rowH + 2.f}, IM_COL32(180,180,180,160), "main");
            dl->AddText({origin.x, row1 + rowH + 2.f}, IM_COL32(180,180,180,160), "workers");

            ImGui::Dummy({tlW, rowH * 2.f + gap + 16.f});
        }
        else
        {
            ImGui::TextDisabled("No frame data yet");
        }

        // ── Rolling sparkline histories ─────────────────────────────────────
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

        ImGui::End();
    }

} // namespace stnks
