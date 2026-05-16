#include <UI/UI.hpp>
#include <Charts/StrategyLayer.hpp>
#include <Service/LocalStrategyService.hpp>
#include <Service/RemoteStrategyService.hpp>
#include <portable-file-dialogs.h>
#include <spdlog/spdlog.h>
#include <algorithm>
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

        spdlog::info("[UI] Initialized");
    }

    void UI::Update() {}

    void UI::DrainAsyncResults()
    {
        marketService_->DrainQuoteResults([this](QuoteFetchResult&& result) {
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
                        service_->CancelStrategy(id);
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

            spdlog::info("[AI] Got {} recs + {} warnings for '{}'",
                         result.recommendations.size(), result.warnings.size(), result.symbol);
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

        // Auto-refresh market data timer
        if (marketRefreshInterval_ > 0.f && !charts_.empty())
        {
            marketRefreshTimer_ -= ImGui::GetIO().DeltaTime;
            if (marketRefreshTimer_ <= 0.f)
            {
                for (auto& panel : charts_)
                {
                    if (!panel.loading && !panel.refreshing && !panel.quote.candles.empty())
                    {
                        auto& tf = kTimeframes[panel.timeframeIdx];
                        panel.refreshing = true;
                        marketService_->FetchQuoteAsync(panel.symbol, tf.interval, tf.range);
                    }
                }
                marketRefreshTimer_ = marketRefreshInterval_;
            }
        }

        // Auto-refresh strategy timer
        strategyRefreshTimer_ -= ImGui::GetIO().DeltaTime;
        if (strategyRefreshTimer_ <= 0.f)
        {
            strategiesDirty_ = true;
            strategyRefreshTimer_ = strategyRefreshInterval_;
        }

        // Reload strategies from DB if dirty
        if (strategiesDirty_)
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
        }

        ShowDockSpace();

        if (showDashboard_)
            ShowDashboard();
        if (showThreadsDebugger_)
            ShowThreadsDebugger();
        if (showStockCharts_)
            ShowStockCharts();
        if (showStrategies_)
            ShowStrategies();
        if (showRecommendations_)
            ShowRecommendations();
        if (showMarketWarnings_)
            ShowMarketWarnings();
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
                ImGui::MenuItem("Strategies", nullptr, &showStrategies_);
                ImGui::MenuItem("Recommendations", nullptr, &showRecommendations_);
                ImGui::MenuItem("Market Warnings", nullptr, &showMarketWarnings_);
                ImGui::Separator();
                ImGui::MenuItem("Threads Debugger", nullptr, &showThreadsDebugger_);
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
        if (focusStrategiesTab_)
        {
            ImGui::SetNextWindowFocus();
            focusStrategiesTab_ = false;
        }

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
                        service_->CancelStrategy(id);
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
        bool forceActiveTab = (selectedStrategyId_ > 0);

        if (ImGui::BeginTabBar("##StratTabs"))
        {
            ImGuiTabItemFlags activeFlags = forceActiveTab
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

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
                float pa = GetCurrentPrice(a->symbol), pb = GetCurrentPrice(b->symbol);
                float pnlA = pa > 0 ? a->UnrealizedPnLPercent(pa) : 0.f;
                float pnlB = pb > 0 ? b->UnrealizedPnLPercent(pb) : 0.f;
                result = (pnlA < pnlB) ? -1 : (pnlA > pnlB) ? 1 : 0;
            } break;
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
        case 10: snprintf(cellEditBuf_, sizeof(cellEditBuf_), "%s", s.notes.c_str()); break;
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
        case 10: s.notes = cellEditBuf_; break;
        }

        service_->UpdateStrategy(s);
        strategiesDirty_ = true;
        editCellRowId_ = -1;
        editCellCol_   = -1;
        spdlog::info("[Strategy] Cell edit committed for #{}", s.id);
    }

    float UI::GetCurrentPrice(const std::string& symbol) const
    {
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

        ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollY;

        constexpr int kColCount = 12;
        if (!ImGui::BeginTable("##StratTable", kColCount, tableFlags, ImVec2(0.f, 0.f)))
            return;

        constexpr auto F = ImGuiTableColumnFlags_WidthFixed;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Symbol",  F | ImGuiTableColumnFlags_DefaultSort, 90.f);
        ImGui::TableSetupColumn("Type",    F, 55.f);
        ImGui::TableSetupColumn("Dir",     F, 50.f);
        ImGui::TableSetupColumn("Entry",   F, 75.f);
        ImGui::TableSetupColumn("TP",      F, 75.f);
        ImGui::TableSetupColumn("SL",      F, 75.f);
        ImGui::TableSetupColumn("Qty",     F, 60.f);
        ImGui::TableSetupColumn("R:R",     F | ImGuiTableColumnFlags_NoSort, 50.f);
        ImGui::TableSetupColumn("P/L%",    F, 65.f);
        ImGui::TableSetupColumn("Status",  F, 70.f);
        ImGui::TableSetupColumn("Notes",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", F | ImGuiTableColumnFlags_NoSort, 120.f);
        ImGui::TableHeadersRow();

        // Handle ImGui sort specs
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                auto& spec = specs->Specs[0];
                static const SortColumn colMap[] = {
                    SortColumn::Symbol, SortColumn::Type, SortColumn::Dir,
                    SortColumn::Entry, SortColumn::TP, SortColumn::SL,
                    SortColumn::Qty, SortColumn::RR, SortColumn::PnL,
                    SortColumn::Status, SortColumn::Notes, SortColumn::None
                };
                if (spec.ColumnIndex >= 0 && spec.ColumnIndex < 11)
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

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(s.id));

            // Row selection highlight
            if (isInSelection)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    IM_COL32(40, 55, 90, 180));

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
                        // Ctrl+click: toggle this row in selection
                        if (isInSelection)
                            tableSelection_.erase(s.id);
                        else
                            tableSelection_.insert(s.id);
                    }
                    else if (io.KeyShift && lastClickedId_ > 0)
                    {
                        // Shift+click: range select from lastClickedId_ to this row
                        bool inRange = false;
                        for (auto* p : filtered)
                        {
                            if (p->id == lastClickedId_ || p->id == s.id)
                            {
                                inRange = !inRange;
                                tableSelection_.insert(p->id);
                                if (!inRange) break; // We toggled twice = end of range
                            }
                            else if (inRange)
                            {
                                tableSelection_.insert(p->id);
                            }
                        }
                    }
                    else
                    {
                        // Plain click: select only this row
                        tableSelection_.clear();
                        tableSelection_.insert(s.id);
                        selectedStrategyId_ = s.id;
                    }
                    lastClickedId_ = s.id;
                }
            };

            // Helper: check for double-click to start editing a cell
            auto HandleCellDblClick = [&](int colIdx) {
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    StartCellEdit(s, colIdx);
                }
            };

            // ── Col 0: Symbol ──
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
                ImGui::Text("%s", s.symbol.c_str());
                HandleRowClick();
                HandleCellDblClick(0);
            }

            // ── Col 1: Type ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 1)
            {
                int typeVal = static_cast<int>(s.type);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##type", &typeVal, "TP/SL\0Position\0"))
                {
                    s.type = static_cast<StrategyType>(typeVal);
                    service_->UpdateStrategy(s);
                    strategiesDirty_ = true;
                    editCellRowId_ = -1;
                    editCellCol_ = -1;
                }
            }
            else
            {
                if (s.IsPosition())
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.f, 1.f), "POS");
                else
                    ImGui::TextDisabled("TP/SL");
                HandleRowClick();
                HandleCellDblClick(1);
            }

            // ── Col 2: Direction ──
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
                    editCellRowId_ = -1;
                    editCellCol_ = -1;
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

            // ── Col 3: Entry ──
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

            // ── Col 4: TP ──
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

            // ── Col 5: SL ──
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

            // ── Col 6: Qty ──
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
                    ImGui::Text("%.2f", s.quantity);
                else
                    ImGui::TextDisabled("-");
                HandleRowClick();
                HandleCellDblClick(6);
            }

            // ── Col 7: R:R (computed, read-only) ──
            ImGui::TableNextColumn();
            if (s.IsTPSL())
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

            // ── Col 8: P/L% (computed from current price) ──
            ImGui::TableNextColumn();
            {
                float curPrice = GetCurrentPrice(s.symbol);
                if (curPrice > 0.f && s.entryPrice > 0.f)
                {
                    float pnlPct = s.UnrealizedPnLPercent(curPrice);
                    ImVec4 pnlCol = pnlPct >= 0.f
                        ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
                        : ImVec4(0.84f, 0.19f, 0.19f, 1.f);
                    ImGui::TextColored(pnlCol, "%+.2f%%", pnlPct);

                    if (ImGui::IsItemHovered())
                    {
                        float pnlAbs = s.UnrealizedPnL(curPrice);
                        ImGui::SetTooltip("P/L: %+.2f %s\nCurrent: %.2f\nEntry: %.2f",
                            pnlAbs, s.quantity > 0.f ? "(qty-weighted)" : "(per unit)",
                            curPrice, s.entryPrice);
                    }
                }
                else
                    ImGui::TextDisabled("-");
            }
            HandleRowClick();

            // ── Col 9: Status ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 9)
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
                    editCellRowId_ = -1;
                    editCellCol_ = -1;
                }
            }
            else
            {
                switch (s.status)
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
                HandleCellDblClick(9);
            }

            // ── Col 10: Notes ──
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == 10)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##notes", cellEditBuf_, sizeof(cellEditBuf_),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    CommitCellEdit(s, 10);
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
                HandleCellDblClick(10);
            }

            // ── Col 11: Actions ──
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("View"))
                viewSymbol = s.symbol;

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.f));
            if (ImGui::SmallButton("Del"))
                deleteId = s.id;
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::EndTable();

        // Process deferred delete: cancel if active, hard-delete if already closed
        if (deleteId > 0)
        {
            tableSelection_.erase(deleteId);
            if (deleteId == selectedStrategyId_)
            {
                isEditingInline_ = false;
                selectedStrategyId_ = -1;
            }
            if (deleteId == editCellRowId_)
            {
                editCellRowId_ = -1;
                editCellCol_ = -1;
            }

            // Find the strategy to check its status
            bool wasActive = false;
            for (auto& s : cachedStrategies_)
            {
                if (s.id == deleteId && s.IsActive())
                {
                    wasActive = true;
                    break;
                }
            }

            if (wasActive)
            {
                service_->CancelStrategy(deleteId);
                spdlog::info("[Strategy] Cancelled #{}", deleteId);
            }
            else
            {
                service_->DeleteStrategy(deleteId);
                spdlog::info("[Strategy] Deleted #{}", deleteId);
            }
            strategiesDirty_ = true;
        }
        if (!viewSymbol.empty())
        {
            FetchSymbol(viewSymbol);
        }
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

        // Header
        fprintf(f, "id,symbol,type,direction,entry_price,take_profit,stop_loss,quantity,status,priority,parent_id,notes\n");

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

            fprintf(f, "%lld,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%d,%d,%lld,\"%s\"\n",
                (long long)s.id, s.symbol.c_str(),
                (int)s.type, (int)s.direction,
                s.entryPrice, s.takeProfit, s.stopLoss, s.quantity,
                (int)s.status, s.priority, (long long)s.parentId,
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

    // ── Stock Charts ───────────────────────────────────────────────────────────

    void UI::ShowStockCharts()
    {
        ImGui::Begin("Stock Charts", &showStockCharts_,
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

        ShowSymbolSelector();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.f);

        ImGui::TextDisabled("Refresh:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.f);
        ImGui::InputFloat("##refreshRate", &marketRefreshInterval_, 0.f, 0.f, "%.0fs");
        marketRefreshInterval_ = std::max(1.f, marketRefreshInterval_);
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0fs)", marketRefreshTimer_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Now"))
            marketRefreshTimer_ = 0.f;

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

        ImGui::Text("STNKS Finance Tool");
        ImGui::Separator();

        ImGui::Text("Frame: %.3f ms  |  FPS: %.1f",
                     1000.0f / ImGui::GetIO().Framerate,
                     ImGui::GetIO().Framerate);

        ImGui::Separator();
        ImGui::Text("Engine Status");

        auto& dbg = engine_->threadDebugInfo_;
        ImGui::Text("Update:  %.2f ms", dbg.updatePhaseMs);
        ImGui::Text("Present: %.2f ms", dbg.presentPhaseMs);

        if (dbg.historyOffset > 0 || dbg.frameHistory[0] > 0.f)
        {
            ImGui::Separator();
            ImGui::Text("Frame Time History");
            ImGui::PlotLines("##frame", dbg.frameHistory.data(),
                             ThreadDebugInfo::kHistorySize, dbg.historyOffset,
                             nullptr, 0.f, 33.3f, ImVec2(0, 60));
        }

        ImGui::Separator();
        ImGui::Text("Thread Pool: %zu threads", engine_->threadRegistry_.GetPoolSize());

        auto status = engine_->threadRegistry_.GetStatus();
        if (!status.empty())
        {
            ImGui::Text("Workers:");
            for (const auto& t : status)
            {
                ImGui::BulletText("%s: %s (%.2f ms, %zu iters)",
                    t.name.c_str(),
                    t.state == WorkerThread::State::Running ? "Running" :
                    t.state == WorkerThread::State::Idle    ? "Idle"    : "Stopped",
                    t.lastDurationMs, t.iterations);
            }
        }

        ImGui::Separator();
        ImGui::Text("ECS: %zu entities", engine_->registry_.Entities().size());

        int activeCount = (int)std::count_if(
            cachedStrategies_.begin(), cachedStrategies_.end(),
            [](const Strategy& s) { return s.IsActive(); });
        int posCount = (int)std::count_if(
            cachedStrategies_.begin(), cachedStrategies_.end(),
            [](const Strategy& s) { return s.IsPosition() && s.IsActive(); });

        ImGui::Text("Strategies: %d active (%d positions)", activeCount, posCount);
        ImGui::Text("Insights: %zu recs, %zu warnings",
                     cachedRecommendations_.size(), cachedWarnings_.size());

        ImGui::End();
    }

    void UI::ShowThreadsDebugger()
    {
        ImGui::Begin("Threads Debugger", &showThreadsDebugger_);

        ImGui::Text("Pool Size: %zu", engine_->threadRegistry_.GetPoolSize());
        ImGui::Separator();

        auto status = engine_->threadRegistry_.GetStatus();
        if (status.empty())
        {
            ImGui::TextDisabled("No active worker threads");
        }
        else
        {
            ImGui::Columns(4, "threads");
            ImGui::Text("Name"); ImGui::NextColumn();
            ImGui::Text("State"); ImGui::NextColumn();
            ImGui::Text("Duration"); ImGui::NextColumn();
            ImGui::Text("Iterations"); ImGui::NextColumn();
            ImGui::Separator();

            for (const auto& t : status)
            {
                ImGui::Text("%s", t.name.c_str()); ImGui::NextColumn();
                const char* stateStr =
                    t.state == WorkerThread::State::Running  ? "Running" :
                    t.state == WorkerThread::State::Idle     ? "Idle" :
                    t.state == WorkerThread::State::Stopping ? "Stopping" : "Stopped";
                ImGui::Text("%s", stateStr); ImGui::NextColumn();
                ImGui::Text("%.2f ms", t.lastDurationMs); ImGui::NextColumn();
                ImGui::Text("%zu", t.iterations); ImGui::NextColumn();
            }
            ImGui::Columns(1);
        }

        ImGui::End();
    }

} // namespace stnks
