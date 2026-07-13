#pragma once

#include <Charts/IStrategyRenderer.hpp>
#include <Market/MarketHours.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace stnks
{
    class TPSLRenderer : public IStrategyRenderer
    {
    public:
        const char* TypeName() const override { return "TP / SL"; }


        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const Strategy& strategy, bool editing,
                  const std::vector<Candle>* candles = nullptr) override
        {
            float top    = std::max({strategy.takeProfit, strategy.entryPrice, strategy.stopLoss});
            float bottom = std::min({strategy.stopLoss, strategy.entryPrice, strategy.takeProfit});

            if (top < vp.priceMin || bottom > vp.priceMax) return;

            float yEntry = vp.PriceToY(strategy.entryPrice);
            float yTP    = vp.PriceToY(strategy.takeProfit);
            float ySL    = vp.PriceToY(strategy.stopLoss);

            float left  = ResolveEntryX(vp, candles, strategy);
            float right = ResolveRightX(vp, candles, strategy);

            bool active    = strategy.status == StrategyStatus::Active;
            bool tpHit     = strategy.status == StrategyStatus::TPHit;
            bool slHit     = strategy.status == StrategyStatus::SLHit;

            ImU32 tpFill = active ? kTPFillColor : (tpHit ? kTPTriggeredFill : kCancelledFill);
            ImU32 tpBord = active ? kTPBorderColor : IM_COL32(38, 166, 91, 60);

            float tpTop    = std::min(yEntry, yTP);
            float tpBottom = std::max(yEntry, yTP);

            drawList->AddRectFilled(ImVec2(left, tpTop), ImVec2(right, tpBottom), tpFill);
            drawList->AddRect(ImVec2(left, tpTop), ImVec2(right, tpBottom), tpBord, 0.f, 0, 1.0f);



            ImU32 slFill = active ? kSLFillColor : (slHit ? kSLTriggeredFill : kCancelledFill);
            ImU32 slBord = active ? kSLBorderColor : IM_COL32(214, 48, 49, 60);

            float slTop    = std::min(yEntry, ySL);
            float slBottom = std::max(yEntry, ySL);

            drawList->AddRectFilled(ImVec2(left, slTop), ImVec2(right, slBottom), slFill);
            drawList->AddRect(ImVec2(left, slTop), ImVec2(right, slBottom), slBord, 0.f, 0, 1.0f);



            ImU32 entryCol = active ? kEntryColor : IM_COL32(255, 200, 50, 80);
            drawList->AddLine(ImVec2(left, yEntry), ImVec2(right, yEntry), entryCol, 1.5f);



            DrawDashedLine(drawList, ImVec2(left, yTP), ImVec2(right, yTP), tpBord);
            DrawDashedLine(drawList, ImVec2(left, ySL), ImVec2(right, ySL), slBord);



            if (!editing)
            {
                float labelX = right - 150.f;

                { char pb[32]; FmtPrice(pb, sizeof(pb), strategy.entryPrice, strategy.symbol);
                DrawLabel(drawList, ImVec2(labelX, yEntry - 16.f),
                          entryCol, "Entry %s", pb); }

                char tpBuf[64]; char tpPb[32];
                FmtPrice(tpPb, sizeof(tpPb), strategy.takeProfit, strategy.symbol);
                snprintf(tpBuf, sizeof(tpBuf), "TP %s (%+.2f%%)",
                         tpPb, strategy.TPPercent());
                DrawLabel(drawList, ImVec2(labelX, yTP + (yTP < yEntry ? -16.f : 2.f)),
                          tpBord, "%s", tpBuf);

                char slBuf[64]; char slPb[32];
                FmtPrice(slPb, sizeof(slPb), strategy.stopLoss, strategy.symbol);
                snprintf(slBuf, sizeof(slBuf), "SL %s (%+.2f%%)",
                         slPb, strategy.SLPercent());
                DrawLabel(drawList, ImVec2(labelX, ySL + (ySL > yEntry ? 2.f : -16.f)),
                          slBord, "%s", slBuf);



                char rrBuf[32];
                snprintf(rrBuf, sizeof(rrBuf), "R:R %.1f", strategy.RiskReward());
                DrawLabel(drawList, ImVec2(left + 4.f, yEntry - 16.f),
                          IM_COL32(180, 180, 200, 200), "%s", rrBuf);



                ImU32 dirCol = strategy.direction == StrategyDirection::Long
                    ? IM_COL32(38, 166, 91, 220) : IM_COL32(214, 48, 49, 220);
                DrawLabel(drawList, ImVec2(left + 4.f, yEntry + 2.f),
                          dirCol, "%s", DirectionToString(strategy.direction));



                if (!active)
                {
                    const char* badge = StatusToString(strategy.status);
                    ImU32 badgeCol = tpHit  ? IM_COL32(38, 166, 91, 255)
                                  : slHit  ? IM_COL32(214, 48, 49, 255)
                                           : IM_COL32(150, 150, 150, 255);

                    ImVec2 badgePos(left + 80.f, yEntry + 2.f);
                    ImVec2 textSize = ImGui::CalcTextSize(badge);
                    drawList->AddRectFilled(
                        ImVec2(badgePos.x - 2.f, badgePos.y - 1.f),
                        ImVec2(badgePos.x + textSize.x + 4.f, badgePos.y + textSize.y + 2.f),
                        kLabelBgColor, 3.f);
                    drawList->AddText(badgePos, badgeCol, badge);



                    if (strategy.triggeredAt > 0)
                    {
                        time_t tt = static_cast<time_t>(strategy.triggeredAt);
                        struct tm t;
#ifdef _WIN32
                        localtime_s(&t, &tt);
#else
                        localtime_r(&tt, &t);
#endif
                        char trigBuf[64];
                        strftime(trigBuf, sizeof(trigBuf), "%b %d %H:%M", &t);
                        char exitBuf[96];
                        if (strategy.exitPrice > 0.f)
                        {
                            char epb[32]; FmtPrice(epb, sizeof(epb), strategy.exitPrice, strategy.symbol);
                            snprintf(exitBuf, sizeof(exitBuf), "%s @ %s (%+.2f%%)",
                                     trigBuf, epb, strategy.closedPnlPct);
                        }
                        else
                            snprintf(exitBuf, sizeof(exitBuf), "%s", trigBuf);

                        float exitLabelX = right - 4.f;
                        ImVec2 exitSz = ImGui::CalcTextSize(exitBuf);
                        exitLabelX -= exitSz.x;
                        float exitLabelY = yEntry + 2.f;
                        DrawLabel(drawList, ImVec2(exitLabelX, exitLabelY), badgeCol, "%s", exitBuf);
                    }
                }
            }
        }


        StrategyInteraction HandleGizmos(ImDrawList* drawList, const ChartViewport& vp,
                                         Strategy& strategy) override
        {
            StrategyInteraction result;
            ImGuiIO& io = ImGui::GetIO();

            bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            float yEntry = vp.PriceToY(strategy.entryPrice);
            float yTP    = vp.PriceToY(strategy.takeProfit);
            float ySL    = vp.PriceToY(strategy.stopLoss);

            float right = vp.chartOrigin.x + vp.chartSize.x;

            bool lineHoveredTP    = windowHovered && IsHoveringLine(vp, yTP);
            bool lineHoveredEntry = windowHovered && IsHoveringLine(vp, yEntry);
            bool lineHoveredSL    = windowHovered && IsHoveringLine(vp, ySL);
            bool anyLineHovered   = lineHoveredTP || lineHoveredEntry || lineHoveredSL;

            if (lineHoveredTP && !isDragging_)
                drawList->AddLine(ImVec2(vp.chartOrigin.x, yTP), ImVec2(right, yTP),
                                  kTPHoverColor, 2.0f);
            if (lineHoveredEntry && !isDragging_)
                drawList->AddLine(ImVec2(vp.chartOrigin.x, yEntry), ImVec2(right, yEntry),
                                  kEntryHoverColor, 2.0f);
            if (lineHoveredSL && !isDragging_)
                drawList->AddLine(ImVec2(vp.chartOrigin.x, ySL), ImVec2(right, ySL),
                                  kSLHoverColor, 2.0f);

            bool anyHandleHovered = false;

            anyHandleHovered |= DrawGizmoHandle(drawList, vp, yTP, kTPBorderColor, kTPHoverColor,
                                          strategy.takeProfit, strategy.TPPercent(),
                                          "TP", DragTarget::TP, windowHovered);

            anyHandleHovered |= DrawGizmoHandle(drawList, vp, yEntry, kEntryColor, kEntryHoverColor,
                                          strategy.entryPrice, 0.f,
                                          "Entry", DragTarget::Entry, windowHovered);

            anyHandleHovered |= DrawGizmoHandle(drawList, vp, ySL, kSLBorderColor, kSLHoverColor,
                                          strategy.stopLoss, strategy.SLPercent(),
                                          "SL", DragTarget::SL, windowHovered);

            bool anyHovered = anyHandleHovered || anyLineHovered;

            if (isDragging_)
            {
                float newPrice = vp.YToPrice(io.MousePos.y);
                newPrice = std::max(0.01f, newPrice);

                bool isLong = strategy.direction == StrategyDirection::Long;

                switch (activeTarget_)
                {
                case DragTarget::Entry:
                {
                    float tpOffset = strategy.takeProfit - strategy.entryPrice;
                    float slOffset = strategy.stopLoss - strategy.entryPrice;
                    strategy.entryPrice = newPrice;
                    strategy.takeProfit = newPrice + tpOffset;
                    strategy.stopLoss   = newPrice + slOffset;
                    break;
                }
                case DragTarget::TP:


                    if (isLong)
                        newPrice = std::max(newPrice, strategy.entryPrice + 0.01f);
                    else
                        newPrice = std::min(newPrice, strategy.entryPrice - 0.01f);
                    strategy.takeProfit = newPrice;
                    break;
                case DragTarget::SL:


                    if (isLong)
                        newPrice = std::min(newPrice, strategy.entryPrice - 0.01f);
                    else
                        newPrice = std::max(newPrice, strategy.entryPrice + 0.01f);
                    strategy.stopLoss = newPrice;
                    break;
                default: break;
                }

                result.modified = true;



                drawList->AddLine(
                    ImVec2(vp.chartOrigin.x, io.MousePos.y),
                    ImVec2(right, io.MousePos.y),
                    IM_COL32(255, 255, 255, 60), 1.0f);

                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    isDragging_ = false;
                    activeTarget_ = DragTarget::None;
                }
            }
            else
            {


                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (anyHandleHovered)
                    {
                        isDragging_ = true;


                    }
                    else if (anyLineHovered)
                    {
                        isDragging_ = true;


                        if (lineHoveredTP)    activeTarget_ = DragTarget::TP;
                        if (lineHoveredEntry) activeTarget_ = DragTarget::Entry;
                        if (lineHoveredSL)    activeTarget_ = DragTarget::SL;
                    }
                }
            }



            {
                float panelX = vp.chartOrigin.x + 8.f;
                float panelY = std::min({yTP, yEntry, ySL}) - 32.f;
                panelY = std::max(panelY, vp.chartOrigin.y + 4.f);

                char infoBuf[128];
                snprintf(infoBuf, sizeof(infoBuf),
                    "%s  |  R:R %.1f  |  TP %+.2f%%  |  SL %+.2f%%",
                    DirectionToString(strategy.direction),
                    strategy.RiskReward(),
                    strategy.TPPercent(), strategy.SLPercent());

                ImVec2 textSz = ImGui::CalcTextSize(infoBuf);
                drawList->AddRectFilled(
                    ImVec2(panelX - 4.f, panelY - 2.f),
                    ImVec2(panelX + textSz.x + 8.f, panelY + textSz.y + 4.f),
                    IM_COL32(20, 20, 30, 230), 4.f);
                drawList->AddRect(
                    ImVec2(panelX - 4.f, panelY - 2.f),
                    ImVec2(panelX + textSz.x + 8.f, panelY + textSz.y + 4.f),
                    IM_COL32(80, 80, 100, 180), 4.f);
                drawList->AddText(ImVec2(panelX, panelY), IM_COL32(200, 200, 220, 255), infoBuf);
            }



            {
                float btnY = std::max({yTP, yEntry, ySL}) + 8.f;
                btnY = std::min(btnY, vp.chartOrigin.y + vp.chartSize.y - 28.f);



                float barX = vp.chartOrigin.x + 4.f;
                float barW = 216.f;
                float barH = 26.f;
                drawList->AddRectFilled(
                    ImVec2(barX - 4.f, btnY - 2.f),
                    ImVec2(barX + barW + 4.f, btnY + barH + 2.f),
                    IM_COL32(16, 18, 26, 230), 6.f);
                drawList->AddRect(
                    ImVec2(barX - 4.f, btnY - 2.f),
                    ImVec2(barX + barW + 4.f, btnY + barH + 2.f),
                    IM_COL32(60, 64, 80, 140), 6.f);

                float innerY = btnY + 2.f;
                result.confirmed = DrawButton(drawList, ImVec2(barX, innerY),
                    "Confirm", IM_COL32(38, 166, 91, 200), IM_COL32(38, 166, 91, 255));

                result.cancelled = DrawButton(drawList, ImVec2(barX + 76.f, innerY),
                    "Cancel", IM_COL32(140, 140, 160, 200), IM_COL32(180, 180, 200, 255));

                result.deleted = DrawButton(drawList, ImVec2(barX + 140.f, innerY),
                    "Delete", IM_COL32(180, 40, 40, 200), IM_COL32(230, 60, 60, 255));
            }



            if (anyHovered || isDragging_)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            return result;
        }

    private:

        enum class DragTarget { None, Entry, TP, SL };
        DragTarget activeTarget_ = DragTarget::None;
        bool       isDragging_   = false;


        static constexpr float kHandleW = 90.f;
        static constexpr float kHandleH = 20.f;
        static constexpr float kLineHitTolerance = 5.f;

        static constexpr ImU32 kTPFillColor      = IM_COL32(38, 166, 91, 40);
        static constexpr ImU32 kTPBorderColor     = IM_COL32(38, 166, 91, 180);
        static constexpr ImU32 kTPHoverColor      = IM_COL32(50, 200, 110, 240);
        static constexpr ImU32 kTPTriggeredFill   = IM_COL32(38, 166, 91, 20);

        static constexpr ImU32 kSLFillColor       = IM_COL32(214, 48, 49, 40);
        static constexpr ImU32 kSLBorderColor     = IM_COL32(214, 48, 49, 180);
        static constexpr ImU32 kSLHoverColor      = IM_COL32(240, 70, 70, 240);
        static constexpr ImU32 kSLTriggeredFill   = IM_COL32(214, 48, 49, 20);

        static constexpr ImU32 kEntryColor        = IM_COL32(255, 200, 50, 200);
        static constexpr ImU32 kEntryHoverColor   = IM_COL32(255, 220, 80, 255);

        static constexpr ImU32 kCancelledFill     = IM_COL32(100, 100, 100, 15);
        static constexpr ImU32 kLabelBgColor      = IM_COL32(30, 30, 40, 200);


        bool DrawGizmoHandle(ImDrawList* drawList, const ChartViewport& vp,
                             float y, ImU32 color, ImU32 hoverColor,
                             float price, float pct,
                             const char* prefix, DragTarget target,
                             bool windowHovered = true)
        {
            float x = vp.chartOrigin.x + 10.f;
            float hy = y - kHandleH * 0.5f;

            ImVec2 min(x, hy);
            ImVec2 max(x + kHandleW, hy + kHandleH);

            bool hovered = windowHovered && ImGui::IsMouseHoveringRect(min, max) && !isDragging_;
            bool active  = (activeTarget_ == target && isDragging_);

            ImU32 col = (hovered || active) ? hoverColor : color;
            ImU32 bgCol = (hovered || active)
                ? IM_COL32(40, 40, 55, 240)
                : IM_COL32(25, 25, 35, 220);

            drawList->AddRectFilled(min, max, bgCol, 4.f);
            drawList->AddRect(min, max, col, 4.f, 0, 1.5f);



            float gripX = x + 8.f;
            for (int i = 0; i < 3; ++i)
            {
                float dotY = y - 4.f + (float)i * 4.f;
                drawList->AddCircleFilled(ImVec2(gripX, dotY), 1.5f,
                    (hovered || active) ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 80));
            }



            char buf[48];
            if (std::abs(pct) > 0.01f)
                snprintf(buf, sizeof(buf), "%s %.2f (%+.2f%%)", prefix, price, pct);
            else
                snprintf(buf, sizeof(buf), "%s %.2f", prefix, price);

            ImVec2 textSz = ImGui::CalcTextSize(buf);
            float tx = x + 18.f;
            float ty = hy + (kHandleH - textSz.y) * 0.5f;
            drawList->AddText(ImVec2(tx, ty), col, buf);



            float right = vp.chartOrigin.x + vp.chartSize.x;
            drawList->AddLine(ImVec2(x + kHandleW, y), ImVec2(right, y),
                (hovered || active) ? col : IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 40),
                (hovered || active) ? 1.5f : 0.5f);



            if (hovered)
                activeTarget_ = target;

            return hovered || active;
        }


        bool DrawButton(ImDrawList* drawList, ImVec2 pos,
                        const char* label, ImU32 color, ImU32 hoverColor)
        {
            ImVec2 textSz = ImGui::CalcTextSize(label);
            float w = textSz.x + 16.f;
            float h = textSz.y + 8.f;

            ImVec2 min = pos;
            ImVec2 max(pos.x + w, pos.y + h);

            bool hovered = ImGui::IsMouseHoveringRect(min, max);
            bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

            ImU32 bg = hovered ? IM_COL32(50, 50, 65, 240) : IM_COL32(30, 30, 40, 220);
            ImU32 col = hovered ? hoverColor : color;

            drawList->AddRectFilled(min, max, bg, 4.f);
            drawList->AddRect(min, max, col, 4.f, 0, 1.5f);
            drawList->AddText(
                ImVec2(pos.x + 8.f, pos.y + 4.f),
                col, label);

            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            return clicked;
        }


        void DrawDashedLine(ImDrawList* drawList, ImVec2 a, ImVec2 b, ImU32 color,
                            float dashLen = 6.f, float gapLen = 4.f)
        {
            float x = a.x;
            while (x < b.x)
            {
                float endX = std::min(x + dashLen, b.x);
                drawList->AddLine(ImVec2(x, a.y), ImVec2(endX, a.y), color, 1.0f);
                x += dashLen + gapLen;
            }
        }

        // Check if mouse is hovering near a horizontal line across the chart
        bool IsHoveringLine(const ChartViewport& vp, float lineY) const
        {
            if (isDragging_) return false;
            if (!ImGui::IsWindowHovered()) return false;
            ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x < vp.chartOrigin.x ||
                mouse.x > vp.chartOrigin.x + vp.chartSize.x) return false;
            return std::abs(mouse.y - lineY) < kLineHitTolerance;
        }

        template<typename... Args>
        void DrawLabel(ImDrawList* drawList, ImVec2 pos, ImU32 color,
                       const char* fmt, Args... args)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), fmt, args...);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            drawList->AddRectFilled(
                ImVec2(pos.x - 2.f, pos.y),
                ImVec2(pos.x + textSize.x + 4.f, pos.y + textSize.y + 1.f),
                kLabelBgColor, 2.f);
            drawList->AddText(pos, color, buf);
        }
    };

} // namespace stnks
