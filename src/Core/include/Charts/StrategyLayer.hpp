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

        // Currently selected/editing strategy ID
        int64_t GetEditingId() const { return editingId_; }

        // Access the wizard (UI uses this for the "+ Position" button etc.)
        StrategyWizard& GetWizard() { return wizard_; }
        const StrategyWizard& GetWizard() const { return wizard_; }

        // Set current price for position P&L rendering
        void SetCurrentPrice(float price) { posRenderer_.SetCurrentPrice(price); }

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
                ImGui::TextDisabled("New Strategy at %.2f", contextMenuPrice_);
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
            const std::vector<Candle>* candles = data.candles.empty() ? nullptr : &data.candles;

            // Draw all existing strategies with appropriate renderer
            for (auto& s : strategies)
            {
                IStrategyRenderer* renderer = GetRenderer(s.type);
                bool isEditing = (s.id == editingId_);
                renderer->Draw(drawList, vp, s, isEditing, candles);

                if (isEditing && wizard_.IsEditing())
                {
                    // Gizmos operate on the wizard's strategy copy
                    Strategy& wizStrat = wizard_.GetStrategy();
                    auto interaction = renderer->HandleGizmos(drawList, vp, wizStrat);

                    if (interaction.modified)
                    {
                        // Sync gizmo changes back to the displayed strategy
                        s.entryPrice = wizStrat.entryPrice;
                        s.takeProfit = wizStrat.takeProfit;
                        s.stopLoss   = wizStrat.stopLoss;
                    }

                    if (interaction.confirmed)
                    {
                        // Gizmo confirm: apply wizard state → save
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
                }
                else if (isEditing && !wizard_.IsOpen())
                {
                    // Editing via gizmo only (wizard was closed externally)
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
                }
                else if (!wizard_.IsOpen() && editingId_ <= 0)
                {
                    // Click on strategy lines to start editing
                    if (s.IsActive() && ClickHitsStrategy(vp, s))
                    {
                        wizard_.OpenEdit(s);
                        editingId_ = s.id;
                        if (onStrategySelected)
                            onStrategySelected(s.id);
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
        std::string contextMenuSymbol_;
        bool        contextMenuOpen_   = false;

        // Editing state
        int64_t editingId_ = -1;

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
            wizard_.OpenCreate(contextMenuSymbol_, type, direction, contextMenuPrice_);
            editingId_ = -1;
        }

        bool ClickHitsStrategy(const ChartViewport& vp, const Strategy& s)
        {
            if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
            if (!ImGui::IsWindowHovered()) return false;

            ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x < vp.chartOrigin.x ||
                mouse.x > vp.chartOrigin.x + vp.chartSize.x) return false;

            float tolerance = 6.f;

            // Always check entry line
            if (std::abs(mouse.y - vp.PriceToY(s.entryPrice)) < tolerance)
                return true;

            // TPSL: also check TP and SL lines
            if (s.type == StrategyType::TPSL)
            {
                if (s.takeProfit > 0.f && std::abs(mouse.y - vp.PriceToY(s.takeProfit)) < tolerance)
                    return true;
                if (s.stopLoss > 0.f && std::abs(mouse.y - vp.PriceToY(s.stopLoss)) < tolerance)
                    return true;
            }

            return false;
        }
    };

} // namespace stnks
