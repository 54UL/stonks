#include <UI/MarketInsightsPanel.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>
#include <cstdio>
#include <algorithm>

namespace stnks
{
    MarketInsightsPanel::MarketInsightsPanel(UIContext& ctx)
        : ctx_(ctx)
        , eventsPanel_(ctx)
        , signalsPanel_(ctx)
        , warningsPanel_(ctx)
    {}

    int MarketInsightsPanel::GetEventCount() const
    {
        return ctx_.graphEvents ? ctx_.graphEvents->UnreadCount() : 0;
    }

    int MarketInsightsPanel::GetSignalCount() const
    {
        return ctx_.signalService ? ctx_.signalService->ActionableCount() : 0;
    }

    int MarketInsightsPanel::GetWarningCount() const
    {
        return ctx_.warnings ? (int)ctx_.warnings->size() : 0;
    }

    void MarketInsightsPanel::Draw(bool* open)
    {
        int evtCount  = GetEventCount();
        int sigCount  = GetSignalCount();
        int warnCount = GetWarningCount();
        int totalBadge = evtCount + sigCount + warnCount;

        // Alert title bar styling when there are critical items
        bool hasAlert = false;
        if (ctx_.signalService)
        {
            for (auto& s : ctx_.signalService->GetAll())
                if (s.severity == SignalSeverity::Alert && !s.dismissed && !s.accepted)
                { hasAlert = true; break; }
        }
        if (!hasAlert && ctx_.warnings)
        {
            hasAlert = std::any_of(ctx_.warnings->begin(), ctx_.warnings->end(),
                [](const MarketInsight& w) { return w.severity == InsightSeverity::Alert; });
        }

        if (hasAlert)
        {
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ui::kAlertTitleActive);
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ui::kAlertTitleBg);
        }

        char title[64];
        if (totalBadge > 0)
            snprintf(title, sizeof(title), "Market Insights (%d)###MarketInsights", totalBadge);
        else
            snprintf(title, sizeof(title), "Market Insights###MarketInsights");

        if (!ImGui::Begin(title, open))
        {
            if (hasAlert) ImGui::PopStyleColor(2);
            ImGui::End();
            focusRequested_ = false;
            return;
        }
        if (hasAlert) ImGui::PopStyleColor(2);

        // Focus the window if requested (from chart event click)
        if (focusRequested_)
        {
            ImGui::SetWindowFocus();
            focusRequested_ = false;
        }

        if (ImGui::BeginTabBar("##InsightsTabBar"))
        {
            // ── Events tab ──
            {
                char evtLabel[48];
                if (evtCount > 0)
                    snprintf(evtLabel, sizeof(evtLabel), "Events (%d)###EventsTab", evtCount);
                else
                    snprintf(evtLabel, sizeof(evtLabel), "Events###EventsTab");

                ImGuiTabItemFlags flags = 0;
                if (pendingTab_ == (int)InsightsTab::Events)
                { flags = ImGuiTabItemFlags_SetSelected; pendingTab_ = -1; }

                if (ImGui::BeginTabItem(evtLabel, nullptr, flags))
                {
                    eventsPanel_.DrawContent();
                    ImGui::EndTabItem();
                }
            }

            // ── Signals tab ──
            {
                char sigLabel[48];
                if (sigCount > 0)
                    snprintf(sigLabel, sizeof(sigLabel), "Signals (%d)###SignalsTab", sigCount);
                else
                    snprintf(sigLabel, sizeof(sigLabel), "Signals###SignalsTab");

                ImGuiTabItemFlags flags = 0;
                if (pendingTab_ == (int)InsightsTab::Signals)
                { flags = ImGuiTabItemFlags_SetSelected; pendingTab_ = -1; }

                if (ImGui::BeginTabItem(sigLabel, nullptr, flags))
                {
                    signalsPanel_.DrawContent();
                    ImGui::EndTabItem();
                }
            }

            // ── Warnings tab ──
            {
                char warnLabel[48];
                if (warnCount > 0)
                    snprintf(warnLabel, sizeof(warnLabel), "Warnings (%d)###WarningsTab", warnCount);
                else
                    snprintf(warnLabel, sizeof(warnLabel), "Warnings###WarningsTab");

                ImGuiTabItemFlags flags = 0;
                if (pendingTab_ == (int)InsightsTab::Warnings)
                { flags = ImGuiTabItemFlags_SetSelected; pendingTab_ = -1; }

                if (ImGui::BeginTabItem(warnLabel, nullptr, flags))
                {
                    warningsPanel_.DrawContent();
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

} // namespace stnks
