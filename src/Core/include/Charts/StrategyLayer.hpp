#pragma once

#include <Charts/ChartLayer.hpp>
#include <Charts/IStrategyRenderer.hpp>
#include <Charts/TPSLRenderer.hpp>
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

        // Callback fired when a strategy is created, modified, or deleted visually.
        // bool isNew: true=insert, false=update
        std::function<void(const Strategy&, bool isNew)> onStrategyChanged;

        // Callback fired when a strategy is cancelled/deleted via gizmo
        std::function<void(int64_t id)> onStrategyCancelled;

        // Callback fired when a strategy gizmo is selected (click to edit).
        // The UI uses this to focus the Strategies tab on the selected row.
        std::function<void(int64_t id)> onStrategySelected;

        // Callback fired when editing is dismissed via the gizmo Cancel button.
        std::function<void()> onEditingDismissed;

        // Currently selected/editing strategy ID (readable by UI for highlighting)
        int64_t GetEditingId() const { return editingId_; }

        // ── Context menu ────────────────────────────────────────────────────

        // Call from StockChart when user right-clicks on the chart.
        // Stores the price and opens the popup.
        void OpenContextMenu(float priceAtClick, const std::string& symbol)
        {
            contextMenuPrice_  = priceAtClick;
            contextMenuSymbol_ = symbol;
            contextMenuOpen_   = true;
        }

        // Draw the context menu popup (call from StockChart::Draw after ImGui::BeginChild)
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
                {
                    CreatePending(StrategyDirection::Long);
                }
                if (ImGui::MenuItem("TP/SL (Short)"))
                {
                    CreatePending(StrategyDirection::Short);
                }

                ImGui::Separator();
                ImGui::TextDisabled("More types coming...");

                ImGui::EndPopup();
            }
        }

        // ── State queries ───────────────────────────────────────────────────

        bool IsEditing() const { return hasPending_ || editingId_ > 0; }
        bool HasPending() const { return hasPending_; }

        // Select an existing strategy for editing (click on it)
        void StartEditing(int64_t id)
        {
            editingId_ = id;
            hasPending_ = false;
        }

        void StopEditing()
        {
            editingId_ = -1;
            hasPending_ = false;
        }

        // ── Draw ────────────────────────────────────────────────────────────

        void Draw(ImDrawList* drawList, const ChartViewport& vp,
                  const StockQuote& data) override
        {
            // Draw all existing strategies
            for (auto& s : strategies)
            {
                bool isEditing = (s.id == editingId_);
                tpslRenderer_.Draw(drawList, vp, s, isEditing);

                if (isEditing)
                {
                    Strategy editCopy = s;
                    auto interaction = tpslRenderer_.HandleGizmos(drawList, vp, editCopy);

                    if (interaction.modified)
                    {
                        s.entryPrice = editCopy.entryPrice;
                        s.takeProfit = editCopy.takeProfit;
                        s.stopLoss   = editCopy.stopLoss;
                    }

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
                else if (!hasPending_ && editingId_ <= 0)
                {
                    // Click on any of the strategy's lines to start editing
                    if (s.IsActive() &&
                        (IsClickOnLine(vp, vp.PriceToY(s.entryPrice)) ||
                         IsClickOnLine(vp, vp.PriceToY(s.takeProfit)) ||
                         IsClickOnLine(vp, vp.PriceToY(s.stopLoss))))
                    {
                        editingId_ = s.id;
                        if (onStrategySelected)
                            onStrategySelected(s.id);
                    }
                }
            }

            // Draw pending (being-created) strategy
            if (hasPending_)
            {
                tpslRenderer_.Draw(drawList, vp, pendingStrategy_, true);
                auto interaction = tpslRenderer_.HandleGizmos(drawList, vp, pendingStrategy_);

                if (interaction.confirmed)
                {
                    pendingStrategy_.createdAt = std::time(nullptr);
                    if (onStrategyChanged)
                        onStrategyChanged(pendingStrategy_, true);
                    hasPending_ = false;
                }

                if (interaction.cancelled)
                {
                    hasPending_ = false;
                }
            }

            // Draw context menu
            DrawContextMenu(vp);
        }

    private:
        TPSLRenderer tpslRenderer_;

        // Context menu state
        float       contextMenuPrice_  = 0.f;
        std::string contextMenuSymbol_;
        bool        contextMenuOpen_   = false;

        // Editing state
        int64_t  editingId_  = -1;    // ID of strategy being edited (-1 = none)
        Strategy pendingStrategy_;     // Strategy being created (not yet saved)
        bool     hasPending_ = false;

        void CreatePending(StrategyDirection direction)
        {
            float entry = contextMenuPrice_;
            float offsetPct = 0.05f; // 5% default

            pendingStrategy_ = Strategy{};
            pendingStrategy_.symbol    = contextMenuSymbol_;
            pendingStrategy_.direction = direction;
            pendingStrategy_.entryPrice = entry;

            if (direction == StrategyDirection::Long)
            {
                pendingStrategy_.takeProfit = entry * (1.f + offsetPct);
                pendingStrategy_.stopLoss   = entry * (1.f - offsetPct * 0.6f);
            }
            else
            {
                pendingStrategy_.takeProfit = entry * (1.f - offsetPct);
                pendingStrategy_.stopLoss   = entry * (1.f + offsetPct * 0.6f);
            }

            hasPending_ = true;
            editingId_  = -1;
        }

        bool IsClickOnLine(const ChartViewport& vp, float lineY)
        {
            if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
            if (!ImGui::IsWindowHovered()) return false;

            ImVec2 mouse = ImGui::GetMousePos();

            // Check mouse is within chart bounds
            if (mouse.x < vp.chartOrigin.x ||
                mouse.x > vp.chartOrigin.x + vp.chartSize.x) return false;

            // 6px tolerance for clicking on a line
            return std::abs(mouse.y - lineY) < 6.f;
        }
    };

} // namespace stnks
