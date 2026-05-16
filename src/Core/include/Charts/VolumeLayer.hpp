#pragma once

#include <Charts/ChartLayer.hpp>

namespace stnks
{
    class VolumeLayer : public ChartLayer
    {
    public:
        VolumeLayer()
        {
            name   = "Volume";
            height = 60.f;
        }

        ImU32 bullColor    = IM_COL32(38, 166, 91, 120);
        ImU32 bearColor    = IM_COL32(214, 48, 49, 120);
        ImU32 highlightCol = IM_COL32(255, 255, 255, 50);

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            if (data.candles.empty()) return;

            int end = std::min(vp.visibleStart + vp.visibleCount, (int)data.candles.size());
            float maxVol = 0.f;
            for (int i = vp.visibleStart; i < end; ++i)
                maxVol = std::max(maxVol, data.candles[i].volume);

            if (maxVol <= 0.f) return;

            for (int i = vp.visibleStart; i < end; ++i)
            {
                const auto& c = data.candles[i];
                int ri = i - vp.visibleStart;

                float x = vp.IndexToX(ri);
                float barHeight = (c.volume / maxVol) * height;
                float yBottom = vp.chartOrigin.y + vp.chartSize.y;
                float yTop    = yBottom - barHeight;

                bool focused = (i == vp.focusedCandle);

                // Focused bar drawn brighter
                ImU32 col = c.IsBullish() ? bullColor : bearColor;
                if (focused)
                {
                    // Make alpha full for focused bar
                    col = c.IsBullish() ? IM_COL32(38, 166, 91, 220) : IM_COL32(214, 48, 49, 220);
                }

                drawList->AddRectFilled(
                    ImVec2(x, yTop),
                    ImVec2(x + vp.candleWidth, yBottom),
                    col);

                if (focused)
                {
                    drawList->AddRectFilled(
                        ImVec2(x - 1.f, vp.chartOrigin.y),
                        ImVec2(x + vp.candleWidth + 1.f, vp.chartOrigin.y + vp.chartSize.y),
                        highlightCol);
                    drawList->AddRect(
                        ImVec2(x - 1.f, yTop - 1.f),
                        ImVec2(x + vp.candleWidth + 1.f, yBottom + 1.f),
                        IM_COL32(255, 255, 255, 180), 0.f, 0, 1.5f);

                    // Value label
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.0f", c.volume);
                    drawList->AddText(ImVec2(x, yTop - 14.f),
                                      IM_COL32(220, 220, 230, 255), buf);
                }
            }
        }
    };

} // namespace stnks
