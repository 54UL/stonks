#include <UI/PortfolioPanel.hpp>
#include <UI/UIConstants.hpp>
#include <Market/MarketHours.hpp>
#include <imgui.h>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <ctime>
#include <cstdio>

namespace stnks
{
    PortfolioPanel::PortfolioPanel(UIContext& ctx) : ctx_(ctx) {}

    void PortfolioPanel::Draw(bool* open)
    {
        ImGui::Begin("Portfolio", open);

        std::vector<Holding> holdings;
        ComputeHoldings(holdings);

        if (holdings.empty())
        {
            ImGui::TextDisabled("No active positions with quantity.");
            ImGui::TextDisabled("Create Position or AI strategies with quantity > 0.");
            ImGui::End();
            return;
        }

        // Split by currency
        std::vector<Holding*> usdHoldings, mxnHoldings;
        float usdValue = 0.f, usdCost = 0.f, usdPnl = 0.f;
        float mxnValue = 0.f, mxnCost = 0.f, mxnPnl = 0.f;

        for (auto& h : holdings)
        {
            if (h.market == MarketType::Mexico)
            {
                mxnHoldings.push_back(&h);
                mxnValue += h.marketValue; mxnCost += h.totalCost; mxnPnl += h.pnl;
            }
            else
            {
                usdHoldings.push_back(&h);
                usdValue += h.marketValue; usdCost += h.totalCost; usdPnl += h.pnl;
            }
        }

        DrawSection("USD  (US + Crypto)", "$",
                    usdHoldings, usdValue, usdCost, usdPnl,
                    false, ImVec4(0.3f, 0.5f, 0.9f, 1.f));
        DrawSection("MXN  (Mexico)", "MX$",
                    mxnHoldings, mxnValue, mxnCost, mxnPnl,
                    true, ui::kColorWarning);

        ImGui::End();
    }


    void PortfolioPanel::ComputeHoldings(std::vector<Holding>& holdings)
    {
        std::unordered_map<std::string, size_t> symbolIdx;

        for (auto& s : *ctx_.strategies)
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
                h->market = MarketHours::ClassifySymbol(s.symbol);
            }
            else
                h = &holdings[it->second];

            h->totalCost += s.EffectiveEntryPrice() * s.quantity;
            h->totalQty  += s.quantity;
            h->activeStrats++;
            if (s.IsAI()) h->aiStrats++;
        }

        for (auto& h : holdings)
        {
            if (h.totalQty > 0.f) h.avgEntry = h.totalCost / h.totalQty;
            h.currentPrice = ctx_.getCurrentPrice(h.symbol);
            if (h.currentPrice > 0.f)
            {
                h.pnl = 0.f;
                for (auto& s : *ctx_.strategies)
                {
                    if (!s.IsActive() || s.symbol != h.symbol || s.quantity <= 0.f) continue;
                    h.pnl += s.UnrealizedPnL(h.currentPrice);
                }
                h.marketValue = h.totalCost + h.pnl;
                h.pnlPct = (h.totalCost > 0.f) ? (h.pnl / h.totalCost) * 100.f : 0.f;
            }
        }
    }


    PortfolioPanel::PeriodPnL PortfolioPanel::ComputePeriodPnL(bool isMexican, int64_t cutoff)
    {
        PeriodPnL result;
        for (auto& s : *ctx_.strategies)
        {
            if (s.IsActive()) continue;
            if (s.triggeredAt <= 0 || s.triggeredAt < cutoff) continue;
            if (s.quantity <= 0.f) continue;

            bool isMx = (MarketHours::ClassifySymbol(s.symbol) == MarketType::Mexico);
            if (isMx != isMexican) continue;

            float pnl = (s.exitPrice > 0.f && s.entryPrice > 0.f)
                ? s.UnrealizedPnL(s.EffectiveExitPrice()) : 0.f;
            result.realized += pnl;
            result.trades++;
            if (pnl >= 0.f) result.wins++;
        }
        return result;
    }


    void PortfolioPanel::DrawSection(
        const char* title, const char* currSym,
        std::vector<Holding*>& hlist,
        float secValue, float secCost, float secPnl,
        bool isMexican, ImVec4 accentCol)
    {
        float secPnlPct = (secCost > 0.f) ? (secPnl / secCost) * 100.f : 0.f;

        ImGui::PushStyleColor(ImGuiCol_Header,
            ImVec4(accentCol.x * 0.3f, accentCol.y * 0.3f, accentCol.z * 0.3f, 0.6f));
        bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();
        if (!open) return;

        ImGui::Indent(4.f);
        DrawSummaryCards(currSym, secValue, secPnl, secPnlPct, (int)hlist.size());
        DrawCumulativePnLChart(currSym, isMexican);
        DrawLiveValueChart(currSym, isMexican);
        ImGui::Spacing();
        DrawPeriodTabs(currSym, isMexican);
        ImGui::Spacing();
        DrawHoldingsTable(hlist, currSym, isMexican);
        ImGui::Unindent(4.f);
        ImGui::Spacing();
    }


    void PortfolioPanel::DrawSummaryCards(
        const char* currSym, float secValue,
        float secPnl, float secPnlPct, int positions)
    {
        ImGui::Spacing();

        ImGui::BeginGroup();
        ImGui::TextDisabled("Total Value");
        char vb[32]; snprintf(vb, sizeof(vb), "%s%.2f", currSym, secValue);
        ImGui::Text("%s", vb);
        ImGui::EndGroup();

        ImGui::SameLine(0.f, 24.f);

        ImGui::BeginGroup();
        ImGui::TextDisabled("Unrealized P/L");
        ImVec4 pCol = ui::PnLColor(secPnl);
        char pb[48]; snprintf(pb, sizeof(pb), "%c%s%.2f (%+.2f%%)",
            secPnl >= 0 ? '+' : '-', currSym, std::fabs(secPnl), secPnlPct);
        ImGui::TextColored(pCol, "%s", pb);
        ImGui::EndGroup();

        ImGui::SameLine(0.f, 24.f);

        ImGui::BeginGroup();
        ImGui::TextDisabled("Positions");
        ImGui::Text("%d", positions);
        ImGui::EndGroup();

        ImGui::Spacing();
    }


    void PortfolioPanel::DrawCumulativePnLChart(const char* currSym, bool isMexican)
    {
        struct ClosedTrade { int64_t ts; float pnl; };
        std::vector<ClosedTrade> closed;
        for (auto& s : *ctx_.strategies)
        {
            if (s.IsActive() || s.triggeredAt <= 0 || s.quantity <= 0.f) continue;
            bool isMx = (MarketHours::ClassifySymbol(s.symbol) == MarketType::Mexico);
            if (isMx != isMexican) continue;
            float pnl = (s.exitPrice > 0.f && s.entryPrice > 0.f)
                ? s.UnrealizedPnL(s.EffectiveExitPrice()) : 0.f;
            closed.push_back({s.triggeredAt, pnl});
        }
        std::sort(closed.begin(), closed.end(),
            [](const ClosedTrade& a, const ClosedTrade& b) { return a.ts < b.ts; });

        int startIdx = (int)closed.size() > 60 ? (int)closed.size() - 60 : 0;
        std::vector<float> data = {0.f};
        float cum = 0.f;
        for (int i = startIdx; i < (int)closed.size(); ++i)
        { cum += closed[i].pnl; data.push_back(cum); }

        if (data.size() < 2) return;

        ImGui::TextDisabled("Cumulative P/L (last %d trades)", (int)closed.size() - startIdx);
        float gW = ImGui::GetContentRegionAvail().x, gH = 60.f;
        ImVec2 gp = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(gp, ImVec2(gp.x + gW, gp.y + gH), IM_COL32(14, 16, 22, 255), 4.f);
        dl->AddRect(gp, ImVec2(gp.x + gW, gp.y + gH), IM_COL32(50, 55, 70, 150), 4.f);

        float vMin = data[0], vMax = data[0];
        for (auto v : data) { vMin = std::min(vMin, v); vMax = std::max(vMax, v); }
        float vRange = vMax - vMin;
        if (vRange < 0.01f) vRange = 1.f;
        float pad = vRange * 0.1f;
        vMin -= pad; vMax += pad; vRange = vMax - vMin;

        float zeroY = gp.y + gH - ((0.f - vMin) / vRange) * gH;
        if (zeroY > gp.y && zeroY < gp.y + gH)
            dl->AddLine(ImVec2(gp.x, zeroY), ImVec2(gp.x + gW, zeroY), IM_COL32(100, 100, 120, 80));

        int n = (int)data.size();
        bool positive = data.back() >= 0.f;
        ImU32 lineCol = positive ? IM_COL32(50, 210, 120, 255) : IM_COL32(230, 80, 80, 255);
        ImU32 fillCol = positive ? IM_COL32(50, 210, 120, 30) : IM_COL32(230, 80, 80, 30);

        for (int i = 0; i < n - 1; ++i)
        {
            float x0 = gp.x + ((float)i / (n - 1)) * gW;
            float x1 = gp.x + ((float)(i + 1) / (n - 1)) * gW;
            float y0 = gp.y + gH - ((data[i] - vMin) / vRange) * gH;
            float y1 = gp.y + gH - ((data[i + 1] - vMin) / vRange) * gH;
            dl->AddQuadFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, zeroY), ImVec2(x0, zeroY), fillCol);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), lineCol, 1.5f);
        }

        char lb[48]; snprintf(lb, sizeof(lb), "%c%s%.2f",
            data.back() >= 0 ? '+' : '-', currSym, std::fabs(data.back()));
        float lastY = gp.y + gH - ((data.back() - vMin) / vRange) * gH;
        dl->AddText(ImVec2(gp.x + gW - ImGui::CalcTextSize(lb).x - 4, lastY - 14), lineCol, lb);

        ImGui::Dummy(ImVec2(gW, gH + 4.f));
    }


    void PortfolioPanel::DrawLiveValueChart(const char* currSym, bool isMexican)
    {
        std::vector<float> rtData;
        for (auto& sample : *ctx_.portfolioHistory)
            rtData.push_back(isMexican ? sample.mxnValue : sample.usdValue);

        if (rtData.size() < 2)
        {
            ImGui::TextDisabled("Live Value: collecting samples (every 15s)...");
            return;
        }

        float elapsed = ctx_.portfolioHistory->back().timestamp - ctx_.portfolioHistory->front().timestamp;
        int elMin = (int)(elapsed / 60.f), elSec = (int)std::fmod(elapsed, 60.f);
        char label[64];
        if (elMin > 0)
            snprintf(label, sizeof(label), "Live Value (%dm %ds, %d samples)", elMin, elSec, (int)rtData.size());
        else
            snprintf(label, sizeof(label), "Live Value (%ds, %d samples)", elSec, (int)rtData.size());
        ImGui::TextDisabled("%s", label);

        float gW = ImGui::GetContentRegionAvail().x, gH = 70.f;
        ImVec2 gp = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(gp, ImVec2(gp.x + gW, gp.y + gH), IM_COL32(14, 16, 22, 255), 4.f);
        dl->AddRect(gp, ImVec2(gp.x + gW, gp.y + gH), IM_COL32(50, 55, 70, 150), 4.f);

        float vMin = rtData[0], vMax = rtData[0];
        for (auto v : rtData) { vMin = std::min(vMin, v); vMax = std::max(vMax, v); }
        float vRange = vMax - vMin;
        if (vRange < 0.01f) { vRange = std::max(vMax * 0.01f, 1.f); vMin -= vRange * 0.5f; vMax += vRange * 0.5f; vRange = vMax - vMin; }
        float pad = vRange * 0.1f;
        vMin -= pad; vMax += pad; vRange = vMax - vMin;

        int n = (int)rtData.size();
        float first = rtData.front(), last = rtData.back();
        bool up = last >= first;
        ImU32 lineCol = up ? IM_COL32(80, 180, 255, 255) : IM_COL32(255, 140, 80, 255);
        ImU32 fillCol = up ? IM_COL32(80, 180, 255, 20) : IM_COL32(255, 140, 80, 20);

        float firstY = gp.y + gH - ((first - vMin) / vRange) * gH;
        for (float dx = 0; dx < gW; dx += 8.f)
            dl->AddLine(ImVec2(gp.x + dx, firstY), ImVec2(gp.x + std::min(dx + 4.f, gW), firstY),
                IM_COL32(100, 100, 120, 60));

        for (int i = 0; i < n - 1; ++i)
        {
            float x0 = gp.x + ((float)i / (n - 1)) * gW;
            float x1 = gp.x + ((float)(i + 1) / (n - 1)) * gW;
            float y0 = gp.y + gH - ((rtData[i] - vMin) / vRange) * gH;
            float y1 = gp.y + gH - ((rtData[i + 1] - vMin) / vRange) * gH;
            dl->AddQuadFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, firstY), ImVec2(x0, firstY), fillCol);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), lineCol, 2.f);
        }

        float lastYPos = gp.y + gH - ((last - vMin) / vRange) * gH;
        dl->AddCircleFilled(ImVec2(gp.x + gW - 2, lastYPos), 3.f, lineCol);

        char vb[32]; snprintf(vb, sizeof(vb), "%s%.2f", currSym, last);
        dl->AddText(ImVec2(gp.x + gW - ImGui::CalcTextSize(vb).x - 6, lastYPos - 14), lineCol, vb);

        float delta = last - first;
        float deltaPct = (first > 0.f) ? (delta / first) * 100.f : 0.f;
        char db[48]; snprintf(db, sizeof(db), "%c%s%.2f (%+.2f%%)",
            delta >= 0 ? '+' : '-', currSym, std::fabs(delta), deltaPct);
        ImU32 dCol = delta >= 0 ? IM_COL32(50, 210, 120, 200) : IM_COL32(230, 80, 80, 200);
        dl->AddText(ImVec2(gp.x + 4, gp.y + 4), dCol, db);

        ImGui::Dummy(ImVec2(gW, gH + 4.f));
    }


    void PortfolioPanel::DrawPeriodTabs(const char* currSym, bool isMexican)
    {
        ImGui::TextDisabled("Realized P/L");
        ImGui::SameLine();
        ImGui::TextDisabled("(closed trades)");

        int64_t now = std::time(nullptr);
        static constexpr int64_t kCutoffDeltas[] = {3600, 86400, 604800, 2592000, 31536000};
        static constexpr const char* kPeriodLabels[] = {"1H", "1D", "1W", "1M", "1Y"};

        char tabId[32]; snprintf(tabId, sizeof(tabId), "##PeriodTabs_%s", isMexican ? "MXN" : "USD");
        if (ImGui::BeginTabBar(tabId))
        {
            for (int ti = 0; ti < 5; ++ti)
            {
                char tabItemId[32]; snprintf(tabItemId, sizeof(tabItemId), "%s##%s",
                    kPeriodLabels[ti], isMexican ? "MXN" : "USD");
                if (ImGui::BeginTabItem(tabItemId))
                {
                    PeriodPnL pp = ComputePeriodPnL(isMexican, now - kCutoffDeltas[ti]);
                    if (pp.trades > 0)
                    {
                        ImVec4 rCol = ui::PnLColor(pp.realized);
                        char rb[48]; snprintf(rb, sizeof(rb), "%c%s%.2f",
                            pp.realized >= 0 ? '+' : '-', currSym, std::fabs(pp.realized));
                        ImGui::TextColored(rCol, "%s", rb);
                        ImGui::SameLine();
                        float winRate = (float)pp.wins / pp.trades * 100.f;
                        ImGui::TextDisabled("%d trades | %.0f%% win rate", pp.trades, winRate);
                    }
                    else
                        ImGui::TextDisabled("No closed trades in this period");
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }


    void PortfolioPanel::DrawHoldingsTable(
        std::vector<Holding*>& hlist, const char* currSym, bool isMexican)
    {
        if (hlist.empty()) { ImGui::TextDisabled("No active positions."); return; }

        ImGui::PushStyleColor(ImGuiCol_TableRowBg,    ui::kTableRowBg);
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ui::kTableRowBgAlt);

        char tableId[32]; snprintf(tableId, sizeof(tableId), "##Port_%s", isMexican ? "MXN" : "USD");
        if (ImGui::BeginTable(tableId, 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_HighlightHoveredColumn))
        {
            ImGui::TableSetupColumn("Symbol",     ImGuiTableColumnFlags_WidthFixed, 100.f);
            ImGui::TableSetupColumn("Qty",        ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("Avg Entry",  ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("Price",      ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("Value",      ImGuiTableColumnFlags_WidthFixed, 100.f);
            ImGui::TableSetupColumn("P/L",        ImGuiTableColumnFlags_WidthFixed, 100.f);
            ImGui::TableSetupColumn("P/L%",       ImGuiTableColumnFlags_WidthFixed, 65.f);
            ImGui::TableSetupColumn("Strategies",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int ri = 0; ri < (int)hlist.size(); ++ri)
            {
                auto& h = *hlist[ri];
                ImGui::TableNextRow();
                ImGui::PushID(ri + (isMexican ? 10000 : 0));

                ImVec4 plCol = ui::PnLColor(h.pnl);

                // Symbol (with row selectable)
                ImGui::TableNextColumn();
                ImGui::Selectable("##prow", false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
                bool rowHovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::GetIO().MouseClickedCount[0] == 2)
                    if (ctx_.fetchSymbol) ctx_.fetchSymbol(h.symbol, "1d", "6mo");
                ImGui::SameLine(0.f, 0.f);
                ImGui::TextColored(ui::MarketTypeColor(h.market), "%s", h.symbol.c_str());

                ImGui::TableNextColumn(); ImGui::Text("%.1f", h.totalQty);
                ImGui::TableNextColumn(); { char pb[32]; ImGui::Text("%s", FmtPrice(pb, sizeof(pb), h.avgEntry, h.symbol)); }
                ImGui::TableNextColumn();
                if (h.currentPrice > 0.f)
                    { char pb[32]; ImGui::TextColored(plCol, "%s", FmtPrice(pb, sizeof(pb), h.currentPrice, h.symbol)); }
                else ImGui::TextDisabled("--");
                ImGui::TableNextColumn(); { char pb[32]; ImGui::Text("%s", FmtPrice(pb, sizeof(pb), h.marketValue, h.symbol)); }
                ImGui::TableNextColumn();
                { char pb[48]; snprintf(pb, sizeof(pb), "%c%s%.2f", h.pnl >= 0 ? '+' : '-', currSym, std::fabs(h.pnl));
                  ImGui::TextColored(plCol, "%s", pb); }
                ImGui::TableNextColumn(); ImGui::TextColored(plCol, "%+.2f%%", h.pnlPct);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d active", h.activeStrats);
                if (h.aiStrats > 0) { ImGui::SameLine(); ImGui::TextColored(ui::kColorAI, "(%d AI)", h.aiStrats); }

                if (rowHovered)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(30, 40, 60, 160));

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);
    }

} // namespace stnks
