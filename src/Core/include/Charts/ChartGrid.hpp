#pragma once

#include <Charts/ChartLayer.hpp>
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace stnks
{
    // Reusable fancy grid renderer for any chart panel (main chart, volume, RSI, MACD, etc.)
    // Draws: gradient background, dot-grid pattern, major/minor grid lines with subtle glow.
    struct ChartGrid
    {
        // Color palette
        static constexpr ImU32 kBgTop        = IM_COL32(16, 18, 28, 255);
        static constexpr ImU32 kBgBottom      = IM_COL32(10, 12, 18, 255);
        static constexpr ImU32 kGridMajor     = IM_COL32(42, 50, 72, 200);
        static constexpr ImU32 kGridMinor     = IM_COL32(28, 32, 48, 120);
        static constexpr ImU32 kGridMajorGlow = IM_COL32(60, 80, 130, 40);
        static constexpr ImU32 kDotColor      = IM_COL32(40, 48, 70, 100);
        static constexpr ImU32 kBorderColor   = IM_COL32(45, 52, 75, 180);

        // Nice-step helper: returns a "round" number close to range/targetLines
        static float NiceStep(float range, float targetLines)
        {
            if (range <= 0.f || targetLines <= 0.f) return 1.f;
            float rawStep   = range / targetLines;
            float magnitude = std::pow(10.f, std::floor(std::log10(rawStep)));
            float normalized = rawStep / magnitude;
            float nice;
            if      (normalized < 1.5f) nice = 1.f;
            else if (normalized < 3.f)  nice = 2.f;
            else if (normalized < 7.f)  nice = 5.f;
            else                        nice = 10.f;
            return nice * magnitude;
        }

        // Draw gradient background fill
        static void DrawBackground(ImDrawList* dl, ImVec2 origin, ImVec2 size)
        {
            ImVec2 br(origin.x + size.x, origin.y + size.y);
            dl->AddRectFilledMultiColor(origin, br, kBgTop, kBgTop, kBgBottom, kBgBottom);
            // Subtle border
            dl->AddRect(origin, br, kBorderColor, 0.f, 0, 1.0f);
        }

        // Draw dot-grid pattern (small dots at grid intersections)
        static void DrawDotGrid(ImDrawList* dl, const ChartViewport& vp,
                                float hStep, float vStepPx,
                                int visibleStart, int visibleCount, int totalCandles,
                                float priceMin, float priceMax)
        {
            if (hStep <= 0.f || vStepPx <= 0.f) return;

            float pixelsPerUnit = vp.chartSize.y / std::max(0.001f, priceMax - priceMin);

            // Horizontal dot positions (price levels)
            float startP = std::ceil(priceMin / hStep) * hStep;
            for (float p = startP; p <= priceMax; p += hStep)
            {
                float y = vp.chartOrigin.y + vp.chartSize.y * (1.f - (p - priceMin) / (priceMax - priceMin));
                if (y < vp.chartOrigin.y || y > vp.chartOrigin.y + vp.chartSize.y) continue;

                // Place dots at vertical grid intersections
                int niceVStep = std::max(1, (int)(vStepPx / vp.CandleStep()));
                int end = std::min(visibleStart + visibleCount, totalCandles);
                int firstAligned = visibleStart - (visibleStart % std::max(1, niceVStep)) + niceVStep;
                for (int i = firstAligned; i < end; i += std::max(1, niceVStep))
                {
                    int ri = i - visibleStart;
                    float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                    dl->AddCircleFilled(ImVec2(x, y), 1.2f, kDotColor);
                }
            }
        }

        // Draw horizontal grid lines (price-based) with glow on major lines
        static void DrawHorizontalGrid(ImDrawList* dl, const ChartViewport& vp,
                                       float priceMin, float priceMax,
                                       float* outMajorStep = nullptr)
        {
            float range = priceMax - priceMin;
            if (range <= 0.f) return;

            float targetHLines = std::clamp(vp.chartSize.y / 80.f, 4.f, 10.f);
            float majorStep = NiceStep(range, targetHLines);

            constexpr float kMinPx = 20.f;
            float pxPerUnit = vp.chartSize.y / range;
            while (majorStep * pxPerUnit < kMinPx && majorStep < range)
                majorStep *= 2.f;

            if (outMajorStep) *outMajorStep = majorStep;

            float minorStep = majorStep / 4.f;
            if (minorStep * pxPerUnit < kMinPx)
                minorStep = majorStep / 2.f;
            bool showMinor = (minorStep * pxPerUnit >= kMinPx);

            float left  = vp.chartOrigin.x;
            float right = vp.chartOrigin.x + vp.chartSize.x;

            // Minor lines
            if (showMinor)
            {
                float start = std::ceil(priceMin / minorStep) * minorStep;
                for (float p = start; p <= priceMax; p += minorStep)
                {
                    float nearMajor = std::round(p / majorStep) * majorStep;
                    if (std::abs(p - nearMajor) < minorStep * 0.3f) continue;
                    float y = vp.chartOrigin.y + vp.chartSize.y * (1.f - (p - priceMin) / range);
                    if (y < vp.chartOrigin.y || y > vp.chartOrigin.y + vp.chartSize.y) continue;
                    dl->AddLine(ImVec2(left, y), ImVec2(right, y), kGridMinor, 1.0f);
                }
            }

            // Major lines with glow
            float start = std::ceil(priceMin / majorStep) * majorStep;
            for (float p = start; p <= priceMax; p += majorStep)
            {
                float y = vp.chartOrigin.y + vp.chartSize.y * (1.f - (p - priceMin) / range);
                if (y < vp.chartOrigin.y || y > vp.chartOrigin.y + vp.chartSize.y) continue;
                // Glow (wider, softer line behind the major line)
                dl->AddLine(ImVec2(left, y), ImVec2(right, y), kGridMajorGlow, 3.0f);
                dl->AddLine(ImVec2(left, y), ImVec2(right, y), kGridMajor, 1.0f);
            }
        }

        // Draw vertical grid lines (candle-index-based) with glow on major lines
        static void DrawVerticalGrid(ImDrawList* dl, const ChartViewport& vp,
                                     int visibleStart, int visibleCount, int totalCandles,
                                     float chartWidth,
                                     int* outNiceVStep = nullptr)
        {
            float targetV = std::clamp(chartWidth / 140.f, 3.f, 8.f);
            int vStep = std::max(1, (int)std::round((float)visibleCount / targetV));

            int niceVStep = 1;
            int candidates[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
            for (int c : candidates)
            {
                if (c >= vStep) { niceVStep = c; break; }
                niceVStep = c;
            }

            constexpr float kMinPx = 20.f;
            float pxPerCandle = vp.CandleStep();
            while (niceVStep * pxPerCandle < kMinPx && niceVStep < visibleCount)
                niceVStep *= 2;

            if (outNiceVStep) *outNiceVStep = niceVStep;

            int end = std::min(visibleStart + visibleCount, totalCandles);
            int firstAligned = visibleStart - (visibleStart % niceVStep) + niceVStep;

            float top    = vp.chartOrigin.y;
            float bottom = vp.chartOrigin.y + vp.chartSize.y;

            // Minor vertical lines
            int minorVStep = niceVStep / 2;
            if (minorVStep >= 1 && minorVStep * pxPerCandle >= kMinPx)
            {
                int firstMinor = visibleStart - (visibleStart % minorVStep) + minorVStep;
                for (int i = firstMinor; i < end; i += minorVStep)
                {
                    if (niceVStep > 0 && (i % niceVStep) == 0) continue;
                    int ri = i - visibleStart;
                    float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                    dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), kGridMinor, 1.0f);
                }
            }

            // Major vertical lines with glow
            for (int i = firstAligned; i < end; i += niceVStep)
            {
                int ri = i - visibleStart;
                float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), kGridMajorGlow, 3.0f);
                dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), kGridMajor, 1.0f);
            }
        }

        // All-in-one: draws background + dot grid + h/v grid lines for a panel
        // For indicator sub-panels that don't use price-based Y axis, pass custom
        // yMin/yMax (e.g., RSI: 0..100, MACD: -absMax..+absMax)
        static void DrawFull(ImDrawList* dl, const ChartViewport& vp,
                             int visibleStart, int visibleCount, int totalCandles,
                             float chartWidth,
                             float yMin, float yMax)
        {
            DrawBackground(dl, vp.chartOrigin, vp.chartSize);

            float majorStep = 0.f;
            DrawHorizontalGrid(dl, vp, yMin, yMax, &majorStep);

            int niceVStep = 1;
            DrawVerticalGrid(dl, vp, visibleStart, visibleCount, totalCandles, chartWidth, &niceVStep);

            // Dot grid at major intersections
            float vStepPx = niceVStep * vp.CandleStep();
            DrawDotGrid(dl, vp, majorStep, vStepPx, visibleStart, visibleCount, totalCandles, yMin, yMax);
        }
    };

} // namespace stnks
