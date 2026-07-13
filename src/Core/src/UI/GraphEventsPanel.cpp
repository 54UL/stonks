#include <UI/GraphEventsPanel.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <cstdio>

namespace stnks
{
    // EventSource -> display label and color
    static void EventSourceBadge(EventSource src, const char*& label, ImVec4& col)
    {
        switch (src)
        {
        case EventSource::Strategy:  label = "STRAT";  col = {0.9f,0.6f,0.2f,1.f}; break;
        case EventSource::Candle:    label = "CANDLE"; col = {0.3f,0.8f,0.5f,1.f}; break;
        case EventSource::Volume:    label = "VOL";    col = {0.5f,0.5f,0.9f,1.f}; break;
        case EventSource::RSI:       label = "RSI";    col = {0.8f,0.4f,0.8f,1.f}; break;
        case EventSource::MACD:      label = "MACD";   col = {0.3f,0.7f,0.9f,1.f}; break;
        case EventSource::EMA:       label = "EMA";    col = {0.9f,0.7f,0.3f,1.f}; break;
        case EventSource::Bollinger: label = "BB";     col = {0.6f,0.9f,0.6f,1.f}; break;
        case EventSource::Pattern:   label = "PATRN";  col = {1.f,0.5f,0.8f,1.f}; break;
        case EventSource::VolProfile:label = "VP";     col = {0.7f,0.8f,1.f,1.f}; break;
        default:                     label = "?";      col = {0.6f,0.6f,0.6f,1.f}; break;
        }
    }

    void GraphEventsPanel::Draw(bool* open)
    {
        int unread = ctx_.graphEvents->UnreadCount();
        char title[64];
        if (unread > 0)
            snprintf(title, sizeof(title), "Graph Events (%d)###GraphEvents", unread);
        else
            snprintf(title, sizeof(title), "Graph Events###GraphEvents");

        if (!ImGui::Begin(title, open)) { ImGui::End(); return; }
        DrawContent();
        ImGui::End();
    }

    void GraphEventsPanel::DrawContent()
    {
        // Toolbar
        if (ImGui::Button("Clear All"))
            ctx_.graphEvents->Clear();
        ImGui::SameLine();
        if (ImGui::Button("Mark Read"))
            ctx_.graphEvents->ConsumeNew();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        int trIdx = (int)*ctx_.eventTimeRange;
        if (ImGui::Combo("##EventTimeRange", &trIdx, ui::kEventTimeRangeLabels, ui::kEventTimeRangeCount))
            *ctx_.eventTimeRange = (ui::EventTimeRange)trIdx;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Filter events by time window.\nAlso determines AI analysis context.");

        ImGui::Separator();

        auto events = ctx_.graphEvents->GetAllEvents();
        int64_t rangeSec = ui::EventTimeRangeSeconds(*ctx_.eventTimeRange);
        if (rangeSec > 0)
        {
            int64_t cutoff = std::time(nullptr) - rangeSec;
            events.erase(std::remove_if(events.begin(), events.end(),
                [cutoff](const GraphEvent& e) { return e.timestamp < cutoff; }), events.end());
        }

        if (events.empty())
        {
            ImGui::TextDisabled("No events detected yet.");
            ImGui::TextDisabled("Events appear as charts are scanned for patterns.");
            return;
        }

        if (ImGui::BeginTable("##GraphEventsTable", 8,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Sev",     ImGuiTableColumnFlags_WidthFixed, 28.f);
            ImGui::TableSetupColumn("Source",  ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("Symbol",  ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("Event",   ImGuiTableColumnFlags_WidthFixed, 160.f);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Score",   ImGuiTableColumnFlags_WidthFixed, 40.f);
            ImGui::TableSetupColumn("Time",    ImGuiTableColumnFlags_WidthFixed, 60.f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150.f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)events.size(); ++i)
                DrawEventRow(events[i], i);

            ImGui::EndTable();
        }
    }

    void GraphEventsPanel::DrawEventRow(GraphEvent& ev, int evIdx)
    {
        ImGui::PushID(evIdx);
        ImGui::TableNextRow();

        // Severity
        ImGui::TableNextColumn();
        ImVec4 sevCol = ui::SeverityColor(ev.severity);
        const char* sevIcon = ui::SeverityIcon(ev.severity);

        bool clicked = ImGui::Selectable("##evtRow", false,
            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
        ImGui::SameLine();
        ImGui::TextColored(sevCol, "%s", sevIcon);

        if (clicked)
        {
            bool found = false;
            for (auto& panel : *ctx_.charts)
            {
                if (panel.symbol == ev.symbol)
                {
                    if (ev.candleIdx >= 0) panel.chart.ScrollToCandle(ev.candleIdx);
                    found = true;
                    break;
                }
            }
            if (!found && !ev.symbol.empty() && ctx_.fetchSymbol)
                ctx_.fetchSymbol(ev.symbol, "1d", "6mo");
        }

        // Source badge
        ImGui::TableNextColumn();
        const char* srcLabel; ImVec4 srcCol;
        EventSourceBadge(ev.source, srcLabel, srcCol);
        ImGui::TextColored(srcCol, "%s", srcLabel);

        // Symbol, Event title
        ImGui::TableNextColumn(); ImGui::TextUnformatted(ev.symbol.c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(ev.title.c_str());

        // Description (truncated)
        ImGui::TableNextColumn();
        if (!ev.detail.empty())
        {
            float colW = ImGui::GetContentRegionAvail().x;
            ImVec2 textSize = ImGui::CalcTextSize(ev.detail.c_str(), nullptr, false, 0.f);
            if (textSize.x > colW && colW > 20.f)
            {
                std::string trunc = ev.detail;
                while (!trunc.empty())
                {
                    ImVec2 ts = ImGui::CalcTextSize((trunc + "...").c_str(), nullptr, false, 0.f);
                    if (ts.x <= colW) break;
                    trunc.pop_back();
                }
                ImGui::TextUnformatted((trunc + "...").c_str());
            }
            else
                ImGui::TextUnformatted(ev.detail.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ev.detail.c_str());
        }
        else
            ImGui::TextDisabled("-");

        // Score
        ImGui::TableNextColumn();
        if ((ev.source == EventSource::Pattern || ev.source == EventSource::VolProfile)
            && ev.score > 0.f && ev.score < 1.f)
        {
            ImVec4 sc = ev.score >= 0.7f ? ui::kColorBullish
                      : ev.score >= 0.5f ? ui::kColorWarning : ui::kColorBearish;
            ImGui::TextColored(sc, "%.0f%%", ev.score * 100.f);
        }
        else
            ImGui::TextDisabled("-");

        // Time
        ImGui::TableNextColumn();
        int64_t ago = std::time(nullptr) - ev.timestamp;
        if (ago < 60)         ImGui::Text("%ds ago", (int)ago);
        else if (ago < 3600)  ImGui::Text("%dm ago", (int)(ago / 60));
        else if (ago < 86400) ImGui::Text("%dh ago", (int)(ago / 3600));
        else                  ImGui::Text("%dd ago", (int)(ago / 86400));

        // Actions
        DrawActionButtons(ev);

        ImGui::PopID();
    }

    void GraphEventsPanel::DrawActionButtons(const GraphEvent& ev)
    {
        ImGui::TableNextColumn();
        if (ev.symbol.empty()) return;

        // View button — scroll chart to the event candle
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.35f, 0.6f, 1.f));
        if (ImGui::SmallButton("View"))
        {
            bool found = false;
            for (auto& panel : *ctx_.charts)
            {
                if (panel.symbol == ev.symbol)
                {
                    if (ev.candleIdx >= 0) panel.chart.ScrollToCandle(ev.candleIdx);
                    found = true;
                    break;
                }
            }
            // Try detached charts
            if (!found)
            {
                for (auto& dc : *ctx_.detachedCharts)
                {
                    if (dc.symbol == ev.symbol)
                    {
                        if (ev.candleIdx >= 0) dc.chart.ScrollToCandle(ev.candleIdx);
                        found = true;
                        break;
                    }
                }
            }
            // No chart open for this symbol — fetch it
            if (!found && ctx_.fetchSymbol)
                ctx_.fetchSymbol(ev.symbol, "1d", "6mo");
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.3f, 1.f));
        if (ImGui::SmallButton("Long"))
        {
            float price = ctx_.getCurrentPrice(ev.symbol);
            StrategyType type = (ev.source == EventSource::Pattern || ev.source == EventSource::VolProfile)
                ? StrategyType::TPSL : StrategyType::Position;
            ctx_.wizard->OpenCreate(ev.symbol, type, StrategyDirection::Long, price);
            *ctx_.showStrategyWizard = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.f));
        if (ImGui::SmallButton("Short"))
        {
            float price = ctx_.getCurrentPrice(ev.symbol);
            StrategyType type = (ev.source == EventSource::Pattern || ev.source == EventSource::VolProfile)
                ? StrategyType::TPSL : StrategyType::Position;
            ctx_.wizard->OpenCreate(ev.symbol, type, StrategyDirection::Short, price);
            *ctx_.showStrategyWizard = true;
        }
        ImGui::PopStyleColor();
    }

} // namespace stnks
