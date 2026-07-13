#pragma once

#include <Charts/ChartLayer.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace stnks
{
    // Volume Profile shape patterns (market auction theory)
    enum class VPShape : int
    {
        None    = 0,
        PShape  = 1,  // Accumulation: heavy volume at bottom, thin top (bullish)
        DShape  = 2,  // Distribution: heavy volume at top, thin bottom (bearish)
        BShape  = 3,  // Balanced: volume concentrated in middle (range-bound)
    };

    inline const char* VPShapeName(VPShape s)
    {
        switch (s)
        {
        case VPShape::PShape: return "P-Shape";
        case VPShape::DShape: return "D-Shape";
        case VPShape::BShape: return "B-Shape";
        default:              return "None";
        }
    }

    struct VPPattern
    {
        VPShape shape      = VPShape::None;
        float   pocPrice   = 0.f;
        float   vahPrice   = 0.f;
        float   valPrice   = 0.f;
        float   confidence = 0.f;
    };

    class VolumeProfileLayer : public ChartLayer
    {
    public:
        VolumeProfileLayer()
        {
            name    = "Volume Profile";
            height  = 0.f;
            visible = true;
        }

        int bucketCount = 40;
        int pinnedLookback = 20;
        float fullBarWidthPct  = 0.20f;
        float pinnedBarWidthPct = 0.25f;

        ImU32 fullBullColor    = IM_COL32(38, 166, 91, 45);
        ImU32 fullBearColor    = IM_COL32(214, 48, 49, 45);
        ImU32 fullPocColor     = IM_COL32(255, 215, 0, 60);

        ImU32 pinnedBullColor  = IM_COL32(38, 166, 91, 100);
        ImU32 pinnedBearColor  = IM_COL32(214, 48, 49, 100);
        ImU32 pinnedPocColor   = IM_COL32(255, 215, 0, 110);
        ImU32 anchorLineColor  = IM_COL32(100, 140, 200, 100);

        ImU32 pShapeFill       = IM_COL32(38, 166, 91, 25);
        ImU32 pShapeBorder     = IM_COL32(38, 166, 91, 120);
        ImU32 dShapeFill       = IM_COL32(214, 48, 49, 25);
        ImU32 dShapeBorder     = IM_COL32(214, 48, 49, 120);
        ImU32 bShapeFill       = IM_COL32(255, 200, 50, 20);
        ImU32 bShapeBorder     = IM_COL32(255, 200, 50, 100);
        ImU32 pocLineColor     = IM_COL32(255, 215, 0, 160);

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            if (data.candles.empty()) return;

            ImGuiIO& io = ImGui::GetIO();
            bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            bool inChartArea = hovered &&
                io.MousePos.x >= vp.chartOrigin.x &&
                io.MousePos.x <= vp.chartOrigin.x + vp.chartSize.x &&
                io.MousePos.y >= vp.chartOrigin.y &&
                io.MousePos.y <= vp.chartOrigin.y + vp.chartSize.y;

            if (inChartArea && vp.focusedCandle >= 0)
            {
                int anchor = vp.focusedCandle;
                int lo = std::max(0, anchor - pinnedLookback / 2);
                int hi = std::min((int)data.candles.size(), anchor + pinnedLookback / 2 + 1);

                auto buckets = BuildBuckets(vp, data, lo, hi);
                if (!buckets.empty())
                {
                    int relIdx = anchor - vp.visibleStart;
                    float anchorX = vp.IndexToX(relIdx) + vp.candleWidth * 0.5f;
                    float maxBarW = vp.chartSize.x * fullBarWidthPct;

                    drawList->AddLine(
                        ImVec2(anchorX, vp.chartOrigin.y),
                        ImVec2(anchorX, vp.chartOrigin.y + vp.chartSize.y),
                        anchorLineColor, 1.f);

                    DrawBars(drawList, vp, buckets, anchorX, maxBarW,
                             fullBullColor, fullBearColor, fullPocColor);

                    VPPattern pat = DetectPattern(buckets);
                    if (pat.shape != VPShape::None)
                        DrawPatternZone(drawList, vp, pat, anchorX, maxBarW, false);
                }
            }

            if (inChartArea && vp.focusedCandle >= 0)
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyShift && !io.KeyAlt)
                {
                    if (io.KeyCtrl)
                    {
                        pinnedCandles_.clear();
                    }
                    else
                    {
                        pinnedCandles_.push_back(vp.focusedCandle);
                    }
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    int removeIdx = FindHoveredPin(vp, data, io.MousePos);
                    if (removeIdx >= 0)
                        pinnedCandles_.erase(pinnedCandles_.begin() + removeIdx);
                }
            }

            pinnedCandles_.erase(
                std::remove_if(pinnedCandles_.begin(), pinnedCandles_.end(),
                    [&](int idx) { return idx < 0 || idx >= (int)data.candles.size(); }),
                pinnedCandles_.end());

            int hoveredPinIdx = hovered ? FindHoveredPin(vp, data, io.MousePos) : -1;

            for (int p = 0; p < (int)pinnedCandles_.size(); ++p)
            {
                int anchor = pinnedCandles_[p];
                int lo = std::max(0, anchor - pinnedLookback / 2);
                int hi = std::min((int)data.candles.size(), anchor + pinnedLookback / 2 + 1);

                auto buckets = BuildBuckets(vp, data, lo, hi);
                if (buckets.empty()) continue;

                int relIdx = anchor - vp.visibleStart;
                float anchorX = vp.IndexToX(relIdx) + vp.candleWidth * 0.5f;
                float maxBarW = vp.chartSize.x * pinnedBarWidthPct;
                bool isHovered = (p == hoveredPinIdx);

                ImU32 lineCol = isHovered
                    ? IM_COL32(180, 200, 255, 200)
                    : anchorLineColor;
                drawList->AddLine(
                    ImVec2(anchorX, vp.chartOrigin.y),
                    ImVec2(anchorX, vp.chartOrigin.y + vp.chartSize.y),
                    lineCol, isHovered ? 1.5f : 1.f);

                DrawBars(drawList, vp, buckets, anchorX, maxBarW,
                         pinnedBullColor, pinnedBearColor, pinnedPocColor);

                VPPattern pat = DetectPattern(buckets);
                if (pat.shape != VPShape::None)
                    DrawPatternZone(drawList, vp, pat, anchorX, maxBarW, true);

                char label[96];
                if (pat.shape != VPShape::None)
                    snprintf(label, sizeof(label), "VP [%d] %s %.0f%%",
                             hi - lo, VPShapeName(pat.shape), pat.confidence * 100.f);
                else
                    snprintf(label, sizeof(label), "VP [%d bars]", hi - lo);

                ImU32 labelCol = isHovered
                    ? IM_COL32(220, 230, 255, 255)
                    : IM_COL32(180, 190, 210, 200);

                float labelY = vp.chartOrigin.y + 2.f + (float)p * 16.f;
                DrawSmallLabel(drawList, ImVec2(anchorX + 2.f, labelY), labelCol, label);

                if (isHovered)
                {
                    DrawSmallLabel(drawList,
                        ImVec2(anchorX + 2.f, labelY + 14.f),
                        IM_COL32(200, 100, 100, 200), "Right-click to remove");
                }
            }
        }

    private:
        std::vector<int> pinnedCandles_;

        struct Bucket
        {
            float priceLo    = 0.f;
            float priceHi    = 0.f;
            float bullVolume = 0.f;
            float bearVolume = 0.f;
            float Total() const { return bullVolume + bearVolume; }
            float PriceMid() const { return (priceLo + priceHi) * 0.5f; }
        };

        std::vector<Bucket> BuildBuckets(const ChartViewport& vp,
                                         const StockQuote& data,
                                         int candleLo, int candleHi) const
        {
            float pMin = vp.priceMin;
            float pMax = vp.priceMax;
            if (pMax <= pMin) return {};

            float bucketSize = (pMax - pMin) / (float)bucketCount;
            if (bucketSize <= 0.f) return {};

            std::vector<Bucket> buckets(bucketCount);
            for (int b = 0; b < bucketCount; ++b)
            {
                buckets[b].priceLo = pMin + (float)b * bucketSize;
                buckets[b].priceHi = pMin + (float)(b + 1) * bucketSize;
            }

            for (int i = candleLo; i < candleHi; ++i)
            {
                const auto& c = data.candles[i];
                float cLo = std::min(c.open, c.close);
                float cHi = std::max(c.open, c.close);
                bool bull = c.IsBullish();

                for (int b = 0; b < bucketCount; ++b)
                {
                    float bLo = buckets[b].priceLo;
                    float bHi = buckets[b].priceHi;
                    float overlapLo = std::max(cLo, bLo);
                    float overlapHi = std::min(cHi, bHi);

                    if (overlapLo < overlapHi)
                    {
                        float bodyRange = cHi - cLo;
                        float frac = (bodyRange > 0.f)
                            ? (overlapHi - overlapLo) / bodyRange
                            : 1.f / (float)bucketCount;
                        float vol = c.volume * frac;
                        if (bull) buckets[b].bullVolume += vol;
                        else      buckets[b].bearVolume += vol;
                    }
                    else if (cLo == cHi && c.close >= bLo && c.close < bHi)
                    {
                        if (bull) buckets[b].bullVolume += c.volume;
                        else      buckets[b].bearVolume += c.volume;
                    }
                }
            }

            return buckets;
        }

        VPPattern DetectPattern(const std::vector<Bucket>& buckets) const
        {
            VPPattern pat;
            if (buckets.size() < 4) return pat;

            int n = (int)buckets.size();

            float maxVol = 0.f;
            int pocIdx = 0;
            float totalVol = 0.f;
            for (int b = 0; b < n; ++b)
            {
                float t = buckets[b].Total();
                totalVol += t;
                if (t > maxVol) { maxVol = t; pocIdx = b; }
            }
            if (totalVol <= 0.f) return pat;

            pat.pocPrice = buckets[pocIdx].PriceMid();

            float vaTarget = totalVol * 0.70f;
            float vaVol = buckets[pocIdx].Total();
            int vaLo = pocIdx, vaHi = pocIdx;

            while (vaVol < vaTarget && (vaLo > 0 || vaHi < n - 1))
            {
                float belowVol = (vaLo > 0) ? buckets[vaLo - 1].Total() : 0.f;
                float aboveVol = (vaHi < n - 1) ? buckets[vaHi + 1].Total() : 0.f;

                if (belowVol >= aboveVol && vaLo > 0)
                    vaVol += buckets[--vaLo].Total();
                else if (vaHi < n - 1)
                    vaVol += buckets[++vaHi].Total();
                else if (vaLo > 0)
                    vaVol += buckets[--vaLo].Total();
                else break;
            }

            pat.valPrice = buckets[vaLo].priceLo;
            pat.vahPrice = buckets[vaHi].priceHi;

            float rangeSize = (float)n;
            float pocRelative = (float)pocIdx / rangeSize;

            float volAbove = 0.f, volBelow = 0.f;
            for (int b = 0; b < pocIdx; ++b) volBelow += buckets[b].Total();
            for (int b = pocIdx + 1; b < n; ++b) volAbove += buckets[b].Total();
            float skew = (totalVol > 0.f) ? (volAbove - volBelow) / totalVol : 0.f;

            float vaWidthRel = (float)(vaHi - vaLo + 1) / rangeSize;

            if (pocRelative < 0.38f && skew < -0.15f)
            {
                pat.shape = VPShape::PShape;
                pat.confidence = std::min(1.f, std::abs(skew) * 1.5f + (0.38f - pocRelative));
            }
            else if (pocRelative > 0.62f && skew > 0.15f)
            {
                pat.shape = VPShape::DShape;
                pat.confidence = std::min(1.f, std::abs(skew) * 1.5f + (pocRelative - 0.62f));
            }
            else if (pocRelative >= 0.30f && pocRelative <= 0.70f &&
                     std::abs(skew) < 0.20f && vaWidthRel < 0.50f)
            {
                pat.shape = VPShape::BShape;
                pat.confidence = std::min(1.f,
                    (1.f - std::abs(skew)) * 0.5f +
                    (1.f - vaWidthRel) * 0.3f +
                    (0.5f - std::abs(pocRelative - 0.5f)) * 0.4f);
            }

            if (pat.confidence < 0.25f)
                pat.shape = VPShape::None;

            return pat;
        }

        void DrawPatternZone(ImDrawList* drawList, const ChartViewport& vp,
                             const VPPattern& pat, float anchorX, float maxBarW,
                             bool pinned) const
        {
            ImU32 fillCol, borderCol;
            switch (pat.shape)
            {
            case VPShape::PShape: fillCol = pShapeFill; borderCol = pShapeBorder; break;
            case VPShape::DShape: fillCol = dShapeFill; borderCol = dShapeBorder; break;
            case VPShape::BShape: fillCol = bShapeFill; borderCol = bShapeBorder; break;
            default: return;
            }

            float yVAH = vp.PriceToY(pat.vahPrice);
            float yVAL = vp.PriceToY(pat.valPrice);
            float yPOC = vp.PriceToY(pat.pocPrice);

            float leftX  = anchorX;
            float chartRight = vp.chartOrigin.x + vp.chartSize.x;
            if (leftX + maxBarW > chartRight)
                leftX = chartRight - maxBarW;
            float rightX = leftX + maxBarW;

            drawList->AddRectFilled(
                ImVec2(leftX, yVAH), ImVec2(rightX, yVAL), fillCol);

            DrawDashedLine(drawList, ImVec2(leftX, yVAH), ImVec2(rightX, yVAH), borderCol);
            DrawDashedLine(drawList, ImVec2(leftX, yVAL), ImVec2(rightX, yVAL), borderCol);

            drawList->AddLine(
                ImVec2(leftX, yPOC), ImVec2(rightX, yPOC), pocLineColor, 1.5f);

            if (pinned)
            {
                float labelX = rightX + 2.f;
                char vahBuf[32]; snprintf(vahBuf, sizeof(vahBuf), "VAH %.2f", pat.vahPrice);
                char pocBuf[32]; snprintf(pocBuf, sizeof(pocBuf), "POC %.2f", pat.pocPrice);
                char valBuf[32]; snprintf(valBuf, sizeof(valBuf), "VAL %.2f", pat.valPrice);

                DrawSmallLabel(drawList, ImVec2(labelX, yVAH - 8.f), borderCol, vahBuf);
                DrawSmallLabel(drawList, ImVec2(labelX, yPOC - 8.f), pocLineColor, pocBuf);
                DrawSmallLabel(drawList, ImVec2(labelX, yVAL - 8.f), borderCol, valBuf);

                char badgeBuf[48];
                snprintf(badgeBuf, sizeof(badgeBuf), "%s (%.0f%%)",
                         VPShapeName(pat.shape), pat.confidence * 100.f);
                ImVec2 textSz = ImGui::CalcTextSize(badgeBuf);
                float badgeX = leftX + (maxBarW - textSz.x) * 0.5f - 4.f;
                float badgeY = std::max(yVAH - 20.f, vp.chartOrigin.y + 2.f);

                drawList->AddRectFilled(
                    ImVec2(badgeX, badgeY),
                    ImVec2(badgeX + textSz.x + 8.f, badgeY + textSz.y + 4.f),
                    IM_COL32(20, 20, 30, 220), 3.f);
                drawList->AddRect(
                    ImVec2(badgeX, badgeY),
                    ImVec2(badgeX + textSz.x + 8.f, badgeY + textSz.y + 4.f),
                    borderCol, 3.f);
                drawList->AddText(
                    ImVec2(badgeX + 4.f, badgeY + 2.f), borderCol, badgeBuf);
            }
        }

        void DrawBars(ImDrawList* drawList, const ChartViewport& vp,
                      const std::vector<Bucket>& buckets,
                      float leftX, float maxBarW,
                      ImU32 bullCol, ImU32 bearCol, ImU32 pocCol) const
        {
            float maxVol = 0.f;
            int pocIdx = 0;
            for (int b = 0; b < (int)buckets.size(); ++b)
            {
                float t = buckets[b].Total();
                if (t > maxVol) { maxVol = t; pocIdx = b; }
            }
            if (maxVol <= 0.f) return;

            float chartRight = vp.chartOrigin.x + vp.chartSize.x;
            if (leftX + maxBarW > chartRight)
                leftX = chartRight - maxBarW;

            for (int b = 0; b < (int)buckets.size(); ++b)
            {
                const auto& bkt = buckets[b];
                if (bkt.Total() <= 0.f) continue;

                float yTop = vp.PriceToY(bkt.priceHi);
                float yBot = vp.PriceToY(bkt.priceLo);
                float bullW = (bkt.bullVolume / maxVol) * maxBarW;
                float bearW = (bkt.bearVolume / maxVol) * maxBarW;
                float barW  = bullW + bearW;

                if (bullW > 0.f)
                    drawList->AddRectFilled(
                        ImVec2(leftX, yTop), ImVec2(leftX + bullW, yBot), bullCol);
                if (bearW > 0.f)
                    drawList->AddRectFilled(
                        ImVec2(leftX + bullW, yTop),
                        ImVec2(leftX + bullW + bearW, yBot), bearCol);
                if (b == pocIdx)
                    drawList->AddRectFilled(
                        ImVec2(leftX, yTop), ImVec2(leftX + barW, yBot), pocCol);
            }
        }

        int FindHoveredPin(const ChartViewport& vp, const StockQuote& data,
                           ImVec2 mousePos) const
        {
            float maxBarW = vp.chartSize.x * pinnedBarWidthPct;
            float chartRight = vp.chartOrigin.x + vp.chartSize.x;
            for (int p = 0; p < (int)pinnedCandles_.size(); ++p)
            {
                int anchor = pinnedCandles_[p];
                int relIdx = anchor - vp.visibleStart;
                float anchorX = vp.IndexToX(relIdx) + vp.candleWidth * 0.5f;
                float leftX = anchorX;
                if (leftX + maxBarW > chartRight)
                    leftX = chartRight - maxBarW;
                if (mousePos.x >= leftX && mousePos.x <= leftX + maxBarW &&
                    mousePos.y >= vp.chartOrigin.y &&
                    mousePos.y <= vp.chartOrigin.y + vp.chartSize.y)
                    return p;
            }
            return -1;
        }

        void DrawDashedLine(ImDrawList* drawList, ImVec2 a, ImVec2 b,
                            ImU32 color, float dashLen = 6.f, float gapLen = 4.f) const
        {
            float x = a.x;
            while (x < b.x)
            {
                float endX = std::min(x + dashLen, b.x);
                drawList->AddLine(ImVec2(x, a.y), ImVec2(endX, a.y), color, 1.0f);
                x += dashLen + gapLen;
            }
        }

        void DrawSmallLabel(ImDrawList* drawList, ImVec2 pos, ImU32 color,
                            const char* text) const
        {
            ImVec2 textSz = ImGui::CalcTextSize(text);
            drawList->AddRectFilled(
                ImVec2(pos.x - 1.f, pos.y),
                ImVec2(pos.x + textSz.x + 3.f, pos.y + textSz.y + 1.f),
                IM_COL32(20, 20, 30, 200), 2.f);
            drawList->AddText(pos, color, text);
        }
    };

} // namespace stnks
