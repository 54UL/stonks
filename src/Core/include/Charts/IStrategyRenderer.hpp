#pragma once

#include <Charts/ChartLayer.hpp>
#include <Strategy/Strategy.hpp>
#include <imgui.h>
#include <vector>

namespace stnks
{
    // Result of gizmo interaction for a single frame
    struct StrategyInteraction
    {
        bool modified  = false;  // Prices changed via drag
        bool confirmed = false;  // User clicked confirm
        bool cancelled = false;  // User clicked cancel / delete
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
