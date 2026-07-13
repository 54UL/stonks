#pragma once

#include <Charts/ChartLayer.hpp>
#include <Strategy/Strategy.hpp>
#include <Market/MarketHours.hpp>
#include <imgui.h>
#include <algorithm>
#include <vector>

namespace stnks
{
    // Result of gizmo interaction for a single frame
    struct StrategyInteraction
    {
        bool modified  = false;  // Prices changed via drag
        bool confirmed = false;  // User clicked confirm
        bool cancelled = false;  // User clicked cancel (dismiss edit)
        bool deleted   = false;  // User clicked delete (X button)
        bool selected  = false;  // User clicked to select/edit this strategy
    };

    // Base interface for strategy chart visualization.
    // Each strategy type (TP/SL, trailing stop, etc.) implements this
    // to provide its own rendering and interactive gizmos.
    class IStrategyRenderer
    {
    public:
        virtual ~IStrategyRenderer() = default;

        // Human-readable name for context menus
        virtual const char* TypeName() const = 0;

        // Draw the strategy visualization (zones, lines, labels, status badges).
        // Called for every visible strategy each frame.
        virtual void Draw(ImDrawList* drawList, const ChartViewport& vp,
                          const Strategy& strategy, bool editing,
                          const std::vector<Candle>* candles = nullptr) = 0;

        // Draw interactive gizmos and handle drag input.
        // Called only for the strategy currently being edited.
        // May modify strategy prices in-place during drag.
        virtual StrategyInteraction HandleGizmos(ImDrawList* drawList, const ChartViewport& vp,
                                                 Strategy& strategy) = 0;

        // Draw a compact mini-bar for non-editing strategies.
        // Shows a small tag at the entry line with an X button to delete.
        // Returns interaction result (selected = click to edit, deleted = X clicked).
        StrategyInteraction DrawMiniBar(ImDrawList* drawList, const ChartViewport& vp,
                                        const Strategy& strategy, float yOffset = 0.f)
        {
            StrategyInteraction result;
            if (!strategy.IsActive()) return result;

            // Only interact when this chart's child window is hovered
            bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            float yEntry = vp.PriceToY(strategy.entryPrice);

            // ── Layout ──
            float barH   = 22.f;
            float barY   = yEntry - barH * 0.5f + yOffset;
            barY = std::clamp(barY, vp.chartOrigin.y + 2.f, vp.chartOrigin.y + vp.chartSize.y - barH - 2.f);

            bool isLong = strategy.direction == StrategyDirection::Long;

            // ── Build label parts ──
            const char* dirArrow = isLong ? "\xe2\x96\xb2" : "\xe2\x96\xbc"; // Unicode triangles
            char priceBuf[32];
            FmtPrice(priceBuf, sizeof(priceBuf), strategy.entryPrice, strategy.symbol);

            const char* typeTag = strategy.IsTPSL() ? "TP/SL" : (strategy.IsPosition() ? "POS" : "AI");

            char label[80];
            snprintf(label, sizeof(label), " %s %s  %s ", dirArrow, typeTag, priceBuf);

            ImVec2 textSz = ImGui::CalcTextSize(label);
            float closeBtnW = 20.f;
            float pad = 6.f;
            float barW = textSz.x + closeBtnW + pad * 2.f;
            float barX = vp.chartOrigin.x + 4.f;

            ImVec2 barMin(barX, barY);
            ImVec2 barMax(barX + barW, barY + barH);

            // ── Colors ──
            ImU32 dirColor     = isLong ? IM_COL32(38, 180, 100, 255) : IM_COL32(230, 75, 75, 255);
            ImU32 dirColorDim  = isLong ? IM_COL32(38, 180, 100, 140) : IM_COL32(230, 75, 75, 140);

            bool barHovered = windowHovered && ImGui::IsMouseHoveringRect(barMin, barMax);

            // Background: subtle gradient feel with left accent stripe
            ImU32 barBg = barHovered ? IM_COL32(30, 34, 48, 245) : IM_COL32(18, 20, 28, 230);
            ImU32 barBord = barHovered ? dirColor : dirColorDim;

            // ── Draw bar ──
            drawList->AddRectFilled(barMin, barMax, barBg, 4.f);

            // Left accent stripe (direction color)
            float stripeW = 3.f;
            drawList->AddRectFilled(
                ImVec2(barX, barY + 1.f),
                ImVec2(barX + stripeW, barY + barH - 1.f),
                dirColor, 2.f);

            // Border (only on hover — cleaner look)
            if (barHovered)
                drawList->AddRect(barMin, barMax, barBord, 4.f, 0, 1.2f);

            // ── Label text ──
            float textY = barY + (barH - textSz.y) * 0.5f;
            ImU32 textCol = barHovered ? IM_COL32(240, 240, 250, 255) : IM_COL32(180, 185, 200, 220);

            // Direction arrow gets its own color
            ImVec2 arrowSz = ImGui::CalcTextSize(dirArrow);
            float arrowX = barX + pad + stripeW;
            drawList->AddText(ImVec2(arrowX, textY), dirColor, dirArrow);

            // Rest of label after arrow
            char restLabel[64];
            snprintf(restLabel, sizeof(restLabel), " %s  %s ", typeTag, priceBuf);
            drawList->AddText(ImVec2(arrowX + arrowSz.x, textY), textCol, restLabel);

            // ── Close button (circle with X) ──
            float closeCx = barX + barW - closeBtnW * 0.5f - pad * 0.5f;
            float closeCy = barY + barH * 0.5f;
            float closeR  = 7.f;

            bool closeHovered = windowHovered && ImGui::IsMouseHoveringRect(
                ImVec2(closeCx - closeR, closeCy - closeR),
                ImVec2(closeCx + closeR, closeCy + closeR));

            if (closeHovered || barHovered)
            {
                ImU32 closeBg = closeHovered ? IM_COL32(200, 50, 50, 200) : IM_COL32(80, 40, 40, 120);
                drawList->AddCircleFilled(ImVec2(closeCx, closeCy), closeR, closeBg);
            }
            // X cross
            float cr = 3.f;
            ImU32 xCol = closeHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 170, barHovered ? 200 : 100);
            drawList->AddLine(ImVec2(closeCx - cr, closeCy - cr), ImVec2(closeCx + cr, closeCy + cr), xCol, 1.5f);
            drawList->AddLine(ImVec2(closeCx + cr, closeCy - cr), ImVec2(closeCx - cr, closeCy + cr), xCol, 1.5f);

            // ── Dashed connection line from bar to entry price level ──
            float lineStartX = barX + barW + 2.f;
            float lineEndX   = vp.chartOrigin.x + vp.chartSize.x;
            float lineY      = yEntry;
            ImU32 lineCol = barHovered ? dirColor : IM_COL32(
                (dirColor >> 0) & 0xFF, (dirColor >> 8) & 0xFF, (dirColor >> 16) & 0xFF, 50);
            float dashX = lineStartX;
            while (dashX < lineEndX)
            {
                float endX = std::min(dashX + 5.f, lineEndX);
                drawList->AddLine(ImVec2(dashX, lineY), ImVec2(endX, lineY), lineCol, 0.8f);
                dashX += 9.f;
            }

            // ── Handle clicks ──
            if (closeHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                result.deleted = true;
            else if (barHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                result.selected = true;

            if (barHovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            // ── Tooltip ──
            if (barHovered && !closeHovered)
            {
                ImGui::BeginTooltip();
                ImGui::TextColored(isLong ? ImVec4(0.15f,0.7f,0.4f,1.f) : ImVec4(0.9f,0.3f,0.3f,1.f),
                    "%s %s", DirectionToString(strategy.direction), StrategyTypeToString(strategy.type));
                ImGui::SameLine();
                ImGui::TextDisabled("#%lld", (long long)strategy.id);
                { char pb[32]; FmtPrice(pb, sizeof(pb), strategy.entryPrice, strategy.symbol);
                ImGui::Text("Entry: %s", pb); }
                if (strategy.takeProfit > 0.f) { char pb[32]; ImGui::Text("TP: %s (%+.1f%%)", FmtPrice(pb, sizeof(pb), strategy.takeProfit, strategy.symbol), strategy.TPPercent()); }
                if (strategy.stopLoss > 0.f)   { char pb[32]; ImGui::Text("SL: %s (%+.1f%%)", FmtPrice(pb, sizeof(pb), strategy.stopLoss, strategy.symbol), strategy.SLPercent()); }
                if (strategy.quantity > 0.f)    ImGui::Text("Qty: %.0f", strategy.quantity);
                if (strategy.takeProfit > 0.f && strategy.stopLoss > 0.f)
                    ImGui::TextColored(ImVec4(0.7f,0.7f,0.8f,1.f), "R:R %.1f", strategy.RiskReward());
                if (!strategy.notes.empty())    ImGui::TextDisabled("%s", strategy.notes.c_str());
                ImGui::Separator();
                ImGui::TextDisabled("Click to edit  |  X to close");
                ImGui::EndTooltip();
            }

            return result;
        }

    public:
        // Find the X pixel for a strategy's entry time.
        // Returns chart left edge if timestamp is before visible range.
        // Returns last candle X if timestamp is at/after the latest candle.
        static float EntryXFromTimestamp(const ChartViewport& vp,
                                          const std::vector<Candle>* candles,
                                          int64_t timestamp)
        {
            if (!candles || candles->empty() || timestamp <= 0)
                return vp.chartOrigin.x;

            // Find candle closest to timestamp (binary search: last candle <= timestamp)
            int idx = 0;
            int lo = 0, hi = (int)candles->size() - 1;
            while (lo <= hi)
            {
                int mid = (lo + hi) / 2;
                if ((*candles)[mid].timestamp <= timestamp)
                {
                    idx = mid;
                    lo = mid + 1;
                }
                else
                    hi = mid - 1;
            }

            // Convert absolute index to relative and then to X
            int rel = idx - vp.visibleStart;
            if (rel < 0) return vp.chartOrigin.x; // Before visible area, clip to left
            float x = vp.IndexToX(rel);
            // Clamp to chart right edge
            float maxX = vp.chartOrigin.x + vp.chartSize.x;
            return (x > maxX) ? maxX : x;
        }

        // Resolve entry X position for a strategy.
        // Prefers entryDate timestamp; falls back to FindCandleByPrice if no date set.
        static float ResolveEntryX(const ChartViewport& vp,
                                    const std::vector<Candle>* candles,
                                    const Strategy& strategy)
        {
            // If we have an explicit entry date, use timestamp lookup
            if (strategy.entryDate > 0)
                return EntryXFromTimestamp(vp, candles, strategy.entryDate);

            // Fall back: find candle closest to entry price
            if (candles && !candles->empty() && strategy.entryPrice > 0.f)
            {
                int idx = FindCandleByPrice(*candles, strategy.entryPrice);
                if (idx >= 0)
                {
                    int rel = idx - vp.visibleStart;
                    if (rel < 0) return vp.chartOrigin.x;
                    float x = vp.IndexToX(rel);
                    float maxX = vp.chartOrigin.x + vp.chartSize.x;
                    return (x > maxX) ? maxX : x;
                }
            }

            // Last resort: use createdAt timestamp
            return EntryXFromTimestamp(vp, candles, strategy.createdAt);
        }

        // Resolve the right edge X for a strategy.
        // Active strategies extend to chart right edge.
        // Closed strategies (TP hit, SL hit, cancelled) clip at triggeredAt timestamp.
        static float ResolveRightX(const ChartViewport& vp,
                                    const std::vector<Candle>* candles,
                                    const Strategy& strategy)
        {
            float chartRight = vp.chartOrigin.x + vp.chartSize.x;
            if (strategy.IsActive() || strategy.triggeredAt <= 0)
                return chartRight;

            float exitX = EntryXFromTimestamp(vp, candles, strategy.triggeredAt);
            // Ensure at least a small visible width from entry
            float leftX = ResolveEntryX(vp, candles, strategy);
            float minRight = leftX + 20.f;
            return std::max(std::min(exitX, chartRight), minRight);
        }

        // Find the candle index whose price matches closest to a given price.
        // Useful for finding where to anchor a position on the chart by price.
        static int FindCandleByPrice(const std::vector<Candle>& candles,
                                      float price, int startIdx = 0)
        {
            if (candles.empty()) return -1;

            int bestIdx = startIdx;
            float bestDist = std::abs(candles[startIdx].close - price);

            for (int i = startIdx; i < (int)candles.size(); ++i)
            {
                float dist = std::abs(candles[i].close - price);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx = i;
                }
            }
            return bestIdx;
        }
    };

} // namespace stnks
