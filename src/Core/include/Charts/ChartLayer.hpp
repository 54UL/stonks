#pragma once

#include <Market/MarketData.hpp>
#include <imgui.h>
#include <string>

namespace stnks
{
    // Viewport state shared across all layers of a chart
    struct ChartViewport
    {
        int   visibleStart  = 0;      // Index of first visible candle
        int   visibleCount  = 60;     // Number of visible candles
        float priceMin      = 0.f;    // Auto-scaled price range
        float priceMax      = 0.f;
        float candleWidth   = 8.f;    // Pixel width per candle
        float candleSpacing = 2.f;    // Gap between candles
        ImVec2 chartOrigin;           // Top-left of chart area in screen coords
        ImVec2 chartSize;             // Size of chart area

        // Synchronized focus: candle index under cursor (-1 = none)
        int   focusedCandle = -1;

        float CandleStep() const { return candleWidth + candleSpacing; }

        // Map price to Y pixel coordinate (price increases upward)
        float PriceToY(float price) const
        {
            if (priceMax <= priceMin) return chartOrigin.y;
            float t = (price - priceMin) / (priceMax - priceMin);
            return chartOrigin.y + chartSize.y * (1.f - t);
        }

        // Map Y pixel coordinate back to price
        float YToPrice(float y) const
        {
            if (chartSize.y <= 0.f) return priceMin;
            float t = 1.f - (y - chartOrigin.y) / chartSize.y;
            return priceMin + t * (priceMax - priceMin);
        }

        // Map candle index (relative to visibleStart) to X pixel coordinate
        float IndexToX(int relIndex) const
        {
            return chartOrigin.x + (float)relIndex * CandleStep() + candleSpacing;
        }

        // Get the X center of a focused candle (absolute index), or -1 if not focused
        float FocusedX() const
        {
            if (focusedCandle < 0) return -1.f;
            int ri = focusedCandle - visibleStart;
            return IndexToX(ri) + candleWidth * 0.5f;
        }
    };

    // Abstract base for chart overlays
    class ChartLayer
    {
    public:
        virtual ~ChartLayer() = default;

        std::string name;
        bool        visible = true;
        float       height  = 0.f;  // 0 = shares main chart area, >0 = separate panel height

        // Draw this layer. drawList is the ImGui draw list for the chart window.
        virtual void Draw(ImDrawList* drawList,
                          const ChartViewport& vp,
                          const StockQuote& data) = 0;
    };

} // namespace stnks
