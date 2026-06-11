#pragma once

#include <Charts/ChartLayer.hpp>

namespace stnks
{
    class CandlestickLayer : public ChartLayer
    {
    public:
        CandlestickLayer() { name = "Candlestick"; }

        ImU32 bullColor    = IM_COL32(38, 166, 91, 255);   // Green
        ImU32 bearColor    = IM_COL32(214, 48, 49, 255);   // Red
        ImU32 wickColor    = IM_COL32(180, 180, 180, 255);
        ImU32 highlightCol = IM_COL32(255, 255, 255, 50);  // Focus highlight

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            if (data.candles.empty()) return;

            int end = std::min(vp.visibleStart + vp.visibleCount, (int)data.candles.size());

            for (int i = vp.visibleStart; i < end; ++i)
            {
                const auto& c = data.candles[i];
                int ri = i - vp.visibleStart;

                float x     = vp.IndexToX(ri);
                float xMid  = x + vp.candleWidth * 0.5f;
                float yOpen = vp.PriceToY(c.open);
                float yClose= vp.PriceToY(c.close);
                float yHigh = vp.PriceToY(c.high);
                float yLow  = vp.PriceToY(c.low);

                ImU32 col = c.IsBullish() ? bullColor : bearColor;

                // Wick (high-low line)
                drawList->AddLine(ImVec2(xMid, yHigh), ImVec2(xMid, yLow), wickColor, 1.0f);

                // Body
                float bodyTop    = std::min(yOpen, yClose);
                float bodyBottom = std::max(yOpen, yClose);
                if (bodyBottom - bodyTop < 1.f) bodyBottom = bodyTop + 1.f;

                drawList->AddRectFilled(
                    ImVec2(x, bodyTop),
                    ImVec2(x + vp.candleWidth, bodyBottom),
                    col);

                // Highlight focused candle
                if (i == vp.focusedCandle)
                {
                    drawList->AddRectFilled(
                        ImVec2(x - 1.f, vp.chartOrigin.y),
                        ImVec2(x + vp.candleWidth + 1.f, vp.chartOrigin.y + vp.chartSize.y),
                        highlightCol);
                    // Brighter border on focused body
                    drawList->AddRect(
                        ImVec2(x - 1.f, bodyTop - 1.f),
                        ImVec2(x + vp.candleWidth + 1.f, bodyBottom + 1.f),
                        IM_COL32(255, 255, 255, 180), 0.f, 0, 1.5f);
                }
            }
        }
    };

    // Sub-panel variant: renders as an indicator panel with its own price axis.
    // Use this to add extra candlestick views below the main chart (e.g., for
    // different strategy views, zoomed views, etc.)
    class CandlestickIndicator : public CandlestickLayer
    {
    public:
        CandlestickIndicator()
        {
            name    = "Candles";
            height  = 120.f;
            visible = false;  // Hidden by default, user enables via indicator combo
        }

        bool GetValueRange(const ChartViewport& vp, const StockQuote& data,
                           float& outMin, float& outMax) const override
        {
            if (data.candles.empty()) return false;
            int end = std::min(vp.visibleStart + vp.visibleCount, (int)data.candles.size());
            float lo = 1e18f, hi = -1e18f;
            for (int i = vp.visibleStart; i < end; ++i)
            {
                lo = std::min(lo, data.candles[i].low);
                hi = std::max(hi, data.candles[i].high);
            }
            if (lo >= hi) return false;
            float margin = (hi - lo) * 0.05f;
            outMin = lo - margin;
            outMax = hi + margin;
            return true;
        }
    };

} // namespace stnks
