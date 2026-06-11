#include <Charts/StockChart.hpp>
#include <Market/MarketHours.hpp>
#include <algorithm>
#include <unordered_map>
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
        yLocked_ = false;
        AutoScalePrice();
    }

    // ── Layer tree helpers ────────────────────────────────────────────────────

    void StockChart::RebuildDefaultTree()
    {
        layerTree_.clear();
        for (int i = 0; i < (int)layers_.size(); ++i)
        {
            if (layers_[i]->name == "Strategies") continue;
            layerTree_.push_back({i, {}});
        }
    }

    bool StockChart::IsRootLayer(int layerIdx) const
    {
        for (auto& node : layerTree_)
            if (node.layerIdx == layerIdx) return true;
        return false;
    }

    int StockChart::FindParentRoot(int layerIdx) const
    {
        for (int r = 0; r < (int)layerTree_.size(); ++r)
        {
            if (layerTree_[r].layerIdx == layerIdx) return -1; // It IS a root
            for (int c : layerTree_[r].children)
                if (c == layerIdx) return r;
        }
        return -1;
    }

    void StockChart::DetachFromTree(int layerIdx)
    {
        for (auto& node : layerTree_)
        {
            // Remove from children
            auto& ch = node.children;
            ch.erase(std::remove(ch.begin(), ch.end(), layerIdx), ch.end());
        }
        // Remove as root
        layerTree_.erase(
            std::remove_if(layerTree_.begin(), layerTree_.end(),
                           [&](const LayerNode& n) { return n.layerIdx == layerIdx; }),
            layerTree_.end());
    }

    void StockChart::ApplyLayerTree()
    {
        // Save original heights once
        if (!heightsSaved_)
        {
            heightsSaved_ = true;
            for (auto& layer : layers_)
                originalHeights_.push_back({layer->name, layer->height});
        }

        // Build default tree if needed
        if (layerTree_.empty())
            RebuildDefaultTree();

        // Assign heights: first visible root = main (height 0), rest = sub-panels
        bool mainAssigned = false;
        for (auto& node : layerTree_)
        {
            if (node.layerIdx < 0 || node.layerIdx >= (int)layers_.size()) continue;
            auto& layer = layers_[node.layerIdx];

            // Check if this root or any of its children are visible
            bool anyVisible = layer->visible;
            for (int ci : node.children)
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible)
                    anyVisible = true;

            if (!anyVisible) continue;

            if (!mainAssigned)
            {
                layer->height = 0.f;
                mainAssigned = true;
            }
            else
            {
                // Restore original sub-panel height
                float h = 100.f;
                for (auto& orig : originalHeights_)
                    if (orig.name == layer->name) { h = (orig.height > 0.f) ? orig.height : 100.f; break; }
                layer->height = h;
            }
        }

        layerTreeDirty_ = false;
    }

    // ── Indicator panel (tree view with drag-drop) ────────────────────────

    void StockChart::DrawIndicatorCombo()
    {
        if (layerTree_.empty())
            RebuildDefaultTree();

        // Count visible layers
        int visibleCount = 0;
        for (auto& node : layerTree_)
        {
            if (node.layerIdx >= 0 && node.layerIdx < (int)layers_.size() && layers_[node.layerIdx]->visible)
                visibleCount++;
            for (int ci : node.children)
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible)
                    visibleCount++;
        }

        // Find the first visible root (= main panel)
        int mainRootIdx = -1;
        for (int r = 0; r < (int)layerTree_.size(); ++r)
        {
            auto& node = layerTree_[r];
            bool anyVis = (node.layerIdx >= 0 && node.layerIdx < (int)layers_.size() && layers_[node.layerIdx]->visible);
            for (int ci : node.children)
                if (!anyVis && ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible)
                    anyVis = true;
            if (anyVis) { mainRootIdx = r; break; }
        }

        // Button opens a popup (not a combo — combos close on drag)
        char label[32];
        snprintf(label, sizeof(label), "Graphs (%d)", visibleCount);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        if (ImGui::SmallButton(label))
            ImGui::OpenPopup("##LayerTreePopup");
        ImGui::PopStyleVar();

        if (!ImGui::BeginPopup("##LayerTreePopup"))
            return;

        ImGui::TextDisabled("Drag onto = merge | x = unmerge");
        ImGui::Separator();

        // Snapshot the tree size before iterating — mutations defer to next frame
        // by breaking out of the loop immediately after any structural change.
        const int treeSize = (int)layerTree_.size();
        bool treeChanged = false;
        bool mutated = false;  // True = tree structure changed, stop drawing

        for (int r = 0; r < treeSize && r < (int)layerTree_.size() && !mutated; ++r)
        {
            int rootLayerIdx = layerTree_[r].layerIdx;
            if (rootLayerIdx < 0 || rootLayerIdx >= (int)layers_.size()) continue;
            auto* rootLayer = layers_[rootLayerIdx].get();
            bool isMain = (r == mainRootIdx);

            // Use the layer index as a stable ID (won't collide)
            ImGui::PushID(rootLayerIdx);

            if (isMain)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.5f, 1.f));

            // Checkbox
            bool checked = rootLayer->visible;
            if (ImGui::Checkbox("##vis", &checked))
            {
                rootLayer->visible = checked;
                treeChanged = true;
            }
            ImGui::SameLine();

            // Selectable (drag source + drop target)
            ImGui::Selectable(rootLayer->name.c_str(), false, 0, ImVec2(110, 0));

            // Drag source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload("LAYER_DND", &rootLayerIdx, sizeof(int));
                ImGui::Text("Move %s", rootLayer->name.c_str());
                ImGui::EndDragDropSource();
            }

            // Drop target: merge dragged layer into this root
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DND"))
                {
                    int srcIdx = *(const int*)payload->Data;
                    if (srcIdx != rootLayerIdx)
                    {
                        DetachFromTree(srcIdx);
                        // After detach, find our root again (index may have shifted)
                        for (int rr = 0; rr < (int)layerTree_.size(); ++rr)
                        {
                            if (layerTree_[rr].layerIdx == rootLayerIdx)
                            {
                                layerTree_[rr].children.push_back(srcIdx);
                                break;
                            }
                        }
                        treeChanged = true;
                        mutated = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Role label
            ImGui::SameLine(130.f);
            if (isMain)
                ImGui::TextDisabled("[main]");
            else
                ImGui::TextDisabled(rootLayer->visible ? "[sub]" : "[off]");

            // Move up/down buttons
            ImGui::SameLine(175.f);
            {
                bool canUp = (r > 0);
                bool canDown = (r < (int)layerTree_.size() - 1);
                if (!canUp) ImGui::BeginDisabled();
                if (ImGui::SmallButton("^") && canUp)
                {
                    std::swap(layerTree_[r], layerTree_[r - 1]);
                    treeChanged = true;
                    mutated = true;
                }
                if (!canUp) ImGui::EndDisabled();

                ImGui::SameLine();

                if (!canDown) ImGui::BeginDisabled();
                if (ImGui::SmallButton("v") && canDown)
                {
                    std::swap(layerTree_[r], layerTree_[r + 1]);
                    treeChanged = true;
                    mutated = true;
                }
                if (!canDown) ImGui::EndDisabled();
            }

            if (isMain)
                ImGui::PopStyleColor();

            // Children (merged layers)
            if (!mutated)
            {
                // Re-read children count from tree (safe since no mutation yet)
                int numChildren = (int)layerTree_[r].children.size();
                for (int ci = 0; ci < numChildren && !mutated; ++ci)
                {
                    int childIdx = layerTree_[r].children[ci];
                    if (childIdx < 0 || childIdx >= (int)layers_.size()) continue;
                    auto* childLayer = layers_[childIdx].get();

                    ImGui::PushID(childIdx + 10000);  // Offset avoids collision with root IDs
                    ImGui::Indent(20.f);

                    bool cVis = childLayer->visible;
                    if (ImGui::Checkbox("##cv", &cVis))
                    {
                        childLayer->visible = cVis;
                        treeChanged = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(childLayer->name.c_str());

                    // Child drag source
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        ImGui::SetDragDropPayload("LAYER_DND", &childIdx, sizeof(int));
                        ImGui::Text("Move %s", childLayer->name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("[merged]");

                    // Unmerge button
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x"))
                    {
                        layerTree_[r].children.erase(layerTree_[r].children.begin() + ci);
                        LayerNode newRoot;
                        newRoot.layerIdx = childIdx;
                        layerTree_.insert(layerTree_.begin() + r + 1, newRoot);
                        treeChanged = true;
                        mutated = true;
                    }

                    ImGui::Unindent(20.f);
                    ImGui::PopID();
                }
            }

            ImGui::PopID();  // rootLayerIdx
        }

        // Drop zone at bottom
        ImGui::Separator();
        ImGui::Selectable("Drop here = new panel", false, 0, ImVec2(0, 20));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_DND"))
            {
                int srcIdx = *(const int*)payload->Data;
                DetachFromTree(srcIdx);
                layerTree_.push_back({srcIdx, {}});
                treeChanged = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (treeChanged)
            layerTreeDirty_ = true;

        ImGui::EndPopup();
    }

    void StockChart::ScrollToCandle(int candleIdx)
    {
        if (data_.candles.empty()) return;
        int total = (int)data_.candles.size();
        candleIdx = std::clamp(candleIdx, 0, total - 1);

        // Center the candle in the visible window
        viewport_.visibleStart = std::clamp(
            candleIdx - viewport_.visibleCount / 2,
            0, std::max(0, total - viewport_.visibleCount));

        highlightCandleIdx_ = candleIdx;
        highlightTimer_ = 2.0f; // 2 second highlight
        yLocked_ = false;
        AutoScalePrice();
    }

    void StockChart::FocusOnPriceRange(float priceLo, float priceHi)
    {
        if (priceLo >= priceHi) return;
        float range = priceHi - priceLo;
        float margin = range * 0.15f; // 15% margin for comfortable viewing
        viewport_.priceMin = priceLo - margin;
        viewport_.priceMax = priceHi + margin;
        yLocked_ = true; // Prevent AutoScalePrice from overriding
    }

    void StockChart::SetEventMarkers(const std::vector<GraphEvent>& events)
    {
        eventMarkers_ = events;
    }

    void StockChart::DrawEventMarkers(ImDrawList* drawList, const ChartViewport& vp)
    {
        if (eventMarkers_.empty() && highlightCandleIdx_ < 0) return;

        int end = std::min(vp.visibleStart + vp.visibleCount, (int)data_.candles.size());

        // Draw highlight ring on focused event candle
        if (highlightCandleIdx_ >= vp.visibleStart && highlightCandleIdx_ < end && highlightTimer_ > 0.f)
        {
            float alpha = std::min(1.f, highlightTimer_);
            int ri = highlightCandleIdx_ - vp.visibleStart;
            float xMid = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
            const auto& c = data_.candles[highlightCandleIdx_];
            float yMid = vp.PriceToY((c.high + c.low) * 0.5f);

            ImU32 ringCol = IM_COL32(255, 220, 50, (int)(200 * alpha));
            drawList->AddCircle(ImVec2(xMid, yMid), 12.f, ringCol, 0, 2.5f);
            drawList->AddCircle(ImVec2(xMid, yMid), 16.f, IM_COL32(255, 220, 50, (int)(80 * alpha)), 0, 1.5f);

            highlightTimer_ -= ImGui::GetIO().DeltaTime;
        }

        // Group events by candle index for stacking
        struct CandleGroup
        {
            int candleIdx = -1;
            EventSeverity maxSeverity = EventSeverity::Info;
            std::vector<const GraphEvent*> events;
        };
        std::unordered_map<int, CandleGroup> grouped;

        for (auto& ev : eventMarkers_)
        {
            if (ev.candleIdx < vp.visibleStart || ev.candleIdx >= end) continue;
            if (ev.candleIdx < 0 || ev.candleIdx >= (int)data_.candles.size()) continue;

            auto& g = grouped[ev.candleIdx];
            g.candleIdx = ev.candleIdx;
            if ((int)ev.severity > (int)g.maxSeverity) g.maxSeverity = ev.severity;
            g.events.push_back(&ev);
        }

        for (auto& [idx, group] : grouped)
        {
            int ri = idx - vp.visibleStart;
            float xMid = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
            const auto& candle = data_.candles[idx];

            // Position based on highest severity in group
            float yPos;
            ImU32 dotColor;
            switch (group.maxSeverity)
            {
                case EventSeverity::Alert:
                    yPos = vp.PriceToY(candle.high) - 8.f;
                    dotColor = IM_COL32(255, 70, 70, 220);
                    break;
                case EventSeverity::Warning:
                    yPos = vp.PriceToY(candle.high) - 8.f;
                    dotColor = IM_COL32(255, 200, 50, 220);
                    break;
                default:
                    yPos = vp.PriceToY(candle.low) + 8.f;
                    dotColor = IM_COL32(100, 160, 255, 180);
                    break;
            }

            yPos = std::clamp(yPos, vp.chartOrigin.y + 4.f,
                              vp.chartOrigin.y + vp.chartSize.y - 4.f);

            int count = (int)group.events.size();
            float radius = count > 1 ? 5.f : 3.f;
            drawList->AddCircleFilled(ImVec2(xMid, yPos), radius, dotColor);

            // Count badge for stacked events (2+)
            if (count > 1)
            {
                char badge[8];
                snprintf(badge, sizeof(badge), "%d", count);
                ImVec2 textSize = ImGui::CalcTextSize(badge);
                float badgeX = xMid - textSize.x * 0.5f;
                float badgeY = yPos - radius - textSize.y - 1.f;
                badgeY = std::max(badgeY, vp.chartOrigin.y);
                drawList->AddText(ImVec2(badgeX, badgeY),
                    IM_COL32(255, 255, 255, 200), badge);
            }

            // Tooltip on hover — shows all events in group
            ImVec2 mousePos = ImGui::GetMousePos();
            float dx = mousePos.x - xMid;
            float dy = mousePos.y - yPos;
            if (dx * dx + dy * dy < (radius + 5.f) * (radius + 5.f))
            {
                ImGui::BeginTooltip();
                if (count > 1)
                    ImGui::TextDisabled("%d events on this candle:", count);
                for (auto* ev : group.events)
                {
                    ImGui::TextColored(
                        ev->severity == EventSeverity::Alert   ? ImVec4(1,0.3f,0.3f,1) :
                        ev->severity == EventSeverity::Warning ? ImVec4(1,0.8f,0.2f,1) :
                                                                  ImVec4(0.5f,0.7f,1,1),
                        "%s", ev->title.c_str());
                    if (!ev->detail.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("- %s", ev->detail.c_str());
                    }
                }
                ImGui::EndTooltip();
            }
        }
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

        // Apply layer tree if changed or not yet built
        if (layerTreeDirty_ || layerTree_.empty())
            ApplyLayerTree();

        // Header
        const auto& last = data_.candles.back();
        ImVec4 priceCol = last.IsBullish()
            ? ImVec4(0.15f, 0.65f, 0.36f, 1.f)
            : ImVec4(0.84f, 0.19f, 0.19f, 1.f);

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.f), "%s", data_.symbol.c_str());
        ImGui::SameLine();
        { char pb[32]; FmtPrice(pb, sizeof(pb), last.close, data_.symbol);
        ImGui::TextColored(priceCol, "%s %s", pb, data_.currency.c_str()); }
        if (!data_.exchange.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", data_.exchange.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
            ResetView();
        ImGui::SameLine();
        DrawIndicatorCombo();
        if (onDuplicateChart)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Detach"))
                onDuplicateChart();
        }

        // Layout — compute sub-panel heights from tree roots
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float totalSubHeight = 0.f;
        for (auto& node : layerTree_)
        {
            if (node.layerIdx < 0 || node.layerIdx >= (int)layers_.size()) continue;
            auto& rootLayer = layers_[node.layerIdx];
            if (rootLayer->height > 0.f)
            {
                // Check if root or any child is visible
                bool anyVis = rootLayer->visible;
                for (int ci : node.children)
                    if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible) anyVis = true;
                if (anyVis)
                    totalSubHeight += rootLayer->height + 4.f;
            }
        }

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

        if (!yLocked_)
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

        panelRegions_.push_back({windowPos, ImVec2(chartWidth_, mainChartHeight), nullptr,
                                 viewport_.priceMin, viewport_.priceMax, true});

        // Fancy grid background for main chart
        ChartGrid::DrawFull(drawList, viewport_,
                            viewport_.visibleStart, viewport_.visibleCount,
                            (int)data_.candles.size(), chartWidth_,
                            viewport_.priceMin, viewport_.priceMax);

        DrawPriceAxis(drawList, viewport_);

        ResolveFocusedCandle();

        // Draw main panel layers (root + merged children with height == 0)
        // Check if the main panel contains a price-based layer (Candlestick)
        bool mainHasPriceLayer = false;
        for (auto& node : layerTree_)
        {
            if (node.layerIdx < 0 || node.layerIdx >= (int)layers_.size()) continue;
            auto& rootLayer = layers_[node.layerIdx];
            if (rootLayer->height != 0.f) continue;

            if (rootLayer->name == "Candlestick") mainHasPriceLayer = true;
            for (int ci : node.children)
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->name == "Candlestick")
                    mainHasPriceLayer = true;

            if (rootLayer->visible)
                rootLayer->Draw(drawList, viewport_, data_);
            for (int ci : node.children)
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible)
                    layers_[ci]->Draw(drawList, viewport_, data_);
        }

        // Draw Strategies and event markers in the main area only if it has a price-based layer
        if (mainHasPriceLayer)
        {
            for (auto& layer : layers_)
                if (layer->name == "Strategies" && layer->visible)
                    layer->Draw(drawList, viewport_, data_);
            DrawEventMarkers(drawList, viewport_);
        }

        // --- Sub-panels with draggable dividers (tree-based) ---
        float yOffset = windowPos.y + mainChartHeight;
        int dividerIdx = 0;

        for (auto& node : layerTree_)
        {
            if (node.layerIdx < 0 || node.layerIdx >= (int)layers_.size()) continue;
            auto& rootLayer = layers_[node.layerIdx];
            if (rootLayer->height <= 0.f) continue; // Skip main panel root

            // Check if root or any child is visible
            bool anyVis = rootLayer->visible;
            for (int ci : node.children)
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible) anyVis = true;
            if (!anyVis) continue;

            // Divider hit area (6px tall, centered on the separator line)
            float dividerY = yOffset;
            ImVec2 divMin(windowPos.x, dividerY - 3.f);
            ImVec2 divMax(windowPos.x + chartWidth_, dividerY + 3.f);

            bool divHovered = ImGui::IsMouseHoveringRect(divMin, divMax);
            bool divDragging = (dividerDragIdx_ == dividerIdx);

            if (divHovered || divDragging)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            if (divHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                dividerDragIdx_ = dividerIdx;
                dividerDragLayer_ = rootLayer.get();
            }

            if (divDragging)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    float delta = ImGui::GetIO().MouseDelta.y;
                    rootLayer->height = std::max(30.f, rootLayer->height - delta);
                }
                else
                {
                    dividerDragIdx_ = -1;
                    dividerDragLayer_ = nullptr;
                }
            }

            // Draw divider line
            ImU32 divColor = (divHovered || divDragging)
                ? IM_COL32(100, 120, 180, 255)
                : IM_COL32(60, 60, 70, 255);
            drawList->AddLine(
                ImVec2(windowPos.x, dividerY),
                ImVec2(windowPos.x + chartWidth_, dividerY),
                divColor, (divHovered || divDragging) ? 2.0f : 1.0f);

            yOffset += 4.f;

            // Build panel label: "Root + Child1 + Child2"
            std::string panelLabel = rootLayer->name;
            for (int ci : node.children)
            {
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible)
                    panelLabel += " + " + layers_[ci]->name;
            }
            drawList->AddText(ImVec2(windowPos.x + 4, yOffset + 2),
                              IM_COL32(140, 140, 160, 200), panelLabel.c_str());

            // "Tear out" button
            if (onIndicatorTearOut)
            {
                ImVec2 nameSize = ImGui::CalcTextSize(panelLabel.c_str());
                float btnX = windowPos.x + 8.f + nameSize.x;
                float btnY = yOffset + 1.f;
                float btnW = 14.f, btnH = 14.f;
                ImVec2 btnMin(btnX, btnY);
                ImVec2 btnMax(btnX + btnW, btnY + btnH);

                bool btnHovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);
                ImU32 btnCol = btnHovered ? IM_COL32(100, 140, 220, 255) : IM_COL32(80, 90, 120, 160);

                drawList->AddRect(btnMin, btnMax, btnCol, 2.f, 0, 1.2f);
                drawList->AddLine(ImVec2(btnX + 4, btnY + btnH - 4), ImVec2(btnX + btnW - 3, btnY + 3), btnCol, 1.5f);
                drawList->AddLine(ImVec2(btnX + btnW - 3, btnY + 3), ImVec2(btnX + btnW - 7, btnY + 3), btnCol, 1.5f);
                drawList->AddLine(ImVec2(btnX + btnW - 3, btnY + 3), ImVec2(btnX + btnW - 3, btnY + 7), btnCol, 1.5f);

                if (btnHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    onIndicatorTearOut(rootLayer->name);

                if (btnHovered)
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Detach %s to its own window", panelLabel.c_str());
                    ImGui::EndTooltip();
                }
            }

            ChartViewport subVp = viewport_;
            subVp.chartOrigin = ImVec2(windowPos.x, yOffset);
            subVp.chartSize   = ImVec2(chartWidth_, rootLayer->height);

            // Get value range from root layer (primary axis for the panel)
            float yMin = 0.f, yMax = 1.f;
            bool hasRange = rootLayer->GetValueRange(subVp, data_, yMin, yMax);

            if (hasRange)
            {
                subVp.priceMin = yMin;
                subVp.priceMax = yMax;
            }

            panelRegions_.push_back({subVp.chartOrigin, subVp.chartSize, rootLayer.get(),
                                     yMin, yMax, hasRange});

            // Grid
            if (hasRange)
            {
                ChartGrid::DrawFull(drawList, subVp,
                                    viewport_.visibleStart, viewport_.visibleCount,
                                    (int)data_.candles.size(), chartWidth_,
                                    yMin, yMax);
                DrawValueAxis(drawList, subVp, yMin, yMax);
            }
            else
            {
                ChartGrid::DrawBackground(drawList, subVp.chartOrigin, subVp.chartSize);
                ChartGrid::DrawVerticalGrid(drawList, subVp,
                                            viewport_.visibleStart, viewport_.visibleCount,
                                            (int)data_.candles.size(), chartWidth_);
            }

            // Draw root layer
            if (rootLayer->visible)
                rootLayer->Draw(drawList, subVp, data_);

            // Draw merged children in the same viewport
            for (int ci : node.children)
            {
                if (ci < 0 || ci >= (int)layers_.size()) continue;
                if (!layers_[ci]->visible) continue;
                layers_[ci]->Draw(drawList, subVp, data_);
            }

            // If this sub-panel contains the Candlestick layer, draw strategies here
            // (they need a price-based viewport, not the main RSI/MACD viewport)
            if (!mainHasPriceLayer)
            {
                bool panelHasPrice = (rootLayer->name == "Candlestick");
                for (int ci : node.children)
                    if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->name == "Candlestick")
                        panelHasPrice = true;

                if (panelHasPrice)
                {
                    // Use the sub-panel viewport with proper price scaling
                    // (subVp already has price range from Candlestick's GetValueRange
                    //  or we compute it from candle data)
                    ChartViewport priceVp = subVp;
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
                    priceVp.priceMin = lo - margin;
                    priceVp.priceMax = hi + margin;

                    for (auto& layer : layers_)
                        if (layer->name == "Strategies" && layer->visible)
                            layer->Draw(drawList, priceVp, data_);
                    DrawEventMarkers(drawList, priceVp);
                    DrawPriceAxis(drawList, priceVp);
                }
            }

            yOffset += rootLayer->height;
            dividerIdx++;
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
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) return;

        ImGuiIO& io = ImGui::GetIO();

        if (io.MouseWheel != 0.f)
        {
            // Consume the wheel so parent windows don't also scroll
            ImGui::SetWindowFocus();

            if (io.KeyCtrl)
            {
                // Ctrl+scroll: Y-axis scroll (pan price range up/down)
                float priceRange = viewport_.priceMax - viewport_.priceMin;
                float scrollAmount = priceRange * 0.05f;  // 5% of visible range per tick
                if (io.MouseWheel > 0.f) scrollAmount = -scrollAmount;  // scroll up = higher prices

                viewport_.priceMin += scrollAmount;
                viewport_.priceMax += scrollAmount;
                yLocked_ = true;
            }
            else if (io.KeyShift)
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

        // ── Horizontal (price) grid ──────────────────────────────────────────

        // Adaptive target: more lines as chart is taller, but clamped
        float targetHLines = std::clamp(vp.chartSize.y / 80.f, 4.f, 10.f);
        float majorStep = NiceStep(range, targetHLines);

        // Minimum pixel spacing between grid lines to prevent infinite density
        constexpr float kMinGridPixelSpacing = 20.f;
        float pixelsPerUnit = vp.chartSize.y / range;

        // Clamp: if majorStep would produce lines closer than kMinGridPixelSpacing, increase it
        while (majorStep * pixelsPerUnit < kMinGridPixelSpacing && majorStep < range)
            majorStep *= 2.f;

        // Minor step: subdivide major into 4 or 5 (whichever NiceStep prefers)
        float minorStep = majorStep / 4.f;
        // Prevent minor lines from being too dense
        if (minorStep * pixelsPerUnit < kMinGridPixelSpacing)
            minorStep = majorStep / 2.f;
        bool showMinor = (minorStep * pixelsPerUnit >= kMinGridPixelSpacing);

        // Draw minor horizontal lines first (behind major)
        if (showMinor)
        {
            float startMinor = std::ceil(vp.priceMin / minorStep) * minorStep;
            for (float p = startMinor; p <= vp.priceMax; p += minorStep)
            {
                // Skip positions that coincide with major lines
                float nearestMajor = std::round(p / majorStep) * majorStep;
                if (std::abs(p - nearestMajor) < minorStep * 0.3f) continue;

                float y = vp.PriceToY(p);
                if (y < vp.chartOrigin.y || y > vp.chartOrigin.y + vp.chartSize.y) continue;
                drawList->AddLine(
                    ImVec2(vp.chartOrigin.x, y),
                    ImVec2(vp.chartOrigin.x + vp.chartSize.x, y),
                    kGridMinorColor, 1.0f);
            }
        }

        // Draw major horizontal lines
        float startMajor = std::ceil(vp.priceMin / majorStep) * majorStep;
        for (float p = startMajor; p <= vp.priceMax; p += majorStep)
        {
            float y = vp.PriceToY(p);
            if (y < vp.chartOrigin.y || y > vp.chartOrigin.y + vp.chartSize.y) continue;
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x, y),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, y),
                kGridMajorColor, 1.0f);
        }

        // ── Vertical (time) grid ─────────────────────────────────────────────

        float targetVLines = std::clamp(chartWidth_ / 140.f, 3.f, 8.f);
        int vStep = std::max(1, (int)std::round((float)viewport_.visibleCount / targetVLines));

        // Snap to nice candle intervals
        int niceVStep = 1;
        int candidates[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
        for (int c : candidates)
        {
            if (c >= vStep) { niceVStep = c; break; }
            niceVStep = c;
        }

        // Minimum spacing for vertical lines too
        float pixelsPerCandle = vp.candleWidth + vp.candleSpacing;
        float vLineSpacing = niceVStep * pixelsPerCandle;
        while (vLineSpacing < kMinGridPixelSpacing && niceVStep < viewport_.visibleCount)
        {
            niceVStep *= 2;
            vLineSpacing = niceVStep * pixelsPerCandle;
        }

        int end = std::min(vp.visibleStart + vp.visibleCount, (int)data_.candles.size());
        int firstAligned = vp.visibleStart - (vp.visibleStart % niceVStep) + niceVStep;

        // Minor vertical lines (halfway between majors)
        if (showMinor)
        {
            int minorVStep = niceVStep / 2;
            if (minorVStep >= 1 && minorVStep * pixelsPerCandle >= kMinGridPixelSpacing)
            {
                int firstMinor = vp.visibleStart - (vp.visibleStart % minorVStep) + minorVStep;
                for (int i = firstMinor; i < end; i += minorVStep)
                {
                    // Skip major positions
                    if (niceVStep > 0 && (i % niceVStep) == 0) continue;
                    int ri = i - vp.visibleStart;
                    float x = vp.IndexToX(ri) + vp.candleWidth * 0.5f;
                    drawList->AddLine(
                        ImVec2(x, vp.chartOrigin.y),
                        ImVec2(x, vp.chartOrigin.y + vp.chartSize.y),
                        kGridMinorColor, 1.0f);
                }
            }
        }

        // Major vertical lines
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

        // Match grid density (same logic as DrawGrid)
        float targetLines = std::clamp(vp.chartSize.y / 80.f, 4.f, 10.f);
        float majorStep = NiceStep(range, targetLines);

        // Clamp to prevent overly dense labels (same as grid)
        constexpr float kMinLabelSpacing = 20.f;
        float pixelsPerUnit = vp.chartSize.y / range;
        while (majorStep * pixelsPerUnit < kMinLabelSpacing && majorStep < range)
            majorStep *= 2.f;

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

            // Small tick mark on the grid boundary
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, y),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x + 3.f, y),
                kAxisTextColor, 1.0f);

            char buf[16];
            snprintf(buf, sizeof(buf), fmt, p);
            drawList->AddText(ImVec2(axisX, y - 6.f), kAxisTextColor, buf);
        }
    }

    // ── Value axis (generic — for sub-panels with arbitrary Y range) ──────────

    void StockChart::DrawValueAxis(ImDrawList* drawList, const ChartViewport& vp,
                                   float valMin, float valMax)
    {
        float range = valMax - valMin;
        if (range <= 0.f) return;

        float targetLines = std::clamp(vp.chartSize.y / 60.f, 2.f, 8.f);
        float majorStep = NiceStep(range, targetLines);

        constexpr float kMinPx = 18.f;
        float pxPerUnit = vp.chartSize.y / range;
        while (majorStep * pxPerUnit < kMinPx && majorStep < range)
            majorStep *= 2.f;

        // Determine formatting
        int decimals;
        if (majorStep >= 1000.f)      decimals = 0;
        else if (majorStep >= 10.f)   decimals = 0;
        else if (majorStep >= 1.f)    decimals = 1;
        else if (majorStep >= 0.1f)   decimals = 2;
        else if (majorStep >= 0.01f)  decimals = 3;
        else                          decimals = 4;

        // Use "K" suffix for large numbers (volume)
        bool useK = (valMax >= 10000.f);
        bool useM = (valMax >= 10000000.f);

        float axisX = vp.chartOrigin.x + vp.chartSize.x + 4.f;
        float startVal = std::ceil(valMin / majorStep) * majorStep;

        for (float v = startVal; v <= valMax; v += majorStep)
        {
            float t = (v - valMin) / range;
            float y = vp.chartOrigin.y + vp.chartSize.y * (1.f - t);

            if (y < vp.chartOrigin.y + 6.f || y > vp.chartOrigin.y + vp.chartSize.y - 6.f)
                continue;

            // Tick mark
            drawList->AddLine(
                ImVec2(vp.chartOrigin.x + vp.chartSize.x, y),
                ImVec2(vp.chartOrigin.x + vp.chartSize.x + 3.f, y),
                kAxisTextColor, 1.0f);

            char buf[16];
            if (useM)
                snprintf(buf, sizeof(buf), "%.1fM", v / 1000000.f);
            else if (useK)
                snprintf(buf, sizeof(buf), "%.0fK", v / 1000.f);
            else
            {
                char fmt[8];
                snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
                snprintf(buf, sizeof(buf), fmt, v);
            }
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
        // "1m","5m","15m","30m" are intraday minutes; "1h" is intraday hours
        // "1d" is daily; "1wk" is weekly; "1mo" is monthly
        bool intraday = (data_.interval.find('m') != std::string::npos &&
                         data_.interval.find("mo") == std::string::npos) ||
                        data_.interval.find('h') != std::string::npos;
        bool monthly  = data_.interval.find("wk") != std::string::npos ||
                        data_.interval.find("mo") != std::string::npos;

        const char* dateFmt;
        if (intraday)
            dateFmt = "%H:%M";
        else if (monthly || viewport_.visibleCount > 90)
            dateFmt = "%b '%y";
        else if (viewport_.visibleCount <= 30)
            dateFmt = "%b %d";
        else
            dateFmt = "%b %d '%y";

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

        // Focused candle date highlight (shows on hovered + synced charts)
        if (viewport_.focusedCandle >= 0 &&
            viewport_.focusedCandle < (int)data_.candles.size())
        {
            float focusX = viewport_.FocusedX();
            time_t ts = (time_t)data_.candles[viewport_.focusedCandle].timestamp;
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &ts);
#else
            localtime_r(&ts, &tm_buf);
#endif
            bool intradayFocus = (data_.interval.find('m') != std::string::npos &&
                                  data_.interval.find("mo") == std::string::npos) ||
                                 data_.interval.find('h') != std::string::npos;
            char buf[32];
            strftime(buf, sizeof(buf), intradayFocus ? "%b %d %H:%M" : "%b %d, %Y", &tm_buf);
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

        bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (hovered)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x >= chartLeft_ && mouse.x <= chartLeft_ + chartWidth_ &&
                mouse.y >= totalChartTop_ && mouse.y <= totalChartBottom_)
            {
                float step = viewport_.CandleStep();
                int candleIdx = (int)((mouse.x - chartLeft_) / step) + viewport_.visibleStart;

                if (candleIdx >= 0 && candleIdx < (int)data_.candles.size())
                {
                    viewport_.focusedCandle = candleIdx;

                    // Write to shared crosshair so other charts can sync
                    if (sharedCrosshair_)
                        sharedCrosshair_->Set(data_.symbol, candleIdx,
                                              data_.candles[candleIdx].timestamp);
                }
            }
        }
        else if (sharedCrosshair_ && sharedCrosshair_->active &&
                 sharedCrosshair_->symbol == data_.symbol &&
                 !data_.candles.empty())
        {
            // Not hovered — read from shared crosshair (another chart is driving)
            // Match by timestamp since candle indices may differ across timeframes
            int64_t targetTs = sharedCrosshair_->timestamp;
            int bestIdx = -1;
            int64_t bestDist = int64_t(9999999999LL);
            for (int i = viewport_.visibleStart;
                 i < std::min((int)data_.candles.size(), viewport_.visibleStart + viewport_.visibleCount);
                 ++i)
            {
                int64_t dist = std::abs(data_.candles[i].timestamp - targetTs);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx = i;
                }
            }
            if (bestIdx >= 0)
                viewport_.focusedCandle = bestIdx;
        }

        // Clear shared crosshair if nothing is hovered anywhere
        if (!hovered && sharedCrosshair_ && sharedCrosshair_->active)
        {
            // Only clear if WE were the one who set it (check symbol match)
            // Actually, don't clear here — let the hovering chart clear on its own
        }
    }

    void StockChart::DrawSyncedCrosshair(ImDrawList* drawList)
    {
        if (viewport_.focusedCandle < 0) return;

        bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        float focusX = viewport_.FocusedX();
        if (focusX < 0.f) return;

        // Vertical crosshair line (always drawn, local or synced)
        ImU32 lineColor = hovered ? kCrosshairColor : IM_COL32(90, 110, 140, 80); // Dimmer when synced
        drawList->AddLine(
            ImVec2(focusX, totalChartTop_),
            ImVec2(focusX, totalChartBottom_),
            lineColor, 1.0f);

        // Horizontal line + value badges only when locally hovered
        if (!hovered) return;

        ImVec2 mouse = ImGui::GetMousePos();

        for (auto& region : panelRegions_)
        {
            if (mouse.y >= region.origin.y &&
                mouse.y <= region.origin.y + region.size.y)
            {
                drawList->AddLine(
                    ImVec2(region.origin.x, mouse.y),
                    ImVec2(region.origin.x + region.size.x, mouse.y),
                    kCrosshairColor, 1.0f);

                // Show value badge on the right axis for any panel with a range
                if (region.hasRange)
                {
                    float range = region.valMax - region.valMin;
                    if (range > 0.f)
                    {
                        float t = 1.f - (mouse.y - region.origin.y) / region.size.y;
                        float value = region.valMin + t * range;

                        char buf[32];
                        if (region.layer == nullptr)
                        {
                            // Main chart: use price formatting
                            FmtPrice(buf, sizeof(buf), value, data_.symbol);
                        }
                        else if (region.valMax >= 10000000.f)
                            snprintf(buf, sizeof(buf), "%.1fM", value / 1000000.f);
                        else if (region.valMax >= 10000.f)
                            snprintf(buf, sizeof(buf), "%.0fK", value / 1000.f);
                        else
                            snprintf(buf, sizeof(buf), "%.2f", value);

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
            char priceBuf[32];
            FmtPrice(priceBuf, sizeof(priceBuf), fc.close, data_.symbol);
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

        // Find the main panel root layer (first visible root in tree)
        ChartLayer* mainRoot = nullptr;
        for (auto& node : layerTree_)
        {
            if (node.layerIdx < 0 || node.layerIdx >= (int)layers_.size()) continue;
            auto& layer = layers_[node.layerIdx];
            bool anyVis = layer->visible;
            for (int ci : node.children)
                if (ci >= 0 && ci < (int)layers_.size() && layers_[ci]->visible) anyVis = true;
            if (anyVis) { mainRoot = layer.get(); break; }
        }

        // If main root is a non-price layer (RSI, MACD, etc.), use its value range
        bool isPriceLayer = !mainRoot || mainRoot->name == "Candlestick" || mainRoot->name == "Volume";
        if (!isPriceLayer)
        {
            float lo = 0.f, hi = 1.f;
            ChartViewport tmpVp = viewport_;
            if (mainRoot->GetValueRange(tmpVp, data_, lo, hi) && hi > lo)
            {
                float range = hi - lo;
                float margin = range * priceMarginPct_;
                viewport_.priceMin = lo - margin;
                viewport_.priceMax = hi + margin;
                return;
            }
        }

        // Default: scale to candle price range
        int end = std::min(viewport_.visibleStart + viewport_.visibleCount,
                           (int)data_.candles.size());

        float lo = 1e18f, hi = -1e18f;
        for (int i = viewport_.visibleStart; i < end; ++i)
        {
            lo = std::min(lo, data_.candles[i].low);
            hi = std::max(hi, data_.candles[i].high);
        }

        // Include active strategy prices (entry/TP/SL) so zones are always visible
        if (strategyLayer_)
        {
            for (auto& s : strategyLayer_->strategies)
            {
                if (!s.IsActive()) continue;
                if (s.symbol != data_.symbol) continue;
                if (s.entryPrice > 0.f)
                {
                    lo = std::min(lo, s.entryPrice);
                    hi = std::max(hi, s.entryPrice);
                }
                if (s.takeProfit > 0.f)
                {
                    lo = std::min(lo, s.takeProfit);
                    hi = std::max(hi, s.takeProfit);
                }
                if (s.stopLoss > 0.f)
                {
                    lo = std::min(lo, s.stopLoss);
                    hi = std::max(hi, s.stopLoss);
                }
            }
        }

        float range = hi - lo;
        float margin = range * priceMarginPct_;
        viewport_.priceMin = lo - margin;
        viewport_.priceMax = hi + margin;
    }

} // namespace stnks
