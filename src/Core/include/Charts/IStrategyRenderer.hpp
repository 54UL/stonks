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

            float yEntry = vp.PriceToY(strategy.entryPrice);
            float right  = vp.chartOrigin.x + vp.chartSize.x;

            // Mini-bar dimensions
            float barH   = 18.f;
            float barW   = 0.f;
            float barY   = yEntry - barH * 0.5f + yOffset;

            // Clamp to chart area
            barY = std::max(barY, vp.chartOrigin.y + 2.f);
            barY = std::min(barY, vp.chartOrigin.y + vp.chartSize.y - barH - 2.f);

            // Build label
            char label[64];
            const char* typeTag = strategy.IsTPSL() ? "TP/SL" : (strategy.IsPosition() ? "POS" : "AI");
            const char* dirTag  = strategy.direction == StrategyDirection::Long ? "L" : "S";
            { char pb[32]; FmtPrice(pb, sizeof(pb), strategy.entryPrice, strategy.symbol);
            snprintf(label, sizeof(label), "%s %s %s", typeTag, dirTag, pb); }

            ImVec2 textSz = ImGui::CalcTextSize(label);
            float xBtnW = 16.f; // X button width
            float pad    = 4.f;
            barW = textSz.x + xBtnW + pad * 3.f;

            float barX = vp.chartOrigin.x + 4.f;

            ImVec2 barMin(barX, barY);
            ImVec2 barMax(barX + barW, barY + barH);

            // Colors based on direction
            bool isLong = strategy.direction == StrategyDirection::Long;
            ImU32 barBg    = IM_COL32(22, 24, 32, 220);
            ImU32 barBord  = isLong ? IM_COL32(38, 166, 91, 140) : IM_COL32(214, 48, 49, 140);
            ImU32 textCol  = isLong ? IM_COL32(80, 200, 120, 255) : IM_COL32(230, 90, 90, 255);

            bool barHovered = ImGui::IsMouseHoveringRect(barMin, barMax);
            if (barHovered)
            {
                barBg   = IM_COL32(32, 36, 48, 240);
                barBord = isLong ? IM_COL32(50, 200, 110, 220) : IM_COL32(240, 70, 70, 220);
            }

            // Draw bar
            drawList->AddRectFilled(barMin, barMax, barBg, 3.f);
            drawList->AddRect(barMin, barMax, barBord, 3.f, 0, 1.f);

            // Label text
            float textY = barY + (barH - textSz.y) * 0.5f;
            drawList->AddText(ImVec2(barX + pad, textY), textCol, label);

            // X button (delete/cancel)
            float xBtnX = barX + barW - xBtnW - pad * 0.5f;
            ImVec2 xMin(xBtnX, barY + 2.f);
            ImVec2 xMax(xBtnX + xBtnW, barY + barH - 2.f);
            bool xHovered = ImGui::IsMouseHoveringRect(xMin, xMax);

            ImU32 xBg  = xHovered ? IM_COL32(180, 40, 40, 200) : IM_COL32(80, 30, 30, 160);
            ImU32 xCol = xHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 180);
            drawList->AddRectFilled(xMin, xMax, xBg, 2.f);

            // Draw X cross
            float cx = xBtnX + xBtnW * 0.5f;
            float cy = barY + barH * 0.5f;
            float cr = 3.5f;
            drawList->AddLine(ImVec2(cx - cr, cy - cr), ImVec2(cx + cr, cy + cr), xCol, 1.5f);
            drawList->AddLine(ImVec2(cx + cr, cy - cr), ImVec2(cx - cr, cy + cr), xCol, 1.5f);

            // Handle clicks
            if (xHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                result.deleted = true;
            else if (barHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                result.selected = true;

            if (barHovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            // Tooltip
            if (barHovered && !xHovered)
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s #%lld", StrategyTypeToString(strategy.type), (long long)strategy.id);
                { char pb[32]; FmtPrice(pb, sizeof(pb), strategy.entryPrice, strategy.symbol);
                ImGui::Text("%s @ %s", DirectionToString(strategy.direction), pb); }
                if (strategy.takeProfit > 0.f) { char pb[32]; ImGui::Text("TP: %s", FmtPrice(pb, sizeof(pb), strategy.takeProfit, strategy.symbol)); }
                if (strategy.stopLoss > 0.f)   { char pb[32]; ImGui::Text("SL: %s", FmtPrice(pb, sizeof(pb), strategy.stopLoss, strategy.symbol)); }
                if (strategy.quantity > 0.f)    ImGui::Text("Qty: %.0f", strategy.quantity);
                if (!strategy.notes.empty())    ImGui::TextDisabled("%s", strategy.notes.c_str());
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
