#pragma once

#include <Charts/ChartLayer.hpp>
#include <Charts/IStrategyRenderer.hpp>
#include <Charts/TPSLRenderer.hpp>
#include <Charts/PositionRenderer.hpp>
#include <Charts/StrategyWizard.hpp>
#include <Strategy/Strategy.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace stnks
{
    class StrategyLayer : public ChartLayer
    {
    public:
        StrategyLayer() { name = "Strategies"; }

        // Strategies set externally from UI each frame
        std::vector<Strategy> strategies;

        // Callback fired when a strategy is created or modified via gizmo/wizard.
        // bool isNew: true=insert, false=update
        std::function<void(const Strategy&, bool isNew)> onStrategyChanged;

        // Callback fired when a strategy is cancelled/deleted via gizmo
        std::function<void(int64_t id)> onStrategyCancelled;

        // Callback fired when a strategy gizmo is selected (click to edit).
        std::function<void(int64_t id)> onStrategySelected;

        // Callback fired when editing is dismissed via Cancel
        std::function<void()> onEditingDismissed;

        // Callback fired when user wants to create a strategy via context menu.
        // If set, context menu routes here instead of opening the layer wizard overlay.
        // Args: (symbol, type, direction, priceAtClick, visiblePriceRange)
        std::function<void(const std::string&, StrategyType, StrategyDirection, float, float)> onCreateRequested;

        // Currently selected/editing strategy ID
        int64_t GetEditingId() const { return editingId_; }

        // Access the wizard (UI uses this for the "+ Position" button etc.)
        StrategyWizard& GetWizard() { return wizard_; }
        const StrategyWizard& GetWizard() const { return wizard_; }

        // Set current price for position P&L rendering
        void SetCurrentPrice(float price) { posRenderer_.SetCurrentPrice(price); }

        // Highlight a strategy on the chart (from table row hover)
        void SetHighlightedId(int64_t id) { highlightedId_ = id; }
        int64_t GetHighlightedId() const { return highlightedId_; }

        // ── Context menu ────────────────────────────────────────────────────

        void OpenContextMenu(float priceAtClick, const std::string& symbol)
        {
            contextMenuPrice_  = priceAtClick;
            contextMenuSymbol_ = symbol;
            contextMenuOpen_   = true;
        }

        void DrawContextMenu(const ChartViewport& vp)
        {
            if (contextMenuOpen_)
            {
                ImGui::OpenPopup("##StrategyContextMenu");
                contextMenuOpen_ = false;
            }

            if (ImGui::BeginPopup("##StrategyContextMenu"))
            {
                { char pb[32]; FmtPrice(pb, sizeof(pb), contextMenuPrice_, contextMenuSymbol_);
                ImGui::TextDisabled("New Strategy at %s", pb); }
                ImGui::Separator();

                if (ImGui::MenuItem("TP/SL (Long)"))
                    OpenWizardCreate(StrategyDirection::Long, StrategyType::TPSL);
                if (ImGui::MenuItem("TP/SL (Short)"))
                    OpenWizardCreate(StrategyDirection::Short, StrategyType::TPSL);

                ImGui::Separator();

                if (ImGui::MenuItem("Position (Long)"))
                    OpenWizardCreate(StrategyDirection::Long, StrategyType::Position);
                if (ImGui::MenuItem("Position (Short)"))
                    OpenWizardCreate(StrategyDirection::Short, StrategyType::Position);

                ImGui::Separator();

                if (ImGui::MenuItem("AI Strategy (Long)"))
                    OpenWizardCreate(StrategyDirection::Long, StrategyType::AI);
                if (ImGui::MenuItem("AI Strategy (Short)"))
                    OpenWizardCreate(StrategyDirection::Short, StrategyType::AI);

                ImGui::EndPopup();
            }
        }

        // ── State queries ───────────────────────────────────────────────────

        bool IsEditing() const { return wizard_.IsOpen() || editingId_ > 0; }
        bool HasPending() const { return wizard_.IsCreating(); }

        void StartEditing(int64_t id)
        {
            // Find the strategy and open wizard in edit mode
            for (auto& s : strategies)
            {
                if (s.id == id)
                {
                    wizard_.OpenEdit(s);
                    editingId_ = id;
                    return;
                }
            }
            editingId_ = id;
        }

        void StopEditing()
        {
            editingId_ = -1;
            wizard_.Close();
        }

        // ── Draw ────────────────────────────────────────────────────────────

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            lastVisibleRange_ = vp.priceMax - vp.priceMin;
            const std::vector<Candle>* candles = data.candles.empty() ? nullptr : &data.candles;

            // --- Compute mini-bar Y offsets for overlap stacking ---
            // Collect entry Y positions for non-editing active strategies
            struct MiniBarSlot { int idx; float baseY; float offset; };
            std::vector<MiniBarSlot> miniSlots;
            for (int i = 0; i < (int)strategies.size(); ++i)
            {
                auto& s = strategies[i];
                if (s.id == editingId_ || !s.IsActive()) continue;
                float yEntry = vp.PriceToY(s.entryPrice);
                miniSlots.push_back({i, yEntry, 0.f});
            }
            // Sort by Y position, then stack overlapping bars
            std::sort(miniSlots.begin(), miniSlots.end(),
                [](const MiniBarSlot& a, const MiniBarSlot& b) { return a.baseY < b.baseY; });
            const float kBarH = 18.f;
            const float kMinGap = 2.f;
            for (int i = 1; i < (int)miniSlots.size(); ++i)
            {
                float prevBottom = miniSlots[i-1].baseY + miniSlots[i-1].offset + kBarH * 0.5f + kMinGap;
                float thisTop    = miniSlots[i].baseY - kBarH * 0.5f;
                if (thisTop < prevBottom)
                    miniSlots[i].offset = prevBottom - miniSlots[i].baseY + kBarH * 0.5f;
            }
            // Build offset lookup: strategy index → offset
            std::vector<float> miniBarOffsets(strategies.size(), 0.f);
            for (auto& slot : miniSlots)
                miniBarOffsets[slot.idx] = slot.offset;

            // --- Draw all existing strategies ---
            int64_t deleteId = -1;  // Deferred delete (avoid modifying while iterating)
            int64_t selectId = -1;  // Deferred select

            for (int i = 0; i < (int)strategies.size(); ++i)
            {
                auto& s = strategies[i];
                IStrategyRenderer* renderer = GetRenderer(s.type);
                bool isEditing = (s.id == editingId_);
                bool isHighlighted = (s.id == highlightedId_ && !isEditing);

                // Draw highlight glow behind the strategy
                if (isHighlighted)
                {
                    float yEntry = vp.PriceToY(s.entryPrice);
                    float left   = vp.chartOrigin.x;
                    float right  = vp.chartOrigin.x + vp.chartSize.x;
                    float bandH  = 24.f;
                    drawList->AddRectFilled(
                        ImVec2(left, yEntry - bandH),
                        ImVec2(right, yEntry + bandH),
                        IM_COL32(80, 140, 255, 30));
                    if (s.takeProfit > 0.f)
                    {
                        float yTP = vp.PriceToY(s.takeProfit);
                        drawList->AddLine(ImVec2(left, yTP), ImVec2(right, yTP),
                                          IM_COL32(38, 200, 100, 120), 2.f);
                    }
                    if (s.stopLoss > 0.f)
                    {
                        float ySL = vp.PriceToY(s.stopLoss);
                        drawList->AddLine(ImVec2(left, ySL), ImVec2(right, ySL),
                                          IM_COL32(230, 60, 60, 120), 2.f);
                    }
                }

                renderer->Draw(drawList, vp, s, isEditing, candles);

                if (isEditing && wizard_.IsEditing())
                {
                    // Gizmos operate on the wizard's strategy copy
                    Strategy& wizStrat = wizard_.GetStrategy();
                    auto interaction = renderer->HandleGizmos(drawList, vp, wizStrat);

                    if (interaction.modified)
                    {
                        s.entryPrice = wizStrat.entryPrice;
                        s.takeProfit = wizStrat.takeProfit;
                        s.stopLoss   = wizStrat.stopLoss;
                    }

                    if (interaction.confirmed)
                    {
                        s = wizStrat;
                        if (onStrategyChanged)
                            onStrategyChanged(s, false);
                        editingId_ = -1;
                        wizard_.Close();
                    }

                    if (interaction.cancelled)
                    {
                        if (onEditingDismissed)
                            onEditingDismissed();
                        editingId_ = -1;
                        wizard_.Close();
                    }

                    if (interaction.deleted)
                        deleteId = s.id;
                }
                else if (isEditing && !wizard_.IsOpen())
                {
                    auto interaction = renderer->HandleGizmos(drawList, vp, s);

                    if (interaction.confirmed)
                    {
                        if (onStrategyChanged)
                            onStrategyChanged(s, false);
                        editingId_ = -1;
                    }
                    if (interaction.cancelled)
                    {
                        if (onEditingDismissed)
                            onEditingDismissed();
                        editingId_ = -1;
                    }
                    if (interaction.deleted)
                        deleteId = s.id;
                }
                else if (editingId_ != s.id && s.IsActive())
                {
                    // Draw mini-bar for non-editing active strategies
                    auto interaction = renderer->DrawMiniBar(drawList, vp, s, miniBarOffsets[i]);

                    if (interaction.deleted)
                        deleteId = s.id;
                    else if (interaction.selected)
                        selectId = s.id;
                }
            }

            // Process deferred delete
            if (deleteId >= 0)
            {
                if (editingId_ == deleteId)
                {
                    editingId_ = -1;
                    wizard_.Close();
                }
                if (onStrategyCancelled)
                    onStrategyCancelled(deleteId);
            }

            // Process deferred select
            if (selectId >= 0 && deleteId < 0)
            {
                for (auto& s : strategies)
                {
                    if (s.id == selectId)
                    {
                        editingId_ = s.id;
                        if (onStrategySelected)
                        {
                            // Route to UI-level dockable wizard; don't open layer overlay
                            onStrategySelected(s.id);
                        }
                        else
                        {
                            // Fallback: open layer overlay wizard
                            wizard_.OpenEdit(s);
                        }
                        break;
                    }
                }
            }

            // Draw pending (wizard creating new) with gizmos
            if (wizard_.IsCreating())
            {
                Strategy& pending = wizard_.GetStrategy();
                IStrategyRenderer* renderer = GetRenderer(pending.type);
                renderer->Draw(drawList, vp, pending, true, candles);
                auto interaction = renderer->HandleGizmos(drawList, vp, pending);

                if (interaction.confirmed)
                {
                    pending.createdAt = std::time(nullptr);
                    if (onStrategyChanged)
                        onStrategyChanged(pending, true);
                    wizard_.Close();
                }

                if (interaction.cancelled)
                    wizard_.Close();
            }

            DrawContextMenu(vp);
        }

        // Draw the wizard overlay UI. Call AFTER the chart child is drawn.
        // This is separate from Draw() because it needs ImGui widget context.
        void DrawWizard(const ImVec2& chartMin, const ImVec2& chartMax,
                        int focusedCandle, int totalCandles,
                        const std::vector<Candle>* candles)
        {
            if (!wizard_.IsOpen()) return;

            wizard_.Draw(chartMin, chartMax, focusedCandle, totalCandles, candles);

            // Handle wizard results
            if (wizard_.WasConfirmed())
            {
                Strategy result = wizard_.GetStrategy();

                if (editingId_ > 0)
                {
                    // Edit mode: update existing
                    result.id = editingId_;
                    if (onStrategyChanged)
                        onStrategyChanged(result, false);
                }
                else
                {
                    // Create mode: insert new
                    if (onStrategyChanged)
                        onStrategyChanged(result, true);
                }

                editingId_ = -1;
                wizard_.ConsumeResult();
            }
            else if (wizard_.WasCancelled())
            {
                if (editingId_ > 0 && onEditingDismissed)
                    onEditingDismissed();
                editingId_ = -1;
                wizard_.ConsumeResult();
            }
        }

    private:
        TPSLRenderer     tpslRenderer_;
        PositionRenderer posRenderer_;
        StrategyWizard   wizard_;

        // Context menu state
        float       contextMenuPrice_  = 0.f;
        float       lastVisibleRange_  = 0.f;
        std::string contextMenuSymbol_;
        bool        contextMenuOpen_   = false;

        // Editing / highlight state
        int64_t editingId_     = -1;
        int64_t highlightedId_ = -1;

        IStrategyRenderer* GetRenderer(StrategyType type)
        {
            switch (type)
            {
            case StrategyType::TPSL: return &tpslRenderer_;
            case StrategyType::Position:
            case StrategyType::AI:
                return &posRenderer_;
            }
            return &tpslRenderer_;
        }

        void OpenWizardCreate(StrategyDirection direction, StrategyType type)
        {
            if (onCreateRequested)
            {
                // Route to UI-level dockable wizard
                onCreateRequested(contextMenuSymbol_, type, direction, contextMenuPrice_, lastVisibleRange_);
            }
            else
            {
                // Fallback: open layer overlay wizard
                wizard_.OpenCreate(contextMenuSymbol_, type, direction, contextMenuPrice_, lastVisibleRange_);
            }
            editingId_ = -1;
        }

    };

} // namespace stnks
