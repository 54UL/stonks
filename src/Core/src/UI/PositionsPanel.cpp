#include <UI/PositionsPanel.hpp>
#include <UI/UIConstants.hpp>
#include <Market/MarketService.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <ctime>
#include <cmath>
#include <cstdio>

namespace stnks
{
    PositionsPanel::PositionsPanel(UIContext& ctx) : ctx_(ctx) {}

    void PositionsPanel::Draw(bool* open)
    {
        if (!ImGui::Begin("Broker Positions", open))
        {
            ImGui::End();
            return;
        }

        // Auto-refresh timer
        float dt = ImGui::GetIO().DeltaTime;
        refreshTimer_ += dt;
        if (needsRefresh_ || refreshTimer_ >= refreshInterval_)
        {
            RefreshPositions();
            refreshTimer_ = 0.f;
            needsRefresh_ = false;
        }

        DrawToolbar();
        ImGui::Separator();
        DrawPositionsTable();

        ImGui::End();
    }

    void PositionsPanel::RefreshPositions()
    {
        cachedPositions_.clear();

        if (!ctx_.marketService) return;

        // Iterate all MarketService sources, find broker connectors, fetch positions
        for (auto& src : ctx_.marketService->GetSources())
        {
            auto* connector = dynamic_cast<IBrokerConnector*>(src.get());
            if (!connector) continue;

            // Only fetch if connected
            if (connector->GetConnectionState() != WsState::Connected)
                continue;

            try
            {
                auto positions = connector->GetPositions();
                for (auto& pos : positions)
                {
                    cachedPositions_.push_back({std::move(pos), connector->GetName()});
                }
            }
            catch (const std::exception& e)
            {
                spdlog::warn("[Positions] Failed to fetch from {}: {}",
                             connector->GetName(), e.what());
            }
        }
    }

    void PositionsPanel::DrawToolbar()
    {
        // Position count
        ImGui::Text("%d position(s)", (int)cachedPositions_.size());
        ImGui::SameLine();

        // Refresh button
        if (ImGui::SmallButton("Refresh"))
            needsRefresh_ = true;

        ImGui::SameLine();

        // Refresh interval
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##interval", &refreshInterval_, 5.f, 120.f, "%.0fs");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Auto-refresh interval");

        // Show connected brokers summary
        if (ctx_.marketService)
        {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
            int connected = 0;
            for (auto& src : ctx_.marketService->GetSources())
            {
                auto* c = dynamic_cast<IBrokerConnector*>(src.get());
                if (c && c->GetConnectionState() == WsState::Connected)
                    connected++;
            }
            ImGui::TextDisabled("%d broker(s) connected", connected);
        }
    }

    void PositionsPanel::DrawPositionsTable()
    {
        if (cachedPositions_.empty())
        {
            ImGui::TextDisabled("No positions found. Connect a broker and click Refresh.");
            return;
        }

        const int colCount = 7;
        ImGuiTableFlags flags = ImGuiTableFlags_Borders
                              | ImGuiTableFlags_RowBg
                              | ImGuiTableFlags_Resizable
                              | ImGuiTableFlags_ScrollY
                              | ImGuiTableFlags_SizingFixedFit;

        if (!ImGui::BeginTable("##PositionsTable", colCount, flags))
            return;

        ImGui::TableSetupColumn("Broker",       ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("Symbol",        ImGuiTableColumnFlags_WidthFixed, 120.f);
        ImGui::TableSetupColumn("Qty",           ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("Avg Cost",      ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableSetupColumn("Market Value",  ImGuiTableColumnFlags_WidthFixed, 100.f);
        ImGui::TableSetupColumn("P/L",           ImGuiTableColumnFlags_WidthFixed, 100.f);
        ImGui::TableSetupColumn("P/L %",         ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (auto& entry : cachedPositions_)
            DrawPositionRow(entry);

        // Totals row
        float totalValue = 0.f, totalPnL = 0.f, totalCost = 0.f;
        for (auto& e : cachedPositions_)
        {
            totalValue += e.pos.marketValue;
            totalPnL   += e.pos.unrealizedPnL;
            totalCost  += e.pos.avgCost * e.pos.quantity;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Total");
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("$%.2f", totalValue);
        ImGui::TableSetColumnIndex(5);
        ImVec4 totalCol = totalPnL >= 0.f ? ui::kColorBullish : ui::kColorBearish;
        ImGui::TextColored(totalCol, "%s$%.2f", totalPnL >= 0.f ? "+" : "", totalPnL);
        ImGui::TableSetColumnIndex(6);
        float totalPct = totalCost > 0.f ? (totalPnL / totalCost) * 100.f : 0.f;
        ImGui::TextColored(totalCol, "%+.2f%%", totalPct);

        ImGui::EndTable();
    }

    void PositionsPanel::DrawPositionRow(const BrokerPositionEntry& entry)
    {
        auto& pos = entry.pos;

        ImGui::TableNextRow();
        ImGui::PushID(pos.symbol.c_str());

        // Broker
        ImGui::TableSetColumnIndex(0);
        if (entry.brokerName == "Binance")
            ImGui::TextColored(ImVec4(0.96f, 0.76f, 0.07f, 1.f), "%s", entry.brokerName.c_str());
        else if (entry.brokerName == "MetaTrader 5")
            ImGui::TextColored(ImVec4(0.30f, 0.75f, 0.40f, 1.f), "%s", entry.brokerName.c_str());
        else
            ImGui::Text("%s", entry.brokerName.c_str());

        // Symbol
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", pos.symbol.c_str());

        // Quantity
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.4g", pos.quantity);

        // Avg Cost
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("$%.2f", pos.avgCost);

        // Market Value
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("$%.2f", pos.marketValue);

        // P/L
        ImGui::TableSetColumnIndex(5);
        ImVec4 pnlCol = pos.unrealizedPnL >= 0.f ? ui::kColorBullish : ui::kColorBearish;
        ImGui::TextColored(pnlCol, "%s$%.2f",
            pos.unrealizedPnL >= 0.f ? "+" : "", pos.unrealizedPnL);

        // P/L %
        ImGui::TableSetColumnIndex(6);
        float cost = pos.avgCost * pos.quantity;
        float pnlPct = cost > 0.f ? (pos.unrealizedPnL / cost) * 100.f : 0.f;
        ImGui::TextColored(pnlCol, "%+.2f%%", pnlPct);

        ImGui::PopID();
    }

} // namespace stnks
