#include <UI/MarketSignalsPanel.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>
#include <cstdio>
#include <ctime>

namespace stnks
{
    MarketSignalsPanel::MarketSignalsPanel(UIContext& ctx) : ctx_(ctx) {}

    void MarketSignalsPanel::Draw(bool* open)
    {
        int actionable = ctx_.signalService->ActionableCount();
        auto allSignals = ctx_.signalService->GetAll();

        bool hasAlert = false;
        for (auto& s : allSignals)
            if (s.severity == SignalSeverity::Alert && !s.dismissed && !s.accepted)
            { hasAlert = true; break; }

        if (hasAlert)
        {
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ui::kAlertTitleActive);
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ui::kAlertTitleBg);
        }

        char title[64];
        if (actionable > 0)
            snprintf(title, sizeof(title), "Market Signals (%d)###MarketSignals", actionable);
        else
            snprintf(title, sizeof(title), "Market Signals###MarketSignals");

        if (!ImGui::Begin(title, open))
        {
            if (hasAlert) ImGui::PopStyleColor(2);
            ImGui::End();
            return;
        }
        if (hasAlert) ImGui::PopStyleColor(2);

        DrawContent();
        ImGui::End();
    }

    void MarketSignalsPanel::DrawContent()
    {
        int actionable = ctx_.signalService->ActionableCount();
        auto allSignals = ctx_.signalService->GetAll();

        DrawToolbar(actionable, allSignals.size());
        DrawFilterTabs();
        ImGui::Separator();

        ImGui::BeginChild("##SignalList", ImVec2(0, 0), false);

        int shown = 0;
        for (auto& sig : allSignals)
        {
            if (filterTab_ == 1 && !sig.IsActionable()) continue;
            if (filterTab_ == 2 && sig.origin != SignalOrigin::Pattern) continue;
            if (filterTab_ == 3 && sig.origin != SignalOrigin::AI) continue;
            if (filterTab_ == 4 && sig.origin != SignalOrigin::Strategy) continue;
            if (sig.dismissed && filterTab_ != 0) continue;

            ImGui::PushID((int)sig.id);
            ++shown;

            if (sig.dismissed)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ui::kDismissedAlpha);

            DrawSignalItem(sig);

            if (sig.dismissed)
                ImGui::PopStyleVar();

            ImGui::Separator();
            ImGui::PopID();
        }

        if (shown == 0)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No signals yet.");
            ImGui::TextDisabled("Signals appear from chart scanning, AI analysis, and strategy triggers.");
        }

        ImGui::EndChild();
    }

    void MarketSignalsPanel::DrawToolbar(int actionable, size_t total)
    {
        if (ImGui::SmallButton("Refresh") && ctx_.refreshInsights)
            ctx_.refreshInsights();
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss All"))
            ctx_.signalService->DismissAll();
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            ctx_.signalService->Clear();
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu total, %d actionable)", total, actionable);
    }

    void MarketSignalsPanel::DrawFilterTabs()
    {
        ImGui::Separator();
        const char* tabs[] = {"All", "Actionable", "Patterns", "AI", "Strategy"};
        for (int i = 0; i < 5; ++i)
        {
            if (i > 0) ImGui::SameLine();
            bool selected = (filterTab_ == i);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ui::kSelectedTabBtn);
            if (ImGui::SmallButton(tabs[i]))
                filterTab_ = i;
            if (selected) ImGui::PopStyleColor();
        }
    }

    void MarketSignalsPanel::DrawSignalItem(MarketSignal& sig)
    {
        // Severity
        ImGui::TextColored(ui::SeverityColor(sig.severity), "%s", ui::SeverityIcon(sig.severity));
        ImGui::SameLine();

        // Origin badge
        ImGui::TextColored(ui::SignalOriginColor(sig.origin), "[%s]", ui::SignalOriginLabel(sig.origin));
        ImGui::SameLine();

        // Symbol
        ImGui::TextColored(ui::kColorAccent, "%s", sig.symbol.c_str());
        ImGui::SameLine();

        // Action badge
        if (sig.action != SignalAction::None && !sig.accepted)
        {
            ImVec4 actCol = sig.IsBuyish() ? ui::kColorBullish
                          : sig.IsSellish() ? ui::kColorBearish
                          : ui::kColorNeutral;
            ImGui::TextColored(actCol, "[%s]", SignalActionToString(sig.action));
            ImGui::SameLine();
        }

        if (sig.confidence > 0.f)
        { ImGui::TextDisabled("%.0f%%", sig.confidence * 100.f); ImGui::SameLine(); }

        // Timestamp
        {
            time_t ts = static_cast<time_t>(sig.timestamp);
            struct tm t;
#ifdef _WIN32
            localtime_s(&t, &ts);
#else
            localtime_r(&ts, &t);
#endif
            char timeBuf[32];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
            ImGui::TextDisabled("%s", timeBuf);
        }

        // Collapsible detail
        bool nodeOpen = ImGui::TreeNode("##detail", "%s", sig.title.c_str());
        if (nodeOpen)
        {
            if (!sig.read) ctx_.signalService->MarkRead(sig.id);

            if (!sig.description.empty())
            {
                ImGui::Indent(4.f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.8f, 1.f));
                ImGui::TextWrapped("%s", sig.description.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(4.f);
                ImGui::Spacing();
            }

            DrawSignalResources(sig);
            DrawSignalActions(sig);
            ImGui::TreePop();
        }
    }

    void MarketSignalsPanel::DrawSignalResources(const MarketSignal& sig)
    {
        if (sig.resources.empty()) return;
        if (!ImGui::TreeNodeEx("Evidence & Resources", ImGuiTreeNodeFlags_DefaultOpen)) return;

        for (int ri = 0; ri < (int)sig.resources.size(); ++ri)
        {
            auto& res = sig.resources[ri];
            ImGui::PushID(ri);

            ImVec4 resCol;
            const char* resIcon;
            switch (res.type)
            {
            case ResourceType::GraphEvent:   resCol = {0.3f,0.8f,0.5f,1.f}; resIcon = "EVT"; break;
            case ResourceType::PatternMatch: resCol = res.score > 0.f ? ImVec4(0.3f,0.9f,0.4f,1.f) : ImVec4(0.6f,0.3f,0.3f,1.f);
                                             resIcon = res.score > 0.f ? "OK" : "MISS"; break;
            case ResourceType::AIPrompt:     resCol = {0.6f,0.4f,1.f,1.f}; resIcon = "IN"; break;
            case ResourceType::AIResponse:   resCol = {0.6f,0.4f,1.f,1.f}; resIcon = "OUT"; break;
            case ResourceType::BrokerOrder:  resCol = ui::kColorWarning; resIcon = "ORD"; break;
            case ResourceType::PriceLevel:   resCol = {0.9f,0.8f,0.3f,1.f}; resIcon = "$"; break;
            case ResourceType::NewsArticle:  resCol = {0.5f,0.7f,0.9f,1.f}; resIcon = "NEWS"; break;
            default:                         resCol = {0.6f,0.6f,0.7f,1.f}; resIcon = "N"; break;
            }

            ImGui::TextColored(resCol, "[%s]", resIcon);
            ImGui::SameLine();

            if (!res.content.empty())
            {
                if (ImGui::TreeNode("##res", "%s", res.label.c_str()))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.f));
                    ImGui::TextWrapped("%s", res.content.c_str());
                    ImGui::PopStyleColor();

                    if (res.candleIdx >= 0 && ImGui::SmallButton("Go to candle"))
                    {
                        for (auto& panel : *ctx_.charts)
                        {
                            if (panel.symbol == sig.symbol)
                            { panel.chart.ScrollToCandle(res.candleIdx); break; }
                        }
                    }
                    ImGui::TreePop();
                }
            }
            else
                ImGui::TextUnformatted(res.label.c_str());

            if (res.type == ResourceType::PatternMatch && res.score > 0.f)
            {
                ImGui::SameLine();
                ImVec4 scoreCol = res.score >= 0.8f ? ui::kColorBullish
                                : res.score >= 0.5f ? ui::kColorWarning
                                : ui::kColorBearish;
                ImGui::TextColored(scoreCol, "%.0f%%", res.score * 100.f);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    void MarketSignalsPanel::DrawSignalActions(MarketSignal& sig)
    {
        if (sig.IsActionable())
        {
            ImGui::Spacing();
            ImGui::Separator();

            ImVec4 acceptCol = sig.IsBuyish() ? ImVec4(0.15f, 0.65f, 0.3f, 1.f)
                             : sig.IsSellish() ? ImVec4(0.8f, 0.2f, 0.2f, 1.f)
                             : ImVec4(0.3f, 0.5f, 0.8f, 1.f);

            ImGui::PushStyleColor(ImGuiCol_Button, acceptCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(acceptCol.x + 0.1f, acceptCol.y + 0.1f, acceptCol.z + 0.1f, 1.f));

            char label[64];
            switch (sig.action)
            {
            case SignalAction::Buy: case SignalAction::Sell:
                snprintf(label, sizeof(label), "Create %s Strategy", sig.IsBuyish() ? "Long" : "Short"); break;
            case SignalAction::CreateTPSL: snprintf(label, sizeof(label), "Create TP/SL"); break;
            case SignalAction::AdjustTP:   snprintf(label, sizeof(label), "Adjust Take Profit"); break;
            case SignalAction::AdjustSL:   snprintf(label, sizeof(label), "Adjust Stop Loss"); break;
            case SignalAction::ClosePos:   snprintf(label, sizeof(label), "Close Position"); break;
            default:                       snprintf(label, sizeof(label), "Accept"); break;
            }

            if (ImGui::Button(label))
            {
                ctx_.signalService->MarkAccepted(sig.id);
                float entryPrice = sig.suggestedEntry > 0.f
                    ? sig.suggestedEntry : ctx_.getCurrentPrice(sig.symbol);
                StrategyType type = (sig.action == SignalAction::CreateTPSL) ? StrategyType::TPSL
                    : (sig.origin == SignalOrigin::AI) ? StrategyType::AI : StrategyType::Position;
                ctx_.wizard->OpenCreate(sig.symbol, type, sig.direction, entryPrice);
                auto& ws = ctx_.wizard->GetStrategy();
                if (sig.suggestedTP > 0.f) ws.takeProfit = sig.suggestedTP;
                if (sig.suggestedSL > 0.f) ws.stopLoss = sig.suggestedSL;
                if (sig.suggestedQty > 0.f) ws.quantity = sig.suggestedQty;
                *ctx_.showStrategyWizard = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ImGui::Button("Dismiss"))
                ctx_.signalService->MarkDismissed(sig.id);

            ImGui::SameLine();
            if (ImGui::SmallButton("View Chart"))
            {
                bool found = false;
                for (auto& panel : *ctx_.charts)
                    if (panel.symbol == sig.symbol) { found = true; break; }
                if (!found && !sig.symbol.empty() && ctx_.fetchSymbol)
                    ctx_.fetchSymbol(sig.symbol, "1d", "6mo");
            }
        }
        else if (sig.accepted)
        {
            ImGui::TextColored(ui::kColorBullish, "Accepted");
        }
    }

} // namespace stnks
