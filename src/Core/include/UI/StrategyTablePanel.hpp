#pragma once

#include <UI/UIContext.hpp>
#include <UI/StrategyTableDefs.hpp>
#include <Strategy/Strategy.hpp>
#include <imgui.h>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <cstdint>

namespace stnks
{
    class StrategyTablePanel
    {
    public:
        explicit StrategyTablePanel(UIContext& ctx);

        // Full window: tabs (Active/Positions/AI/Triggered/All) + toolbar + table
        void DrawWindow(bool* open);

        // Single filtered table (used inside each tab)
        void Draw(const std::function<bool(const Strategy&)>& filter);

        // CSV import / export (triggered from menu bar)
        void ExportCSV();
        void ImportCSV();

        // Public accessors for cross-panel interaction (e.g. chart layer callbacks)
        void ClearCellEditForRow(int64_t rowId);
        void SelectSingle(int64_t id);
        void ClearSelection();
        std::set<int64_t>& GetSelection() { return selection_; }
        int64_t GetLastClickedId() const { return lastClickedId_; }

    private:
        UIContext& ctx_;

        // Per-column filter state
        struct TableFilter
        {
            bool  active      = false;
            char  symbolBuf[64]  = "";
            char  notesBuf[256]  = "";
            int   typeFilter     = -1;
            int   dirFilter      = -1;
            int   statusFilter   = -1;
            float entryMin = 0.f, entryMax = 0.f;
            float tpMin    = 0.f, tpMax    = 0.f;
            float slMin    = 0.f, slMax    = 0.f;
            float qtyMin   = 0.f, qtyMax   = 0.f;
            float pnlMin   = -9999.f, pnlMax = 9999.f;
            bool  pnlEnabled = false;

            bool HasAnyFilter() const;
            void Clear();
        };
        TableFilter filter_;

        // Multi-selection
        std::set<int64_t> selection_;
        int64_t           lastClickedId_ = -1;

        // Sorting — uses StratCol directly
        StratCol sortCol_  = StratCol::Check; // Check == None (not sortable)
        bool     sortAsc_  = true;

        // Cell editing — uses StratCol as column identifier
        int64_t  editCellRowId_ = -1;
        StratCol editCellCol_   = StratCol::Check;
        char     cellEditBuf_[256] = "";
        float    cellEditFloat_ = 0.f;

        // Sub-methods
        void DrawToolbar(std::vector<Strategy*>& filtered);
        void DrawFilterRow();
        void DrawTableBody(std::vector<Strategy*>& filtered);
        void DrawStrategyRow(Strategy& s, const std::vector<Strategy*>& filtered,
                             int64_t& deleteId, const Strategy*& viewStrat);
        void DrawIdentityCells(Strategy& s, bool isCellEditing);
        void DrawPriceCells(Strategy& s, bool isCellEditing);
        void DrawFeeCells(Strategy& s, bool isCellEditing);
        void DrawAnalyticsCells(Strategy& s);
        void DrawStatusCells(Strategy& s, bool isCellEditing);
        void DrawAveragingCells(Strategy& s);
        void DrawActionCells(Strategy& s, int64_t& deleteId, const Strategy*& viewStrat);
        void HandleDeferredActions(int64_t deleteId, const Strategy* viewStrat);

        void SortStrategies(std::vector<Strategy*>& ptrs);
        void StartCellEdit(const Strategy& strat, StratCol col);
        void CommitCellEdit(Strategy& strat, StratCol col);
        bool MatchesFilter(const Strategy& strat) const;

        // Row interaction helpers (set by DrawStrategyRow for the current row)
        bool     rowHovered_     = false;
        bool     isInSelection_  = false;
        ImGuiIO* io_             = nullptr;

        void HandleRowClick(Strategy& s, const std::vector<Strategy*>& filtered);
        void HandleCellDblClick(const Strategy& s, StratCol col);
    };

} // namespace stnks
