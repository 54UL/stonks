#pragma once

#include <Charts/IStrategyRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace stnks
{
    class TPSLRenderer : public IStrategyRenderer
    {
    public:
        const char* TypeName() const override { return "TP / SL"; }

        // ── Draw strategy visualization ─────────────────────────────────────

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

            // Start rendering from entry position on the chart
            float left  = ResolveEntryX(vp, candles, strategy);
            float right = vp.chartOrigin.x + vp.chartSize.x;

            bool active    = strategy.status == StrategyStatus::Active;
            bool tpHit     = strategy.status == StrategyStatus::TPHit;
            bool slHit     = strategy.status == StrategyStatus::SLHit;

            // --- Take profit zone ---
            ImU32 tpFill = active ? kTPFillColor : (tpHit ? kTPTriggeredFill : kCancelledFill);
            ImU32 tpBord = active ? kTPBorderColor : IM_COL32(38, 166, 91, 60);

            float tpTop    = std::min(yEntry, yTP);
            float tpBottom = std::max(yEntry, yTP);

            drawList->AddRectFilled(ImVec2(left, tpTop), ImVec2(right, tpBottom), tpFill);
            drawList->AddRect(ImVec2(left, tpTop), ImVec2(right, tpBottom), tpBord, 0.f, 0, 1.0f);

            // --- Stop loss zone ---
            ImU32 slFill = active ? kSLFillColor : (slHit ? kSLTriggeredFill : kCancelledFill);
            ImU32 slBord = active ? kSLBorderColor : IM_COL32(214, 48, 49, 60);

            float slTop    = std::min(yEntry, ySL);
            float slBottom = std::max(yEntry, ySL);

            drawList->AddRectFilled(ImVec2(left, slTop), ImVec2(right, slBottom), slFill);
            drawList->AddRect(ImVec2(left, slTop), ImVec2(right, slBottom), slBord, 0.f, 0, 1.0f);

            // --- Entry line (solid) ---
            ImU32 entryCol = active ? kEntryColor : IM_COL32(255, 200, 50, 80);
            drawList->AddLine(ImVec2(left, yEntry), ImVec2(right, yEntry), entryCol, 1.5f);

            // --- TP / SL dashed lines ---
            DrawDashedLine(drawList, ImVec2(left, yTP), ImVec2(right, yTP), tpBord);
            DrawDashedLine(drawList, ImVec2(left, ySL), ImVec2(right, ySL), slBord);

            // --- Labels (only when NOT editing — gizmos replace them) ---
            if (!editing)
            {
                float labelX = right - 150.f;

                DrawLabel(drawList, ImVec2(labelX, yEntry - 16.f),
                          entryCol, "Entry %.2f", strategy.entryPrice);

                char tpBuf[64];
                snprintf(tpBuf, sizeof(tpBuf), "TP %.2f (%+.1f%%)",
                         strategy.takeProfit, strategy.TPPercent());
                DrawLabel(drawList, ImVec2(labelX, yTP + (yTP < yEntry ? -16.f : 2.f)),
                          tpBord, "%s", tpBuf);

                char slBuf[64];
                snprintf(slBuf, sizeof(slBuf), "SL %.2f (%+.1f%%)",
                         strategy.stopLoss, strategy.SLPercent());
                DrawLabel(drawList, ImVec2(labelX, ySL + (ySL > yEntry ? 2.f : -16.f)),
                          slBord, "%s", slBuf);

                // R:R label
                char rrBuf[32];
                snprintf(rrBuf, sizeof(rrBuf), "R:R %.1f", strategy.RiskReward());
                DrawLabel(drawList, ImVec2(left + 4.f, yEntry - 16.f),
                          IM_COL32(180, 180, 200, 200), "%s", rrBuf);

                // Direction label
                ImU32 dirCol = strategy.direction == StrategyDirection::Long
                    ? IM_COL32(38, 166, 91, 220) : IM_COL32(214, 48, 49, 220);
                DrawLabel(drawList, ImVec2(left + 4.f, yEntry + 2.f),
                          dirCol, "%s", DirectionToString(strategy.direction));

                // Status badge for triggered/cancelled
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
                }
            }
        }

        // ── Interactive gizmos ──────────────────────────────────────────────

        StrategyInteraction HandleGizmos(ImDrawList* drawList, const ChartViewport& vp,
                                         Strategy& strategy) override
        {
            StrategyInteraction result;
            ImGuiIO& io = ImGui::GetIO();

            float yEntry = vp.PriceToY(strategy.entryPrice);
            float yTP    = vp.PriceToY(strategy.takeProfit);
            float ySL    = vp.PriceToY(strategy.stopLoss);

            float right = vp.chartOrigin.x + vp.chartSize.x;

            // --- Check if mouse hovers any of the full-width lines ---
            bool lineHoveredTP    = IsHoveringLine(vp, yTP);
            bool lineHoveredEntry = IsHoveringLine(vp, yEntry);
            bool lineHoveredSL    = IsHoveringLine(vp, ySL);
            bool anyLineHovered   = lineHoveredTP || lineHoveredEntry || lineHoveredSL;

            // Highlight hovered lines with a brighter redraw
            if (lineHoveredTP && !isDragging_)
                drawList->AddLine(ImVec2(vp.chartOrigin.x, yTP), ImVec2(right, yTP),
                                  kTPHoverColor, 2.0f);
            if (lineHoveredEntry && !isDragging_)
                drawList->AddLine(ImVec2(vp.chartOrigin.x, yEntry), ImVec2(right, yEntry),
                                  kEntryHoverColor, 2.0f);
            if (lineHoveredSL && !isDragging_)
                drawList->AddLine(ImVec2(vp.chartOrigin.x, ySL), ImVec2(right, ySL),
                                  kSLHoverColor, 2.0f);

            // --- Draw and interact with drag handles (buttons on the right) ---
            bool anyHandleHovered = false;

            anyHandleHovered |= DrawGizmoHandle(drawList, vp, yTP, kTPBorderColor, kTPHoverColor,
                                          strategy.takeProfit, strategy.TPPercent(),
                                          "TP", DragTarget::TP);

            anyHandleHovered |= DrawGizmoHandle(drawList, vp, yEntry, kEntryColor, kEntryHoverColor,
                                          strategy.entryPrice, 0.f,
                                          "Entry", DragTarget::Entry);

            anyHandleHovered |= DrawGizmoHandle(drawList, vp, ySL, kSLBorderColor, kSLHoverColor,
                                          strategy.stopLoss, strategy.SLPercent(),
                                          "SL", DragTarget::SL);

            bool anyHovered = anyHandleHovered || anyLineHovered;

            // --- Handle drag logic ---
            if (isDragging_)
            {
                float newPrice = vp.YToPrice(io.MousePos.y);
                newPrice = std::max(0.01f, newPrice);

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
                    strategy.takeProfit = newPrice;
                    break;
                case DragTarget::SL:
                    strategy.stopLoss = newPrice;
                    break;
                default: break;
                }

                result.modified = true;

                // Draw drag guide line
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
                // Start drag on click — handles take priority, then lines
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (anyHandleHovered)
                    {
                        isDragging_ = true;
                        // activeTarget_ was set by DrawGizmoHandle
                    }
                    else if (anyLineHovered)
                    {
                        isDragging_ = true;
                        // Pick the closest line if overlapping
                        if (lineHoveredTP)    activeTarget_ = DragTarget::TP;
                        if (lineHoveredEntry) activeTarget_ = DragTarget::Entry;
                        if (lineHoveredSL)    activeTarget_ = DragTarget::SL;
                    }
                }
            }

            // --- R:R info panel ---
            {
                float panelX = vp.chartOrigin.x + 8.f;
                float panelY = std::min({yTP, yEntry, ySL}) - 32.f;
                panelY = std::max(panelY, vp.chartOrigin.y + 4.f);

                char infoBuf[128];
                snprintf(infoBuf, sizeof(infoBuf),
                    "%s  |  R:R %.1f  |  TP %+.1f%%  |  SL %+.1f%%",
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

            // --- Confirm / Cancel buttons ---
            {
                float btnY = std::max({yTP, yEntry, ySL}) + 8.f;
                btnY = std::min(btnY, vp.chartOrigin.y + vp.chartSize.y - 24.f);
                float btnX = right - 140.f;

                result.confirmed = DrawButton(drawList, ImVec2(btnX, btnY),
                    "Confirm", IM_COL32(38, 166, 91, 200), IM_COL32(38, 166, 91, 255));

                result.cancelled = DrawButton(drawList, ImVec2(btnX + 72.f, btnY),
                    "Cancel", IM_COL32(180, 60, 60, 200), IM_COL32(214, 48, 49, 255));
            }

            // Change cursor when hovering handles
            if (anyHovered || isDragging_)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            return result;
        }

    private:
        // ── Drag state ──────────────────────────────────────────────────────

        enum class DragTarget { None, Entry, TP, SL };
        DragTarget activeTarget_ = DragTarget::None;
        bool       isDragging_   = false;

        // ── Constants ───────────────────────────────────────────────────────

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

        // ── Gizmo handle drawing ────────────────────────────────────────────

        bool DrawGizmoHandle(ImDrawList* drawList, const ChartViewport& vp,
                             float y, ImU32 color, ImU32 hoverColor,
                             float price, float pct,
                             const char* prefix, DragTarget target)
        {
            float x = vp.chartOrigin.x + vp.chartSize.x - kHandleW - 10.f;
            float hy = y - kHandleH * 0.5f;

            ImVec2 min(x, hy);
            ImVec2 max(x + kHandleW, hy + kHandleH);

            bool hovered = ImGui::IsMouseHoveringRect(min, max) && !isDragging_;
            bool active  = (activeTarget_ == target && isDragging_);

            ImU32 col = (hovered || active) ? hoverColor : color;
            ImU32 bgCol = (hovered || active)
                ? IM_COL32(40, 40, 55, 240)
                : IM_COL32(25, 25, 35, 220);

            // Handle background
            drawList->AddRectFilled(min, max, bgCol, 4.f);
            drawList->AddRect(min, max, col, 4.f, 0, 1.5f);

            // Grip dots (left side)
            float gripX = x + 8.f;
            for (int i = 0; i < 3; ++i)
            {
                float dotY = y - 4.f + (float)i * 4.f;
                drawList->AddCircleFilled(ImVec2(gripX, dotY), 1.5f,
                    (hovered || active) ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 80));
            }

            // Price text
            char buf[48];
            if (std::abs(pct) > 0.01f)
                snprintf(buf, sizeof(buf), "%s %.2f (%+.1f%%)", prefix, price, pct);
            else
                snprintf(buf, sizeof(buf), "%s %.2f", prefix, price);

            ImVec2 textSz = ImGui::CalcTextSize(buf);
            float tx = x + 18.f;
            float ty = hy + (kHandleH - textSz.y) * 0.5f;
            drawList->AddText(ImVec2(tx, ty), col, buf);

            // Connecting line from handle to chart edge
            drawList->AddLine(ImVec2(x, y), ImVec2(vp.chartOrigin.x, y),
                (hovered || active) ? col : IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 40),
                (hovered || active) ? 1.5f : 0.5f);

            // Track hover for drag start
            if (hovered)
                activeTarget_ = target;

            return hovered || active;
        }

        // ── Button drawing ──────────────────────────────────────────────────

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

        // ── Helpers ─────────────────────────────────────────────────────────

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
