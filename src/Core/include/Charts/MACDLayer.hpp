#pragma once

#include <Charts/ChartLayer.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace stnks
{
    class MACDLayer : public ChartLayer
    {
    public:
        MACDLayer()
        {
            name    = "MACD(12,26,9)";
            height  = 90.f;
            visible = true; // Visible by default, stacked with Volume and RSI
        }

        int   fastPeriod   = 12;
        int   slowPeriod   = 26;
        int   signalPeriod = 9;

        ImU32 macdColor      = IM_COL32(66, 165, 245, 255);   // Blue
        ImU32 signalColor    = IM_COL32(255, 167, 38, 255);   // Orange
        ImU32 histBullColor  = IM_COL32(38, 166, 91, 180);    // Green bars
        ImU32 histBearColor  = IM_COL32(214, 48, 49, 180);    // Red bars
        ImU32 zeroLineColor  = IM_COL32(120, 120, 120, 80);
        ImU32 highlightCol   = IM_COL32(255, 255, 255, 50);

        bool GetValueRange(const ChartViewport& vp, const StockQuote& data,
                           float& outMin, float& outMax) const override
        {
            if (data.candles.empty()) return false;
            MACDData macd = ComputeMACD(data.candles, fastPeriod, slowPeriod, signalPeriod);
            if (macd.macd.empty()) return false;
            int end = std::min(vp.visibleStart + vp.visibleCount, (int)data.candles.size());
            float vMin = 1e18f, vMax = -1e18f;
            for (int i = vp.visibleStart; i < end; ++i)
            {
                if (!std::isnan(macd.macd[i]))      { vMin = std::min(vMin, macd.macd[i]); vMax = std::max(vMax, macd.macd[i]); }
                if (!std::isnan(macd.signal[i]))     { vMin = std::min(vMin, macd.signal[i]); vMax = std::max(vMax, macd.signal[i]); }
                if (!std::isnan(macd.histogram[i]))  { vMin = std::min(vMin, macd.histogram[i]); vMax = std::max(vMax, macd.histogram[i]); }
            }
            if (vMin >= vMax) return false;
            float absMax = std::max(std::abs(vMin), std::abs(vMax)) * 1.1f;
            outMin = -absMax;
            outMax = absMax;
            return true;
        }

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            if (data.candles.empty()) return;

            MACDData macd = ComputeMACD(data.candles, fastPeriod, slowPeriod, signalPeriod);
            if (macd.macd.empty()) return;

            int end = std::min(vp.visibleStart + vp.visibleCount, (int)data.candles.size());

            // Find value range for visible area
            float vMin = 1e18f, vMax = -1e18f;
            for (int i = vp.visibleStart; i < end; ++i)
            {
                if (!std::isnan(macd.macd[i]))
                {
                    vMin = std::min(vMin, macd.macd[i]);
                    vMax = std::max(vMax, macd.macd[i]);
                }
                if (!std::isnan(macd.signal[i]))
                {
                    vMin = std::min(vMin, macd.signal[i]);
                    vMax = std::max(vMax, macd.signal[i]);
                }
                if (!std::isnan(macd.histogram[i]))
                {
                    vMin = std::min(vMin, macd.histogram[i]);
                    vMax = std::max(vMax, macd.histogram[i]);
                }
            }

            if (vMin >= vMax) return;

            // Symmetrize around zero for balanced display
            float absMax = std::max(std::abs(vMin), std::abs(vMax));
            absMax *= 1.1f; // 10% margin
            vMin = -absMax;
            vMax = absMax;

            float panelTop    = vp.chartOrigin.y;
            float panelBottom = vp.chartOrigin.y + vp.chartSize.y;

            auto valToY = [&](float val) -> float {
                float t = (val - vMin) / (vMax - vMin);
                return panelBottom - t * vp.chartSize.y;
            };

            float zeroY = valToY(0.f);

            // Zero line
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x, zeroY),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, zeroY),
                zeroLineColor, 1.0f);

            // Histogram bars
            for (int i = vp.visibleStart; i < end; ++i)
            {
                if (std::isnan(macd.histogram[i])) continue;

                int ri = i - vp.visibleStart;
                float x = vp.IndexToX(ri);
                float barY = valToY(macd.histogram[i]);

                bool positive = macd.histogram[i] >= 0.f;
                ImU32 col = positive ? histBullColor : histBearColor;

                float top    = std::min(zeroY, barY);
                float bottom = std::max(zeroY, barY);
                if (bottom - top < 1.f) bottom = top + 1.f;

                drawList->AddRectFilled(
                    ImVec2(x, top),
                    ImVec2(x + vp.candleWidth, bottom),
                    col);
            }

            // MACD line
            {
                ImVec2 prev;
                bool hasPrev = false;
                for (int i = vp.visibleStart; i < end; ++i)
                {
                    if (std::isnan(macd.macd[i])) { hasPrev = false; continue; }
                    int ri = i - vp.visibleStart;
                    float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                    ImVec2 cur(x, valToY(macd.macd[i]));
                    if (hasPrev)
                        drawList->AddLine(prev, cur, macdColor, 1.5f);
                    hasPrev = true;
                    prev = cur;
                }
            }

            // Signal line
            {
                ImVec2 prev;
                bool hasPrev = false;
                for (int i = vp.visibleStart; i < end; ++i)
                {
                    if (std::isnan(macd.signal[i])) { hasPrev = false; continue; }
                    int ri = i - vp.visibleStart;
                    float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                    ImVec2 cur(x, valToY(macd.signal[i]));
                    if (hasPrev)
                        drawList->AddLine(prev, cur, signalColor, 1.5f);
                    hasPrev = true;
                    prev = cur;
                }
            }

            // Focused candle highlight + values
            if (vp.focusedCandle >= 0 && vp.focusedCandle < (int)macd.macd.size())
            {
                int ri = vp.focusedCandle - vp.visibleStart;
                float x = vp.IndexToX(ri);

                // Highlight column
                drawList->AddRectFilled(
                    ImVec2(x - 1.f, panelTop),
                    ImVec2(x + vp.candleWidth + 1.f, panelBottom),
                    highlightCol);

                // Value labels in top-left
                float labelX = vp.chartOrigin.x + 4.f;
                float labelY = panelTop + 14.f; // Below the panel name

                if (!std::isnan(macd.macd[vp.focusedCandle]))
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "MACD %.4f", macd.macd[vp.focusedCandle]);
                    drawList->AddText(ImVec2(labelX, labelY), macdColor, buf);
                    labelX += ImGui::CalcTextSize(buf).x + 12.f;
                }
                if (!std::isnan(macd.signal[vp.focusedCandle]))
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Signal %.4f", macd.signal[vp.focusedCandle]);
                    drawList->AddText(ImVec2(labelX, labelY), signalColor, buf);
                    labelX += ImGui::CalcTextSize(buf).x + 12.f;
                }
                if (!std::isnan(macd.histogram[vp.focusedCandle]))
                {
                    char buf[32];
                    float h = macd.histogram[vp.focusedCandle];
                    snprintf(buf, sizeof(buf), "Hist %+.4f", h);
                    drawList->AddText(ImVec2(labelX, labelY),
                                      h >= 0 ? histBullColor : histBearColor, buf);
                }

                // Dots on MACD and signal lines
                float xMid = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                if (!std::isnan(macd.macd[vp.focusedCandle]))
                {
                    float y = valToY(macd.macd[vp.focusedCandle]);
                    drawList->AddCircleFilled(ImVec2(xMid, y), 3.5f, macdColor);
                    drawList->AddCircle(ImVec2(xMid, y), 3.5f, IM_COL32(255, 255, 255, 200), 0, 1.f);
                }
                if (!std::isnan(macd.signal[vp.focusedCandle]))
                {
                    float y = valToY(macd.signal[vp.focusedCandle]);
                    drawList->AddCircleFilled(ImVec2(xMid, y), 3.5f, signalColor);
                    drawList->AddCircle(ImVec2(xMid, y), 3.5f, IM_COL32(255, 255, 255, 200), 0, 1.f);
                }
            }
        }
    };

} // namespace stnks
