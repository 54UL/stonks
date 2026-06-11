#pragma once

#include <Charts/ChartLayer.hpp>
#include <cmath>

namespace stnks
{
    class RSILayer : public ChartLayer
    {
    public:
        RSILayer()
        {
            name   = "RSI(14)";
            height = 80.f;
        }

        int   period       = 14;
        ImU32 lineColor    = IM_COL32(156, 136, 255, 255);
        ImU32 overBought   = IM_COL32(214, 48, 49, 60);
        ImU32 overSold     = IM_COL32(38, 166, 91, 60);
        ImU32 midLine      = IM_COL32(120, 120, 120, 80);
        ImU32 highlightCol = IM_COL32(255, 255, 255, 50);
        float overboughtLv = 70.f;
        float oversoldLv   = 30.f;

        bool GetValueRange(const ChartViewport& /*vp*/, const StockQuote& /*data*/,
                           float& outMin, float& outMax) const override
        {
            outMin = 0.f;
            outMax = 100.f;
            return true;
        }

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            if (data.candles.empty()) return;

            RSIData rsi = ComputeRSI(data.candles, period);
            if (rsi.values.empty()) return;

            int end = std::min(vp.visibleStart + vp.visibleCount, (int)data.candles.size());

            float panelBottom = vp.chartOrigin.y + vp.chartSize.y;

            auto rsiToY = [&](float val) -> float {
                float t = val / 100.f;
                return panelBottom - t * vp.chartSize.y;
            };

            // Overbought zone
            drawList->AddRectFilled(
                ImVec2(vp.chartOrigin.x, rsiToY(100.f)),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, rsiToY(overboughtLv)),
                overBought);

            // Oversold zone
            drawList->AddRectFilled(
                ImVec2(vp.chartOrigin.x, rsiToY(oversoldLv)),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, rsiToY(0.f)),
                overSold);

            // Horizontal guide lines
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x, rsiToY(overboughtLv)),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, rsiToY(overboughtLv)),
                midLine, 1.0f);
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x, rsiToY(oversoldLv)),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, rsiToY(oversoldLv)),
                midLine, 1.0f);
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x, rsiToY(50.f)),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, rsiToY(50.f)),
                midLine, 1.0f);

            // RSI line
            ImVec2 prev;
            bool hasPrev = false;

            for (int i = vp.visibleStart; i < end; ++i)
            {
                if (std::isnan(rsi.values[i])) { hasPrev = false; continue; }

                int ri = i - vp.visibleStart;
                float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                float y = rsiToY(rsi.values[i]);
                ImVec2 cur(x, y);

                if (hasPrev)
                    drawList->AddLine(prev, cur, lineColor, 1.5f);

                hasPrev = true;
                prev = cur;
            }

            // Focused candle: highlight column + dot + value label
            if (vp.focusedCandle >= 0 && vp.focusedCandle < (int)rsi.values.size()
                && !std::isnan(rsi.values[vp.focusedCandle]))
            {
                int ri = vp.focusedCandle - vp.visibleStart;
                float x = vp.IndexToX(ri);
                float xMid = x + vp.candleWidth * 0.5f;
                float rsiVal = rsi.values[vp.focusedCandle];
                float y = rsiToY(rsiVal);

                // Highlight column
                drawList->AddRectFilled(
                    ImVec2(x - 1.f, vp.chartOrigin.y),
                    ImVec2(x + vp.candleWidth + 1.f, vp.chartOrigin.y + vp.chartSize.y),
                    highlightCol);

                // Dot on RSI line
                drawList->AddCircleFilled(ImVec2(xMid, y), 4.f, lineColor);
                drawList->AddCircle(ImVec2(xMid, y), 4.f, IM_COL32(255, 255, 255, 200), 0, 1.5f);

                // Value label
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", rsiVal);
                drawList->AddText(ImVec2(xMid + 6.f, y - 7.f),
                                  IM_COL32(220, 220, 230, 255), buf);
            }

            // Labels
            drawList->AddText(ImVec2(vp.chartOrigin.x + 4, rsiToY(overboughtLv) - 14),
                              IM_COL32(180, 180, 180, 200), "70");
            drawList->AddText(ImVec2(vp.chartOrigin.x + 4, rsiToY(oversoldLv) + 2),
                              IM_COL32(180, 180, 180, 200), "30");
        }
    };

} // namespace stnks
