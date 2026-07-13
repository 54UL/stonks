#include <UI/AIPanels.hpp>
#include <UI/UIConstants.hpp>
#include <Market/MarketHours.hpp>
#include <Dependencies/Globals.hpp>
#include <GlobalKeys.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>

namespace stnks
{

    void RecommendationsPanel::Draw(bool* open)
    {
        ImGui::Begin("Recommendations", open);

        if (ImGui::SmallButton("Refresh Insights") && ctx_.refreshInsights)
        {
            ctx_.refreshInsights();
            *ctx_.insightRefreshTimer = *ctx_.insightRefreshInterval;
        }
        ImGui::SameLine();
        if (*ctx_.insightsLoading)
        {
            float time = (float)ImGui::GetTime();
            const char* spinner = "|/-\\";
            ImGui::Text("Loading %c", spinner[(int)(time * 4.f) % 4]);
        }
        else
            ImGui::TextDisabled("(%zu insights)", ctx_.recommendations->size());

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.f);
        ImGui::TextDisabled("%.0fs", *ctx_.insightRefreshTimer);

        if (ctx_.newsService && !ctx_.newsService->HasApiKey())
        {
            ImGui::Separator();
            ImGui::TextColored(ui::kColorWarning, "Set GNEWS_API_KEY env var for live news data");
        }

        ImGui::Separator();

        if (ctx_.recommendations->empty())
        {
            ImGui::TextDisabled("No recommendations yet. Add active strategies and refresh.");
        }
        else
        {
            for (auto& rec : *ctx_.recommendations)
            {
                ImGui::PushID(&rec);
                ImGui::TextColored(ui::kColorAccent, "[%s]", rec.symbol.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ui::kColorBullish, "%s", rec.title.c_str());

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


    void MarketWarningsPanel::Draw(bool* open)
    {
        bool hasAlerts = std::any_of(ctx_.warnings->begin(), ctx_.warnings->end(),
            [](const MarketInsight& w) { return w.severity == InsightSeverity::Alert; });

        if (hasAlerts)
        {
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ui::kAlertTitleActive);
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ui::kAlertTitleBg);
        }

        ImGui::Begin("Market Warnings", open);
        if (hasAlerts) ImGui::PopStyleColor(2);

        DrawContent();
        ImGui::End();
    }

    void MarketWarningsPanel::DrawContent()
    {
        ImGui::TextDisabled("(%zu warnings)", ctx_.warnings->size());
        ImGui::Separator();

        if (ctx_.warnings->empty())
        {
            ImGui::TextColored(ui::kColorBullish, "No warnings - all clear");
        }
        else
        {
            for (auto& warn : *ctx_.warnings)
            {
                ImGui::PushID(&warn);
                ImVec4 sevColor = ui::SeverityColor(warn.severity);
                const char* sevIcon;
                switch (warn.severity)
                {
                case InsightSeverity::Alert:   sevIcon = "[!]"; break;
                case InsightSeverity::Warning: sevIcon = "[*]"; break;
                default:                       sevIcon = "[i]"; break;
                }

                ImGui::TextColored(sevColor, "%s", sevIcon);
                ImGui::SameLine();
                ImGui::TextColored(ui::kColorAccent, "[%s]", warn.symbol.c_str());
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
    }


    void AIOperationsPanel::Draw(bool* open)
    {
        ImGui::Begin("AI Operations", open);

        // Live trading toggle
        if (ImGui::Checkbox("Live Trading", ctx_.liveTradingEnabled))
        {
            if (ctx_.engine->globals_)
                ctx_.engine->globals_->Set(gk::prefix::STATE, gk::key::LIVE_TRADING,
                    *ctx_.liveTradingEnabled ? "1" : "0");
        }
        ImGui::SameLine();
        if (*ctx_.liveTradingEnabled)
            ImGui::TextColored(ui::kColorBearish, "LIVE - Real orders will be sent!");
        else
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.f), "DRY-RUN - No real orders");

        // Auto-trade toggle
        if (ImGui::Checkbox("AI Auto-Trade", ctx_.aiAutoTrade))
        {
            if (ctx_.engine->globals_)
                ctx_.engine->globals_->Set(gk::prefix::STATE, gk::key::AI_AUTO_TRADE,
                    *ctx_.aiAutoTrade ? "1" : "0");
        }
        ImGui::SameLine();
        if (*ctx_.aiAutoTrade && *ctx_.liveTradingEnabled)
            ImGui::TextColored(ui::kColorBearish, "AI will auto-execute trades!");
        else if (*ctx_.aiAutoTrade)
            ImGui::TextDisabled("AI auto-trade ON but live trading OFF (dry-run)");
        else
            ImGui::TextDisabled("Disabled - AI suggestions only");

        ImGui::Separator();

        if (ctx_.operations->empty())
        {
            ImGui::TextDisabled("No AI operations pending.");
            ImGui::TextDisabled("Add AI-type strategies and wait for analysis cycle.");
        }
        else
        {
            static constexpr const char* kUrgencyLabels[] = {"RECOMMENDATIONS", "SHOULD ACT", "EMERGENCY"};

            for (int sev = (int)InsightSeverity::Alert; sev >= (int)InsightSeverity::Info; --sev)
            {
                auto severity = (InsightSeverity)sev;
                bool hasAny = false;
                for (auto& op : *ctx_.operations)
                    if (op.urgency == severity) { hasAny = true; break; }
                if (!hasAny) continue;

                ImGui::TextColored(ui::SeverityColor(severity), "--- %s ---", kUrgencyLabels[sev]);
                ImGui::Spacing();

                for (auto& op : *ctx_.operations)
                {
                    if (op.urgency != severity) continue;
                    ImGui::PushID(&op);

                    ImGui::TextColored(ui::OperationTypeColor(op.type), "[%s]", ui::OperationTypeLabel(op.type));
                    ImGui::SameLine();
                    ImGui::TextColored(ui::kColorAccent, "%s", op.symbol.c_str());
                    ImGui::SameLine();

                    if (op.suggestedPrice > 0.f)
                    { char pb[32]; ImGui::Text("@ %s", FmtPrice(pb, sizeof(pb), op.suggestedPrice, op.symbol)); }

                    if (op.strategyId > 0)
                    { ImGui::SameLine(); ImGui::TextDisabled("(#%lld)", (long long)op.strategyId); }

                    if (op.confidence > 0.f)
                    { ImGui::SameLine(); ImGui::TextDisabled("%.0f%%", op.confidence * 100.f); }

                    if (!op.reason.empty())
                    {
                        ImGui::Indent(20.f);
                        ImGui::TextWrapped("%s", op.reason.c_str());
                        ImGui::Unindent(20.f);
                    }

                    if (op.executed)
                    {
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
                        ImGui::TextColored(ui::kColorBullish, "DONE");
                    }
                    else if (!*ctx_.aiAutoTrade)
                    {
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.f);
                        if (ImGui::SmallButton("Execute"))
                        {
                            // Execute through context
                            if (ctx_.executeOrder && op.type != OperationType::Hold)
                            {
                                OrderSide side = (op.type == OperationType::Buy) ? OrderSide::Buy : OrderSide::Sell;
                                BrokerSource broker = BrokerSource::Auto;
                                float quantity = 1.f;
                                if (op.strategyId > 0)
                                {
                                    for (auto& s : *ctx_.strategies)
                                        if (s.id == op.strategyId) { broker = s.broker; quantity = s.quantity; break; }
                                }
                                if (quantity <= 0.f) quantity = 1.f;
                                float price = (op.type == OperationType::AdjustTP || op.type == OperationType::AdjustSL)
                                    ? op.suggestedPrice : 0.f;
                                if (ctx_.executeOrder(op.symbol, broker, side, quantity, price))
                                    op.executed = true;
                            }
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

} // namespace stnks
