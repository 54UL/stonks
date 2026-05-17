#pragma once

#include <Charts/IStrategyRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace stnks
{
    // Renders a position tracking strategy on the chart.
    // Shows entry line, current P&L, quantity, and entry date.
    // The entry handle is draggable; confirm/cancel buttons control editing.
    class PositionRenderer : public IStrategyRenderer
    {
    public:
        const char* TypeName() const override { return "Position"; }

        // Supply live price so P&L can be calculated
        void SetCurrentPrice(float price) { currentPrice_ = price; }

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const Strategy& strategy, bool editing,
                  const std::vector<Candle>* candles = nullptr) override
        {
            if (strategy.entryPrice < vp.priceMin || strategy.entryPrice > vp.priceMax)
                return;

            float yEntry = vp.PriceToY(strategy.entryPrice);
            float left   = ResolveEntryX(vp, candles, strategy);
            float right  = vp.chartOrigin.x + vp.chartSize.x;

            bool active = strategy.IsActive();

            // --- Determine P&L coloring ---
            float pnlPct = strategy.UnrealizedPnLPercent(currentPrice_);
            bool inProfit = pnlPct >= 0.f;

            ImU32 entryCol   = active ? kEntryColor : IM_COL32(255, 200, 50, 80);
            ImU32 pnlZoneFill = inProfit
                ? IM_COL32(38, 166, 91, 25)
                : IM_COL32(214, 48, 49, 25);
            ImU32 pnlZoneBorder = inProfit
                ? IM_COL32(38, 166, 91, 60)
                : IM_COL32(214, 48, 49, 60);

            // --- P&L shaded zone (entry to current price) ---
            if (currentPrice_ > 0.f && active)
            {
                float yCurrent = vp.PriceToY(currentPrice_);
                float top    = std::min(yEntry, yCurrent);
                float bottom = std::max(yEntry, yCurrent);

                drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), pnlZoneFill);
                drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), pnlZoneBorder, 0.f, 0, 1.0f);

                // Current price dashed line
                DrawDashedLine(drawList, ImVec2(left, yCurrent), ImVec2(right, yCurrent),
                               inProfit ? IM_COL32(38, 166, 91, 120) : IM_COL32(214, 48, 49, 120));
            }

            // --- Entry line (solid, thicker for position) ---
            drawList->AddLine(ImVec2(left, yEntry), ImVec2(right, yEntry), entryCol, 2.0f);

            // --- Labels (when not editing) ---
            if (!editing)
            {
                float labelX = right - 200.f;

                // Entry + quantity label
                char entryBuf[64];
                if (strategy.quantity > 0.f)
                    snprintf(entryBuf, sizeof(entryBuf), "POS %.2f x%.0f", strategy.entryPrice, strategy.quantity);
                else
                    snprintf(entryBuf, sizeof(entryBuf), "POS %.2f", strategy.entryPrice);
                DrawLabel(drawList, ImVec2(labelX, yEntry - 16.f), entryCol, "%s", entryBuf);

                // Direction
                ImU32 dirCol = strategy.direction == StrategyDirection::Long
                    ? IM_COL32(38, 166, 91, 220) : IM_COL32(214, 48, 49, 220);
                DrawLabel(drawList, ImVec2(left + 4.f, yEntry + 2.f),
                          dirCol, "%s", DirectionToString(strategy.direction));

                // P&L badge
                if (currentPrice_ > 0.f && active)
                {
                    float pnl = strategy.UnrealizedPnL(currentPrice_);
                    ImU32 pnlCol = inProfit
                        ? IM_COL32(38, 166, 91, 255)
                        : IM_COL32(214, 48, 49, 255);

                    char pnlBuf[64];
                    if (strategy.quantity > 0.f)
                        snprintf(pnlBuf, sizeof(pnlBuf), "%+.2f (%+.1f%%)", pnl, pnlPct);
                    else
                        snprintf(pnlBuf, sizeof(pnlBuf), "%+.1f%%", pnlPct);

                    DrawLabel(drawList, ImVec2(labelX, yEntry + 2.f), pnlCol, "%s", pnlBuf);
                }

                // Entry date badge
                if (strategy.entryDate > 0)
                {
                    struct tm t;
                    time_t et = static_cast<time_t>(strategy.entryDate);
#ifdef _WIN32
                    localtime_s(&t, &et);
#else
                    localtime_r(&et, &t);
#endif
                    char dateBuf[32];
                    strftime(dateBuf, sizeof(dateBuf), "%b %d, %Y", &t);
                    DrawLabel(drawList, ImVec2(left + 4.f, yEntry - 16.f),
                              IM_COL32(180, 180, 200, 180), "%s", dateBuf);
                }

                // Status badge for closed positions
                if (!active)
                {
                    const char* badge = StatusToString(strategy.status);
                    ImU32 badgeCol = IM_COL32(150, 150, 150, 255);
                    ImVec2 badgePos(left + 80.f, yEntry + 2.f);
                    ImVec2 textSize = ImGui::CalcTextSize(badge);
                    drawList->AddRectFilled(
                        ImVec2(badgePos.x - 2.f, badgePos.y - 1.f),
                        ImVec2(badgePos.x + textSize.x + 4.f, badgePos.y + textSize.y + 2.f),
                        kLabelBgColor, 3.f);
                    drawList->AddText(badgePos, badgeCol, badge);
                }
            }
        }

        StrategyInteraction HandleGizmos(ImDrawList* drawList, const ChartViewport& vp,
                                         Strategy& strategy) override
        {
            StrategyInteraction result;
            ImGuiIO& io = ImGui::GetIO();

            float yEntry = vp.PriceToY(strategy.entryPrice);
            float right  = vp.chartOrigin.x + vp.chartSize.x;

            // --- Entry drag handle ---
            bool hovered = DrawGizmoHandle(drawList, vp, yEntry, kEntryColor, kEntryHoverColor,
                                            strategy.entryPrice, "Entry");

            // --- Handle drag ---
            if (isDragging_)
            {
                float newPrice = vp.YToPrice(io.MousePos.y);
                strategy.entryPrice = std::max(0.01f, newPrice);
                result.modified = true;

                drawList->AddLine(
                    ImVec2(vp.chartOrigin.x, io.MousePos.y),
                    ImVec2(right, io.MousePos.y),
                    IM_COL32(255, 255, 255, 60), 1.0f);

                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    isDragging_ = false;
            }
            else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                isDragging_ = true;
            }

            // --- Info panel ---
            {
                float panelX = vp.chartOrigin.x + 8.f;
                float panelY = yEntry - 32.f;
                panelY = std::max(panelY, vp.chartOrigin.y + 4.f);

                char infoBuf[128];
                snprintf(infoBuf, sizeof(infoBuf),
                    "%s Position  |  Entry %.2f  |  Qty %.0f",
                    DirectionToString(strategy.direction),
                    strategy.entryPrice, strategy.quantity);

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

            // --- Confirm / Cancel buttons ---
            {
                float btnY = yEntry + 28.f;
                btnY = std::min(btnY, vp.chartOrigin.y + vp.chartSize.y - 24.f);
                float btnX = right - 140.f;

                result.confirmed = DrawButton(drawList, ImVec2(btnX, btnY),
                    "Confirm", IM_COL32(38, 166, 91, 200), IM_COL32(38, 166, 91, 255));

                result.cancelled = DrawButton(drawList, ImVec2(btnX + 72.f, btnY),
                    "Cancel", IM_COL32(180, 60, 60, 200), IM_COL32(214, 48, 49, 255));
            }

            if (hovered || isDragging_)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            return result;
        }

    private:
        float currentPrice_ = 0.f;
        bool  isDragging_   = false;

        static constexpr float kHandleW = 110.f;
        static constexpr float kHandleH = 20.f;

        static constexpr ImU32 kEntryColor      = IM_COL32(100, 180, 255, 200);
        static constexpr ImU32 kEntryHoverColor  = IM_COL32(130, 200, 255, 255);
        static constexpr ImU32 kLabelBgColor     = IM_COL32(30, 30, 40, 200);

        bool DrawGizmoHandle(ImDrawList* drawList, const ChartViewport& vp,
                             float y, ImU32 color, ImU32 hoverColor,
                             float price, const char* prefix)
        {
            float x = vp.chartOrigin.x + vp.chartSize.x - kHandleW - 10.f;
            float hy = y - kHandleH * 0.5f;

            ImVec2 min(x, hy);
            ImVec2 max(x + kHandleW, hy + kHandleH);

            bool hovered = ImGui::IsMouseHoveringRect(min, max) && !isDragging_;
            bool active  = isDragging_;

            ImU32 col = (hovered || active) ? hoverColor : color;
            ImU32 bgCol = (hovered || active)
                ? IM_COL32(40, 40, 55, 240)
                : IM_COL32(25, 25, 35, 220);

            drawList->AddRectFilled(min, max, bgCol, 4.f);
            drawList->AddRect(min, max, col, 4.f, 0, 1.5f);

            // Grip dots
            float gripX = x + 8.f;
            for (int i = 0; i < 3; ++i)
            {
                float dotY = y - 4.f + (float)i * 4.f;
                drawList->AddCircleFilled(ImVec2(gripX, dotY), 1.5f,
                    (hovered || active) ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 80));
            }

            char buf[48];
            snprintf(buf, sizeof(buf), "%s %.2f", prefix, price);
            ImVec2 textSz = ImGui::CalcTextSize(buf);
            drawList->AddText(
                ImVec2(x + 18.f, hy + (kHandleH - textSz.y) * 0.5f),
                col, buf);

            // Connecting line
            drawList->AddLine(ImVec2(x, y), ImVec2(vp.chartOrigin.x, y),
                (hovered || active) ? col : IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 40),
                (hovered || active) ? 1.5f : 0.5f);

            return hovered;
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

            drawList->AddRectFilled(min, max,
                hovered ? IM_COL32(50, 50, 65, 240) : IM_COL32(30, 30, 40, 220), 4.f);
            drawList->AddRect(min, max, hovered ? hoverColor : color, 4.f, 0, 1.5f);
            drawList->AddText(ImVec2(pos.x + 8.f, pos.y + 4.f), hovered ? hoverColor : color, label);

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
