#include <Charts/StockChart.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace stnks
{
    StockChart::StockChart() = default;

    void StockChart::SetData(const StockQuote& quote)
    {
        data_ = quote;
        if (!data_.candles.empty())
        {
            viewport_.visibleCount = std::min(viewport_.visibleCount, (int)data_.candles.size());
            viewport_.visibleStart = std::max(0, (int)data_.candles.size() - viewport_.visibleCount);
        }
        AutoScalePrice();
    }

    void StockChart::ResetView()
    {
        viewport_.visibleCount = 60;
        if (!data_.candles.empty())
        {
            viewport_.visibleCount = std::min(viewport_.visibleCount, (int)data_.candles.size());
            viewport_.visibleStart = std::max(0, (int)data_.candles.size() - viewport_.visibleCount);
        }
        AutoScalePrice();
    }

    void StockChart::SetStrategies(const std::vector<Strategy>& strategies)
    {
        if (!strategyLayer_)
        {
            for (auto& layer : layers_)
            {
                auto* sl = dynamic_cast<StrategyLayer*>(layer.get());
                if (sl) { strategyLayer_ = sl; break; }
            }
        }

        if (strategyLayer_)
            strategyLayer_->strategies = strategies;
    }

    // ── Nice step helpers ──────────────────────────────────────────────────────

    float StockChart::NiceStep(float range, float targetLines)
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

    // ── Draw ───────────────────────────────────────────────────────────────────

    void StockChart::Draw(const char* label)
    {
        if (data_.candles.empty())
        {
            ImGui::TextDisabled("No data loaded");
            return;
        }

        // Header
        const auto& last = data_.candles.back();
        ImVec4 priceCol = last.IsBullish()
            ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
            : ImVec4(0.84f, 0.19f, 0.19f, 1.f);

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.f), "%s", data_.symbol.c_str());
        ImGui::SameLine();
        ImGui::TextColored(priceCol, "%.2f %s", last.close, data_.currency.c_str());
        if (!data_.exchange.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", data_.exchange.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
            ResetView();

        // Layout
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float totalSubHeight = 0.f;
        for (auto& layer : layers_)
            if (layer->visible && layer->height > 0.f)
                totalSubHeight += layer->height + 4.f;

        float mainChartHeight = avail.y - totalSubHeight - 20.f;
        if (mainChartHeight < 100.f) mainChartHeight = 100.f;

        chartWidth_ = avail.x - rightAxisWidth_;
        if (chartWidth_ < 100.f) chartWidth_ = 100.f;

        // Clamp visible count first — this is the single source of truth
        viewport_.visibleCount = std::clamp(viewport_.visibleCount,
            minVisibleCandles_, std::min(maxVisibleCandles_, (int)data_.candles.size()));

        int maxStart = std::max(0, (int)data_.candles.size() - viewport_.visibleCount);
        viewport_.visibleStart = std::clamp(viewport_.visibleStart, 0, maxStart);

        // Candle sizing derived from visibleCount (never feeds back)
        float stepPx = chartWidth_ / (float)viewport_.visibleCount;
        viewport_.candleWidth   = std::max(1.f, stepPx * 0.72f);
        viewport_.candleSpacing = std::max(0.5f, stepPx * 0.28f);

        AutoScalePrice();

        // Begin child
        ImGui::BeginChild(label, avail, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetCursorScreenPos();

        panelRegions_.clear();
        chartLeft_ = windowPos.x;

        // --- Main chart ---
        viewport_.chartOrigin = windowPos;
        viewport_.chartSize   = ImVec2(chartWidth_, mainChartHeight);
        totalChartTop_ = windowPos.y;

        panelRegions_.push_back({windowPos, ImVec2(chartWidth_, mainChartHeight), nullptr});

        drawList->AddRectFilled(viewport_.chartOrigin,
            ImVec2(viewport_.chartOrigin.x + chartWidth_, viewport_.chartOrigin.y + mainChartHeight),
            kBgColor);

        DrawGrid(drawList, viewport_);
        DrawPriceAxis(drawList, viewport_);

        ResolveFocusedCandle();

        for (auto& layer : layers_)
        {
            if (layer->visible && layer->height == 0.f)
                layer->Draw(drawList, viewport_, data_);
        }

        // --- Sub-panels ---
        float yOffset = windowPos.y + mainChartHeight;

        for (auto& layer : layers_)
        {
            if (!layer->visible || layer->height <= 0.f) continue;

            yOffset += 4.f;

            drawList->AddLine(
                ImVec2(windowPos.x, yOffset - 2.f),
                ImVec2(windowPos.x + chartWidth_, yOffset - 2.f),
                IM_COL32(60, 60, 70, 255), 1.0f);

            drawList->AddText(ImVec2(windowPos.x + 4, yOffset + 2),
                              IM_COL32(140, 140, 160, 200), layer->name.c_str());

            ChartViewport subVp = viewport_;
            subVp.chartOrigin = ImVec2(windowPos.x, yOffset);
            subVp.chartSize   = ImVec2(chartWidth_, layer->height);

            panelRegions_.push_back({subVp.chartOrigin, subVp.chartSize, layer.get()});

            drawList->AddRectFilled(subVp.chartOrigin,
                ImVec2(subVp.chartOrigin.x + chartWidth_, subVp.chartOrigin.y + layer->height),
                kBgColor);

            layer->Draw(drawList, subVp, data_);
            yOffset += layer->height;
        }

        totalChartBottom_ = yOffset;

        // --- Time axis ---
        ChartViewport timeVp = viewport_;
        timeVp.chartOrigin = ImVec2(windowPos.x, yOffset);
        timeVp.chartSize   = ImVec2(chartWidth_, 20.f);
        DrawTimeAxis(drawList, timeVp);

        DrawSyncedCrosshair(drawList);
        DrawTooltip();
        HandleInput();

        ImGui::EndChild();
    }

    // ── Input ──────────────────────────────────────────────────────────────────

    void StockChart::HandleInput()
    {
        if (!ImGui::IsWindowHovered()) return;

        ImGuiIO& io = ImGui::GetIO();

        if (io.MouseWheel != 0.f)
        {
            // Consume the wheel so parent windows don't also scroll
            ImGui::SetWindowFocus();

            if (io.KeyShift)
            {
                // Proportional zoom anchored to mouse X position
                float mouseRelX = (io.MousePos.x - chartLeft_) / chartWidth_;
                mouseRelX = std::clamp(mouseRelX, 0.f, 1.f);

                // The candle under the mouse cursor (fractional)
                float anchorCandle = (float)viewport_.visibleStart + mouseRelX * (float)viewport_.visibleCount;

                // Scale visible count proportionally
                float factor = (io.MouseWheel > 0.f) ? (1.f - zoomSpeed_) : (1.f + zoomSpeed_);
                int newCount = (int)std::round((float)viewport_.visibleCount * factor);
                newCount = std::clamp(newCount, minVisibleCandles_,
                                      std::min(maxVisibleCandles_, (int)data_.candles.size()));

                // Adjust start so the anchor candle stays under the mouse
                int newStart = (int)std::round(anchorCandle - mouseRelX * (float)newCount);
                int maxStart = std::max(0, (int)data_.candles.size() - newCount);
                newStart = std::clamp(newStart, 0, maxStart);

                viewport_.visibleCount = newCount;
                viewport_.visibleStart = newStart;
            }
            else
            {
                // Scroll speed proportional to visible range
                int scroll = std::max(1, viewport_.visibleCount / 20);
                if (io.MouseWheel > 0.f) scroll = -scroll;

                int maxStart = std::max(0, (int)data_.candles.size() - viewport_.visibleCount);
                viewport_.visibleStart = std::clamp(
                    viewport_.visibleStart + scroll, 0, maxStart);
            }

            // Clear the wheel delta so no parent window processes it
            io.MouseWheel = 0.f;
            io.MouseWheelH = 0.f;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            if (!isDragging_)
            {
                isDragging_     = true;
                dragStartX_     = io.MousePos.x;
                dragStartIndex_ = viewport_.visibleStart;
                dragCandleStep_ = viewport_.CandleStep();  // Cache step at drag start
            }

            float dx = io.MousePos.x - dragStartX_;
            int candleShift = -(int)(dx / dragCandleStep_);

            int maxStart = std::max(0, (int)data_.candles.size() - viewport_.visibleCount);
            viewport_.visibleStart = std::clamp(
                dragStartIndex_ + candleShift, 0, maxStart);
        }
        else
        {
            isDragging_ = false;
        }

        // Double-click middle mouse to reset view
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
            ResetView();

        // Right-click opens strategy context menu
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && strategyLayer_)
        {
            float clickPrice = viewport_.YToPrice(io.MousePos.y);
            if (io.MousePos.x >= chartLeft_ &&
                io.MousePos.x <= chartLeft_ + chartWidth_ &&
                io.MousePos.y >= totalChartTop_ &&
                io.MousePos.y <= viewport_.chartOrigin.y + viewport_.chartSize.y)
            {
                strategyLayer_->OpenContextMenu(clickPrice, data_.symbol);
            }
        }
    }

    // ── Grid ───────────────────────────────────────────────────────────────────

    void StockChart::DrawGrid(ImDrawList* drawList, const ChartViewport& vp)
    {
        float range = vp.priceMax - vp.priceMin;
        if (range <= 0.f) return;

        // TradingView-style: sparse grid, ~4-7 horizontal lines max
        float targetHLines = std::clamp(vp.chartSize.y / 100.f, 3.f, 7.f);
        float majorStep = NiceStep(range, targetHLines);

        float startMajor = std::ceil(vp.priceMin / majorStep) * majorStep;
        for (float p = startMajor; p <= vp.priceMax; p += majorStep)
        {
            float y = vp.PriceToY(p);
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x, y),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, y),
                kGridMajorColor, 1.0f);
        }

        // Vertical grid lines — sparse: ~4-6 lines max
        float targetVLines = std::clamp(chartWidth_ / 160.f, 3.f, 6.f);
        int vStep = std::max(1, (int)std::round((float)viewport_.visibleCount / targetVLines));

        // Snap to nice candle intervals
        int niceVStep = 1;
        int candidates[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
        for (int c : candidates)
        {
            if (c >= vStep) { niceVStep = c; break; }
            niceVStep = c;
        }

        int end = std::min(vp.visibleStart + vp.visibleCount, (int)data_.candles.size());
        int firstAligned = vp.visibleStart - (vp.visibleStart % niceVStep) + niceVStep;

        for (int i = firstAligned; i < end; i += niceVStep)
        {
            int ri = i - vp.visibleStart;
            float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
            drawList->AddLine(
                ImVec2(x, vp.chartOrigin.y),
                ImVec2(x, vp.chartOrigin.y + vp.chartSize.y),
                kGridMajorColor, 1.0f);
        }
    }

    // ── Price axis ─────────────────────────────────────────────────────────────

    void StockChart::DrawPriceAxis(ImDrawList* drawList, const ChartViewport& vp)
    {
        float range = vp.priceMax - vp.priceMin;
        if (range <= 0.f) return;

        // Match grid density
        float targetLines = std::clamp(vp.chartSize.y / 100.f, 3.f, 7.f);
        float majorStep = NiceStep(range, targetLines);

        // Determine decimal places from step size
        int decimals = 2;
        if (majorStep >= 10.f)       decimals = 0;
        else if (majorStep >= 1.f)   decimals = 1;
        else if (majorStep >= 0.1f)  decimals = 2;
        else if (majorStep >= 0.01f) decimals = 3;
        else                         decimals = 4;

        char fmt[8];
        snprintf(fmt, sizeof(fmt), "%%.%df", decimals);

        float startPrice = std::ceil(vp.priceMin / majorStep) * majorStep;
        float axisX = vp.chartOrigin.x + vp.chartSize.x + 4.f;

        for (float p = startPrice; p <= vp.priceMax; p += majorStep)
        {
            float y = vp.PriceToY(p);

            // Skip labels too close to top/bottom edges
            if (y < vp.chartOrigin.y + 6.f || y > vp.chartOrigin.y + vp.chartSize.y - 6.f)
                continue;

            char buf[16];
            snprintf(buf, sizeof(buf), fmt, p);
            drawList->AddText(ImVec2(axisX, y - 6.f), kAxisTextColor, buf);
        }
    }

    // ── Time axis ──────────────────────────────────────────────────────────────

    void StockChart::DrawTimeAxis(ImDrawList* drawList, const ChartViewport& vp)
    {
        // Match vertical grid density — sparse labels
        float targetLabels = std::clamp(chartWidth_ / 160.f, 3.f, 6.f);
        int labelStep = std::max(1, (int)std::round((float)viewport_.visibleCount / targetLabels));

        // Snap to nice intervals
        int niceStep = 1;
        int candidates[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
        for (int c : candidates)
        {
            if (c >= labelStep) { niceStep = c; break; }
            niceStep = c;
        }

        int end = std::min(vp.visibleStart + vp.visibleCount, (int)data_.candles.size());
        int firstAligned = vp.visibleStart - (vp.visibleStart % niceStep) + niceStep;

        // Choose date format based on data granularity and zoom level
        bool intraday = data_.interval.find('m') != std::string::npos ||
                        data_.interval.find('h') != std::string::npos;
        const char* dateFmt;
        if (intraday)
            dateFmt = "%H:%M";
        else if (viewport_.visibleCount <= 30)
            dateFmt = "%b %d";
        else
            dateFmt = "%m/%d";

        for (int i = firstAligned; i < end; i += niceStep)
        {
            int ri = i - vp.visibleStart;
            float x = vp.IndexToX(ri) + viewport_.candleWidth * 0.5f;

            time_t ts = (time_t)data_.candles[i].timestamp;
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &ts);
#else
            localtime_r(&ts, &tm_buf);
#endif
            char buf[16];
            strftime(buf, sizeof(buf), dateFmt, &tm_buf);

            ImVec2 textSize = ImGui::CalcTextSize(buf);
            drawList->AddText(ImVec2(x - textSize.x * 0.5f, vp.chartOrigin.y + 2.f),
                              kAxisTextColor, buf);
        }

        // Focused candle date highlight
        if (viewport_.focusedCandle >= 0 && ImGui::IsWindowHovered())
        {
            float focusX = viewport_.FocusedX();
            time_t ts = (time_t)data_.candles[viewport_.focusedCandle].timestamp;
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &ts);
#else
            localtime_r(&ts, &tm_buf);
#endif
            bool intradayFocus = data_.interval.find('m') != std::string::npos ||
                                 data_.interval.find('h') != std::string::npos;
            char buf[32];
            strftime(buf, sizeof(buf), intradayFocus ? "%b %d %H:%M" : "%b %d", &tm_buf);
            ImVec2 textSize = ImGui::CalcTextSize(buf);

            float px = focusX - textSize.x * 0.5f - 4.f;
            float py = vp.chartOrigin.y + 1.f;
            drawList->AddRectFilled(
                ImVec2(px, py),
                ImVec2(px + textSize.x + 8.f, py + textSize.y + 2.f),
                IM_COL32(60, 60, 80, 220), 3.f);
            drawList->AddText(ImVec2(px + 4.f, py + 1.f),
                              IM_COL32(255, 255, 255, 255), buf);
        }
    }

    // ── Focus / Crosshair / Tooltip ────────────────────────────────────────────

    void StockChart::ResolveFocusedCandle()
    {
        viewport_.focusedCandle = -1;
        if (!ImGui::IsWindowHovered()) return;

        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x < chartLeft_ || mouse.x > chartLeft_ + chartWidth_) return;
        if (mouse.y < totalChartTop_ || mouse.y > totalChartBottom_) return;

        float step = viewport_.CandleStep();
        int candleIdx = (int)((mouse.x - chartLeft_) / step) + viewport_.visibleStart;

        if (candleIdx >= 0 && candleIdx < (int)data_.candles.size())
            viewport_.focusedCandle = candleIdx;
    }

    void StockChart::DrawSyncedCrosshair(ImDrawList* drawList)
    {
        if (viewport_.focusedCandle < 0) return;
        if (!ImGui::IsWindowHovered()) return;

        float focusX = viewport_.FocusedX();
        if (focusX < 0.f) return;

        ImVec2 mouse = ImGui::GetMousePos();

        drawList->AddLine(
            ImVec2(focusX, totalChartTop_),
            ImVec2(focusX, totalChartBottom_),
            kCrosshairColor, 1.0f);

        for (auto& region : panelRegions_)
        {
            if (mouse.y >= region.origin.y &&
                mouse.y <= region.origin.y + region.size.y)
            {
                drawList->AddLine(
                    ImVec2(region.origin.x, mouse.y),
                    ImVec2(region.origin.x + region.size.x, mouse.y),
                    kCrosshairColor, 1.0f);

                if (region.layer == nullptr)
                {
                    float range = viewport_.priceMax - viewport_.priceMin;
                    if (range > 0.f)
                    {
                        float t = 1.f - (mouse.y - region.origin.y) / region.size.y;
                        float price = viewport_.priceMin + t * range;

                        char buf[16];
                        snprintf(buf, sizeof(buf), "%.2f", price);

                        float axisX = region.origin.x + region.size.x + 2.f;
                        drawList->AddRectFilled(
                            ImVec2(axisX, mouse.y - 8.f),
                            ImVec2(axisX + rightAxisWidth_ - 4.f, mouse.y + 8.f),
                            IM_COL32(60, 60, 80, 220));
                        drawList->AddText(ImVec2(axisX + 2.f, mouse.y - 6.f),
                                          IM_COL32(255, 255, 255, 255), buf);
                    }
                }
                break;
            }
        }

        // --- Fancy close-price badge on the price axis ---
        const auto& fc = data_.candles[viewport_.focusedCandle];
        bool bullish = fc.IsBullish();
        ImU32 candleCol = bullish ? IM_COL32(38, 166, 91, 255)   // green
                                  : IM_COL32(214, 48, 48, 255);  // red
        ImU32 candleColDim = bullish ? IM_COL32(38, 166, 91, 60)
                                     : IM_COL32(214, 48, 48, 60);

        float closeY = viewport_.PriceToY(fc.close);

        // Only draw if close is within the main chart panel
        if (closeY >= viewport_.chartOrigin.y &&
            closeY <= viewport_.chartOrigin.y + viewport_.chartSize.y)
        {
            // Dotted horizontal line from candle to axis
            float lineX0 = focusX;
            float lineX1 = viewport_.chartOrigin.x + viewport_.chartSize.x;
            for (float x = lineX0; x < lineX1; x += 6.f)
            {
                float segEnd = std::min(x + 3.f, lineX1);
                drawList->AddLine(ImVec2(x, closeY), ImVec2(segEnd, closeY),
                                  candleColDim, 1.0f);
            }

            // Price badge with rounded rect + triangle pointer
            float axisX = viewport_.chartOrigin.x + viewport_.chartSize.x + 2.f;
            float badgeW = rightAxisWidth_ - 4.f;
            float badgeH = 18.f;
            float by = closeY - badgeH * 0.5f;

            // Triangle pointer on the left edge
            drawList->AddTriangleFilled(
                ImVec2(axisX - 5.f, closeY),
                ImVec2(axisX, closeY - 5.f),
                ImVec2(axisX, closeY + 5.f),
                candleCol);

            // Badge background
            drawList->AddRectFilled(
                ImVec2(axisX, by),
                ImVec2(axisX + badgeW, by + badgeH),
                candleCol, 3.f);

            // Badge border glow
            drawList->AddRect(
                ImVec2(axisX, by),
                ImVec2(axisX + badgeW, by + badgeH),
                bullish ? IM_COL32(100, 220, 140, 120) : IM_COL32(255, 100, 100, 120),
                3.f, 0, 1.5f);

            // Price text
            char priceBuf[16];
            snprintf(priceBuf, sizeof(priceBuf), "%.2f", fc.close);
            ImVec2 textSz = ImGui::CalcTextSize(priceBuf);
            drawList->AddText(
                ImVec2(axisX + (badgeW - textSz.x) * 0.5f, by + (badgeH - textSz.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), priceBuf);
        }
    }

    void StockChart::DrawTooltip()
    {
        if (viewport_.focusedCandle < 0) return;
        if (!ImGui::IsWindowHovered()) return;

        const auto& c = data_.candles[viewport_.focusedCandle];

        time_t ts = (time_t)c.timestamp;
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &ts);
#else
        localtime_r(&ts, &tm_buf);
#endif
        bool intradayTip = data_.interval.find('m') != std::string::npos ||
                           data_.interval.find('h') != std::string::npos;
        char dateBuf[32];
        strftime(dateBuf, sizeof(dateBuf), intradayTip ? "%Y-%m-%d %H:%M" : "%Y-%m-%d", &tm_buf);

        ImGui::BeginTooltip();
        ImGui::Text("%s", dateBuf);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.f), "O:");
        ImGui::SameLine(); ImGui::Text("%.2f", c.open);
        ImGui::SameLine(); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.f), "  H:");
        ImGui::SameLine(); ImGui::Text("%.2f", c.high);

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.f), "L:");
        ImGui::SameLine(); ImGui::Text("%.2f", c.low);
        ImGui::SameLine(); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.f), "  C:");
        ImGui::SameLine();
        ImVec4 closeCol = c.IsBullish()
            ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
            : ImVec4(0.84f, 0.19f, 0.19f, 1.f);
        ImGui::TextColored(closeCol, "%.2f", c.close);

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.f), "Vol:");
        ImGui::SameLine(); ImGui::Text("%.0f", c.volume);
        ImGui::EndTooltip();
    }

    void StockChart::AutoScalePrice()
    {
        if (data_.candles.empty()) return;

        int end = std::min(viewport_.visibleStart + viewport_.visibleCount,
                           (int)data_.candles.size());

        float lo = 1e18f, hi = -1e18f;
        for (int i = viewport_.visibleStart; i < end; ++i)
        {
            lo = std::min(lo, data_.candles[i].low);
            hi = std::max(hi, data_.candles[i].high);
        }

        float range = hi - lo;
        float margin = range * priceMarginPct_;
        viewport_.priceMin = lo - margin;
        viewport_.priceMax = hi + margin;
    }

} // namespace stnks
