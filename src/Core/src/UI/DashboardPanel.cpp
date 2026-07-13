#include <UI/DashboardPanel.hpp>
#include <UI/UIConstants.hpp>
#include <Dependencies/Globals.hpp>
#include <GlobalKeys.hpp>
#include <Service/RemoteStrategyService.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace stnks
{
    void DashboardPanel::Draw(bool* open)
    {
        ImGui::Begin("Dashboard", open);

        ctx_.telemetry->uptimeSec += ImGui::GetIO().DeltaTime;
        ctx_.telemetry->avgFrameMs = 1000.0f / ImGui::GetIO().Framerate;

        DrawConnectionStatus();
        DrawRemoteConnect();

        ImGui::Separator();
        DrawSystems();
        DrawDataConfig();
        DrawEnvironment();
        DrawPerformance();
        DrawTelemetry();
        DrawStrategiesSummary();

        ImGui::End();
    }

    void DashboardPanel::DrawConnectionStatus()
    {
        bool connected = ctx_.service->IsConnected();
        ImGui::TextColored(connected ? ui::kColorConnected : ui::kColorDisconnected,
            "%s", connected ? "CONNECTED" : "DISCONNECTED");
        ImGui::SameLine();

        std::string url = ctx_.service->GetServerUrl();
        if (url == "local://embedded")
            ImGui::TextDisabled("(monolith)");
        else
        {
            ImGui::TextDisabled("(%s)", url.c_str());
            auto* remote = dynamic_cast<RemoteStrategyService*>(ctx_.service);
            if (remote)
            {
                float latency = remote->GetLatencyMs();
                if (latency >= 0.f)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ui::LatencyColor(latency), "%.0fms", latency);
                }
            }
        }

        bool monitoring = ctx_.service->IsMonitoring();
        ImGui::SameLine();
        if (monitoring)
            ImGui::TextColored(ui::kColorMonitoring, "[Monitoring]");
        else
            ImGui::TextDisabled("[Idle]");
    }

    void DashboardPanel::DrawRemoteConnect()
    {
        ImGui::Spacing();

        // Toggle button for the connect panel
        bool isRemote = ctx_.service->GetServerUrl() != "local://embedded";

        if (isRemote)
        {
            // Show disconnect button when in remote mode
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.2f, 1.f));
            if (ImGui::Button("Disconnect", ImVec2(90, 0)))
            {
                if (ctx_.connectToServer)
                    ctx_.connectToServer("");
                showConnectPanel_ = false;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
        }

        // Connect / expand button
        {
            const char* label = showConnectPanel_ ? "Hide##connect" : "Connect to Remote";
            if (ImGui::Button(label))
                showConnectPanel_ = !showConnectPanel_;
        }

        // Launch local server shortcut
        if (ctx_.showServerLauncher)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Launch Local Server"))
                *ctx_.showServerLauncher = true;
        }

        if (!showConnectPanel_)
        {
            ImGui::Spacing();
            return;
        }

        ImGui::Spacing();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x;
        float panelH = 82.f;

        // Panel background
        ImVec2 panelMin(cursor.x, cursor.y);
        ImVec2 panelMax(cursor.x + avail, cursor.y + panelH);
        dl->AddRectFilled(panelMin, panelMax, IM_COL32(18, 22, 32, 240), 6.f);
        dl->AddRect(panelMin, panelMax, IM_COL32(55, 70, 110, 180), 6.f, 0, 1.2f);

        // Reserve space
        ImGui::Dummy(ImVec2(avail, panelH));

        // Draw inside the panel
        float pad = 8.f;
        ImGui::SetCursorScreenPos(ImVec2(panelMin.x + pad, panelMin.y + pad));

        // Label
        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.85f, 1.f), "Server Address");

        ImGui::SetCursorScreenPos(ImVec2(panelMin.x + pad, panelMin.y + pad + 22.f));

        // Input field with styled background
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.08f, 0.10f, 0.16f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.15f, 0.22f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.15f, 0.18f, 0.28f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.25f, 0.35f, 0.55f, 0.8f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, 6.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

        float inputW = avail - pad * 2.f - 90.f;
        ImGui::SetNextItemWidth(inputW);

        bool enterPressed = ImGui::InputText("##remoteAddr", remoteAddress_, sizeof(remoteAddress_),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);

        // Connect button
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.45f, 0.75f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.20f, 0.55f, 0.85f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.25f, 0.60f, 0.90f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);

        bool doConnect = ImGui::Button("Connect", ImVec2(74, 0)) || enterPressed;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        if (doConnect && remoteAddress_[0] != '\0')
        {
            if (ctx_.connectToServer)
                ctx_.connectToServer(remoteAddress_);
            showConnectPanel_ = false;
        }

        ImGui::Spacing();
    }

    void DashboardPanel::DrawPerformance()
    {
        if (!ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) return;

        ImGui::Text("FPS: %.1f  (%.2f ms/frame)", ImGui::GetIO().Framerate, ctx_.telemetry->avgFrameMs);

        auto& dbg = ctx_.engine->threadDebugInfo_;
        if (dbg.historyOffset > 0 || dbg.frameHistory[0] > 0.f)
        {
            ImGui::PlotLines("##frame", dbg.frameHistory.data(),
                ThreadDebugInfo::kHistorySize, dbg.historyOffset,
                nullptr, 0.f, 33.3f, ImVec2(-1, 40));
        }

        int upH = (int)(ctx_.telemetry->uptimeSec / 3600.f);
        int upM = (int)(std::fmod(ctx_.telemetry->uptimeSec, 3600.f) / 60.f);
        int upS = (int)std::fmod(ctx_.telemetry->uptimeSec, 60.f);
        ImGui::Text("Uptime: %02d:%02d:%02d", upH, upM, upS);
    }

    void DashboardPanel::DrawTelemetry()
    {
        if (!ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) return;

        ImGui::TextDisabled("Client");
        ImGui::Text("Market Fetches:    %lld", (long long)ctx_.telemetry->marketFetches);
        ImGui::Text("Chart Refreshes:   %lld", (long long)ctx_.telemetry->chartRefreshes);
        ImGui::Text("Strategy Saves:    %lld", (long long)ctx_.telemetry->strategySaves);
        ImGui::Text("Strategy Deletes:  %lld", (long long)ctx_.telemetry->strategyDeletes);
        ImGui::Text("Open Charts:       %d", (int)ctx_.charts->size());

        auto* remote = dynamic_cast<RemoteStrategyService*>(ctx_.service);
        if (remote && ctx_.service->IsConnected())
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

    void DashboardPanel::DrawStrategiesSummary()
    {
        if (!ImGui::CollapsingHeader("Strategies", ImGuiTreeNodeFlags_DefaultOpen)) return;

        int active = (int)std::count_if(ctx_.strategies->begin(), ctx_.strategies->end(),
            [](const Strategy& s) { return s.IsActive(); });
        int pos = (int)std::count_if(ctx_.strategies->begin(), ctx_.strategies->end(),
            [](const Strategy& s) { return s.IsPosition() && s.IsActive(); });
        int ai = (int)std::count_if(ctx_.strategies->begin(), ctx_.strategies->end(),
            [](const Strategy& s) { return s.IsAI() && s.IsActive(); });

        ImGui::Text("Active: %d  |  Positions: %d  |  AI: %d", active, pos, ai);
        ImGui::Text("Total: %d", (int)ctx_.strategies->size());

        if (!ctx_.recommendations->empty() || !ctx_.warnings->empty() || !ctx_.operations->empty())
            ImGui::Text("Insights: %zu recs, %zu warnings, %zu ops",
                ctx_.recommendations->size(), ctx_.warnings->size(), ctx_.operations->size());
    }


    void DashboardPanel::DrawSystems()
    {
        if (!ImGui::CollapsingHeader("Systems", ImGuiTreeNodeFlags_DefaultOpen)) return;
        if (!ctx_.systemToggles || !ctx_.envOverrides) return;

        auto& t = *ctx_.systemToggles;
        auto& env = *ctx_.envOverrides;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Helper: draw a system row with colored indicator dot + toggle
        auto drawSystem = [&](const char* label, bool& enabled, const char* envKey,
                              const ImVec4& activeColor, const char* tooltip)
        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            float dotRadius = 5.f;
            float dotY = cursor.y + ImGui::GetTextLineHeight() * 0.5f;

            bool hasKey = envKey ? env.IsSet(envKey) : true;
            bool running = enabled && hasKey;

            // Status dot
            ImU32 dotCol = running
                ? ImGui::ColorConvertFloat4ToU32(activeColor)
                : IM_COL32(80, 80, 80, 200);
            dl->AddCircleFilled(ImVec2(cursor.x + dotRadius, dotY), dotRadius, dotCol);

            // Pulsing glow when active
            if (running)
            {
                float pulse = 0.4f + 0.3f * std::sin((float)ImGui::GetTime() * 2.5f);
                ImU32 glowCol = IM_COL32(
                    (int)(activeColor.x * 255), (int)(activeColor.y * 255),
                    (int)(activeColor.z * 255), (int)(pulse * 100));
                dl->AddCircleFilled(ImVec2(cursor.x + dotRadius, dotY), dotRadius + 3.f, glowCol);
            }

            ImGui::SetCursorScreenPos(ImVec2(cursor.x + dotRadius * 2.f + 8.f, cursor.y));

            // Toggle checkbox
            ImGui::PushID(label);
            if (!hasKey)
            {
                // No API key — show disabled toggle
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
                bool dummy = false;
                ImGui::Checkbox(label, &dummy);
                ImGui::PopStyleVar();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.85f, 0.5f, 0.2f, 1.f), "(no key)");
            }
            else
            {
                ImGui::Checkbox(label, &enabled);
                ImGui::SameLine();
                ImGui::TextColored(running ? ImVec4(0.3f, 0.85f, 0.4f, 1.f) : ImVec4(0.5f, 0.5f, 0.5f, 1.f),
                                   running ? "ON" : "OFF");
            }

            if (ImGui::IsItemHovered() && tooltip)
                ImGui::SetTooltip("%s", tooltip);

            ImGui::PopID();
        };

        ImGui::Spacing();
        ImGui::Indent(4.f);

        drawSystem("AI Analysis",       t.ai,              "CLAUDE_API_KEY",
                   ImVec4(0.55f, 0.4f, 0.9f, 1.f),  "Claude AI market analysis & trade suggestions");
        drawSystem("News Feed",         t.news,            "GNEWS_API_KEY",
                   ImVec4(0.3f, 0.7f, 0.95f, 1.f),  "GNews API for market news context");
        drawSystem("Strategy Monitor",  t.strategyMonitor, nullptr,
                   ImVec4(0.2f, 0.8f, 0.5f, 1.f),   "TP/SL trigger checking & auto-execution");
        drawSystem("Graph Events",      t.graphEvents,     nullptr,
                   ImVec4(0.9f, 0.7f, 0.2f, 1.f),   "Chart pattern detection (RSI/MACD/crossovers)");

        ImGui::Spacing();
        ImGui::TextDisabled("Brokers");
        drawSystem("Binance",           t.binance,         "BINANCE_API_KEY",
                   ImVec4(0.96f, 0.78f, 0.15f, 1.f), "Binance spot trading & real-time data");
        drawSystem("MetaTrader 5",      t.metaTrader,      "MT5_API_KEY",
                   ImVec4(0.3f, 0.75f, 0.3f, 1.f),  "MetaTrader 5 via MetaApi bridge");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Trading");

        // Live trading & AI auto-trade — persist changes to globals
        auto* globals = ctx_.engine ? ctx_.engine->globals_.get() : nullptr;

        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            float dotY = cursor.y + ImGui::GetTextLineHeight() * 0.5f;
            ImU32 ltCol = *ctx_.liveTradingEnabled
                ? IM_COL32(220, 50, 50, 255)
                : IM_COL32(80, 80, 80, 200);
            dl->AddCircleFilled(ImVec2(cursor.x + 5.f, dotY), 5.f, ltCol);
            if (*ctx_.liveTradingEnabled)
            {
                float p = 0.4f + 0.3f * std::sin((float)ImGui::GetTime() * 3.f);
                dl->AddCircleFilled(ImVec2(cursor.x + 5.f, dotY), 8.f, IM_COL32(220, 50, 50, (int)(p * 100)));
            }
            ImGui::SetCursorScreenPos(ImVec2(cursor.x + 18.f, cursor.y));
            if (ImGui::Checkbox("Live Trading", ctx_.liveTradingEnabled))
            {
                if (globals)
                    globals->Set(gk::prefix::STATE, gk::key::LIVE_TRADING,
                                 *ctx_.liveTradingEnabled ? "1" : "0");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("When ON, triggers and AI ops send real orders.\nWhen OFF, everything is dry-run (log only).");
            ImGui::SameLine();
            ImGui::TextColored(*ctx_.liveTradingEnabled
                ? ImVec4(0.9f, 0.2f, 0.2f, 1.f) : ImVec4(0.3f, 0.75f, 0.4f, 1.f),
                *ctx_.liveTradingEnabled ? "LIVE" : "DRY-RUN");
        }

        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            float dotY = cursor.y + ImGui::GetTextLineHeight() * 0.5f;
            bool active = *ctx_.aiAutoTrade && *ctx_.liveTradingEnabled && t.ai;
            ImU32 col = active ? IM_COL32(180, 80, 220, 255) : IM_COL32(80, 80, 80, 200);
            dl->AddCircleFilled(ImVec2(cursor.x + 5.f, dotY), 5.f, col);
            ImGui::SetCursorScreenPos(ImVec2(cursor.x + 18.f, cursor.y));
            if (ImGui::Checkbox("AI Auto-Trade", ctx_.aiAutoTrade))
            {
                if (globals)
                    globals->Set(gk::prefix::STATE, gk::key::AI_AUTO_TRADE,
                                 *ctx_.aiAutoTrade ? "1" : "0");
            }
            if (!t.ai)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.85f, 0.5f, 0.2f, 1.f), "(AI off)");
            }
            else if (*ctx_.aiAutoTrade && !*ctx_.liveTradingEnabled)
            {
                ImGui::TextColored(ImVec4(0.85f, 0.7f, 0.2f, 1.f),
                                   "Auto-trade ON but live trading OFF — orders logged only.");
            }
        }

        ImGui::Unindent(4.f);
        ImGui::Spacing();
    }


    void DashboardPanel::DrawDataConfig()
    {
        if (!ImGui::CollapsingHeader("Data & Intervals")) return;

        if (ctx_.marketRefreshInterval)
        {
            ImGui::Text("Market Refresh: %.0fs", *ctx_.marketRefreshInterval);
            ImGui::SliderFloat("Refresh (s)", ctx_.marketRefreshInterval, 5.f, 120.f, "%.0f");
        }
        if (ctx_.priceCacheInterval)
        {
            ImGui::Text("Price Cache: %.0fs", *ctx_.priceCacheInterval);
            ImGui::SliderFloat("Cache (s)", ctx_.priceCacheInterval, 5.f, 60.f, "%.0f");
        }

        ImGui::Separator();
        std::string url = ctx_.service->GetServerUrl();
        ImGui::Text("Server: %s", url == "local://embedded" ? "localhost (monolith)" : url.c_str());
        ImGui::Text("Connected: %s", ctx_.service->IsConnected() ? "Yes" : "No");
    }


    void DashboardPanel::DrawEnvironment()
    {
        if (!ImGui::CollapsingHeader("Environment")) return;
        if (!ctx_.envOverrides) return;

        auto& env = *ctx_.envOverrides;

        // Initialize edit buffers on first draw
        if (!envBuffersInit_)
        {
            for (auto& [key, entry] : env.entries)
            {
                auto& buf = envEditBuffers_[key];
                std::memset(buf.data, 0, sizeof(buf.data));
                // Use Get() which reads overridden value (e.g. from .env) or std::getenv
                std::string val = env.Get(key);
                if (!val.empty())
                    std::strncpy(buf.data, val.c_str(), sizeof(buf.data) - 1);
            }
            envBuffersInit_ = true;
        }

        ImGui::Spacing();

        // Group env vars by category
        struct EnvGroup { const char* label; std::vector<const char*> keys; };
        EnvGroup groups[] = {
            {"AI & News", {"CLAUDE_API_KEY", "GNEWS_API_KEY"}},
            {"Binance",   {"BINANCE_API_KEY", "BINANCE_API_SECRET", "BINANCE_SANDBOX"}},
            {"MetaTrader", {"MT5_API_KEY", "MT5_ACCOUNT_ID"}},
            {"General",    {"STNKS_SERVER_URL", "ASSETS_STNKS"}},
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 4.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);

        for (auto& group : groups)
        {
            ImGui::TextDisabled("%s", group.label);
            ImGui::Indent(4.f);

            for (auto* key : group.keys)
            {
                auto it = env.entries.find(key);
                if (it == env.entries.end()) continue;
                auto& entry = it->second;
                auto& buf = envEditBuffers_[key];
                bool hasValue = buf.data[0] != '\0';

                ImGui::PushID(key);

                // Status indicator
                ImVec4 statusCol = entry.overridden
                    ? ImVec4(0.9f, 0.7f, 0.2f, 1.f)   // yellow = overridden
                    : hasValue
                        ? ImVec4(0.3f, 0.8f, 0.4f, 1.f)  // green = from env
                        : ImVec4(0.5f, 0.5f, 0.5f, 0.6f); // gray = not set
                ImGui::TextColored(statusCol, "%s", entry.overridden ? "[OVR]" : hasValue ? "[ENV]" : "[---]");
                ImGui::SameLine();

                // Label
                ImGui::Text("%s", key);
                ImGui::SameLine(220.f);

                // Editable input
                float inputW = std::max(ImGui::GetContentRegionAvail().x - 60.f, 100.f);
                ImGui::SetNextItemWidth(inputW);

                if (entry.isSecret && !entry.overridden && hasValue)
                {
                    // Show masked value for secrets
                    size_t len = std::strlen(buf.data);
                    size_t maskLen = std::min(len, (size_t)20);
                    char masked[24] = {};
                    if (len > 4)
                    {
                        std::memcpy(masked, buf.data, 4);
                        std::memset(masked + 4, '*', maskLen - 4);
                    }
                    else
                        std::memset(masked, '*', maskLen);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
                    ImGui::InputText("##val", masked, sizeof(masked), ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Edit"))
                        entry.overridden = true;
                }
                else
                {
                    // Editable field
                    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                        entry.overridden ? ImVec4(0.15f, 0.13f, 0.05f, 1.f)
                                         : ImVec4(0.08f, 0.10f, 0.16f, 1.f));

                    ImGui::InputText("##val", buf.data, sizeof(buf.data));

                    // Fires on Enter, Tab, or click-away after editing
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        entry.value = buf.data;
                        entry.overridden = true;
                        ctx_.PushToast(std::string(key) + " updated", ui::kToastInfo, 3.f);

                        // Persist to .env file
                        if (ctx_.saveEnv) ctx_.saveEnv();

                        // Re-wire broker connector if a broker credential changed
                        if (ctx_.wireBroker)
                        {
                            std::string k(key);
                            if (k.rfind("BINANCE_", 0) == 0)
                                ctx_.wireBroker("Binance");
                            else if (k.rfind("MT5_", 0) == 0)
                                ctx_.wireBroker("MetaTrader 5");
                        }
                    }

                    ImGui::PopStyleColor();

                    // Reset button if overridden
                    if (entry.overridden)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Reset"))
                        {
                            const char* orig = std::getenv(key);
                            std::memset(buf.data, 0, sizeof(buf.data));
                            if (orig) std::strncpy(buf.data, orig, sizeof(buf.data) - 1);
                            entry.value = buf.data;
                            entry.overridden = false;
                            ctx_.PushToast(std::string(key) + " reset to env", ui::kToastInfo, 3.f);
                        }
                    }
                }

                ImGui::PopID();
            }

            ImGui::Unindent(4.f);
            ImGui::Spacing();
        }

        ImGui::PopStyleVar(2);
    }

} // namespace stnks
