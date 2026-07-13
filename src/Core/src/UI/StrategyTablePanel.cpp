#include <UI/StrategyTablePanel.hpp>
#include <UI/UIConstants.hpp>
#include <UI/StrategyTableDefs.hpp>
#include <Market/MarketHours.hpp>
#include <imgui_internal.h>
#include <portable-file-dialogs.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cctype>

namespace stnks
{

    bool StrategyTablePanel::TableFilter::HasAnyFilter() const
    {
        return symbolBuf[0] || notesBuf[0] ||
               typeFilter >= 0 || dirFilter >= 0 || statusFilter >= 0 ||
               entryMin != 0.f || entryMax != 0.f ||
               tpMin != 0.f || tpMax != 0.f ||
               slMin != 0.f || slMax != 0.f ||
               qtyMin != 0.f || qtyMax != 0.f ||
               pnlEnabled;
    }

    void StrategyTablePanel::TableFilter::Clear()
    {
        symbolBuf[0] = '\0';
        notesBuf[0]  = '\0';
        typeFilter   = -1;
        dirFilter    = -1;
        statusFilter = -1;
        entryMin = entryMax = 0.f;
        tpMin = tpMax = 0.f;
        slMin = slMax = 0.f;
        qtyMin = qtyMax = 0.f;
        pnlMin = -9999.f; pnlMax = 9999.f;
        pnlEnabled = false;
    }


    StrategyTablePanel::StrategyTablePanel(UIContext& ctx) : ctx_(ctx) {}


    void StrategyTablePanel::DrawWindow(bool* open)
    {
        ImGui::Begin("Strategies", open);

        ImGui::TextDisabled("Right-click on a chart to create a strategy visually");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 380.f);

        if (ImGui::SmallButton("Export CSV"))
            ExportCSV();
        ImGui::SameLine();
        if (ImGui::SmallButton("Import CSV"))
            ImportCSV();
        ImGui::SameLine();

        if (!selection_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kDeleteText);
            char delLabel[32];
            snprintf(delLabel, sizeof(delLabel), "Del (%d)", (int)selection_.size());
            if (ImGui::SmallButton(delLabel))
            {
                for (int64_t id : selection_)
                {
                    bool active = false;
                    for (auto& s : *ctx_.strategies)
                        if (s.id == id && s.IsActive()) { active = true; break; }

                    if (active)
                    {
                        float exitPrice = 0.f;
                        for (auto& s : *ctx_.strategies)
                            if (s.id == id) { exitPrice = ctx_.getCurrentPrice(s.symbol); break; }
                        if (!ctx_.service->CancelStrategy(id, exitPrice))
                            ctx_.PushToast("Failed to cancel strategy #" + std::to_string(id), ui::kToastError);
                    }
                    else
                    {
                        if (!ctx_.service->DeleteStrategy(id))
                            ctx_.PushToast("Failed to delete strategy #" + std::to_string(id), ui::kToastError);
                    }
                }
                selection_.clear();
                *ctx_.selectedStrategyId = -1;
                editCellRowId_ = -1;
                *ctx_.strategiesDirty = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        if (ImGui::SmallButton("Refresh"))
            *ctx_.strategiesDirty = true;

        ImGui::Separator();

        if (ImGui::BeginTabBar("##StratTabs"))
        {
            if (ImGui::BeginTabItem("Active"))
            {
                Draw([](const Strategy& s) { return s.IsActive(); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Positions"))
            {
                Draw([](const Strategy& s) { return s.IsPosition() && s.IsActive(); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("AI"))
            {
                Draw([](const Strategy& s) { return s.IsAI() && s.IsActive(); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Triggered"))
            {
                Draw([](const Strategy& s) {
                    return s.status == StrategyStatus::TPHit || s.status == StrategyStatus::SLHit;
                });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("All"))
            {
                Draw([](const Strategy&) { return true; });
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }


    void StrategyTablePanel::Draw(const std::function<bool(const Strategy&)>& filter)
    {
        std::vector<Strategy*> filtered;
        for (auto& s : *ctx_.strategies)
            if (filter(s) && MatchesFilter(s))
                filtered.push_back(&s);

        if (filtered.empty())
        {
            if (filter_.HasAnyFilter())
                ImGui::TextDisabled("No strategies match filters");
            else
                ImGui::TextDisabled("No strategies");
            return;
        }

        SortStrategies(filtered);
        DrawToolbar(filtered);
        ImGui::Spacing();
        DrawTableBody(filtered);
    }


    void StrategyTablePanel::DrawToolbar(std::vector<Strategy*>& filtered)
    {
        int selCount = (int)selection_.size();
        if (selCount > 0)
            ImGui::Text("%d selected", selCount);
        else
            ImGui::TextDisabled("Select rows with checkboxes");

        ImGui::SameLine(200.f);
        bool hasSel = selCount > 0;
        if (!hasSel) ImGui::BeginDisabled();

        if (ImGui::SmallButton("Enable"))
        {
            for (auto& s : *ctx_.strategies)
                if (selection_.count(s.id)) {
                    s.enabled = true;
                    if (!ctx_.service->UpdateStrategy(s))
                        ctx_.PushToast("Failed to enable strategy #" + std::to_string(s.id), ui::kToastError);
                }
            *ctx_.strategiesDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Disable"))
        {
            for (auto& s : *ctx_.strategies)
                if (selection_.count(s.id)) {
                    s.enabled = false;
                    if (!ctx_.service->UpdateStrategy(s))
                        ctx_.PushToast("Failed to disable strategy #" + std::to_string(s.id), ui::kToastError);
                }
            *ctx_.strategiesDirty = true;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::kDeleteText);
        if (ImGui::SmallButton("Delete Selected"))
        {
            for (int64_t id : selection_)
            {
                bool active = false;
                for (auto& s : *ctx_.strategies)
                    if (s.id == id && s.IsActive()) { active = true; break; }
                if (active)
                {
                    float ep = 0.f;
                    for (auto& s : *ctx_.strategies)
                        if (s.id == id) { ep = ctx_.getCurrentPrice(s.symbol); break; }
                    if (!ctx_.service->CancelStrategy(id, ep))
                        ctx_.PushToast("Failed to cancel strategy #" + std::to_string(id), ui::kToastError);
                }
                else
                {
                    if (!ctx_.service->DeleteStrategy(id))
                        ctx_.PushToast("Failed to delete strategy #" + std::to_string(id), ui::kToastError);
                }
            }
            selection_.clear();
            *ctx_.strategiesDirty = true;
        }
        ImGui::PopStyleColor();
        if (!hasSel) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        bool hasFilters = filter_.HasAnyFilter();
        if (hasFilters)
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kFilterActiveText);
        if (ImGui::SmallButton(filter_.active ? "Hide Filters" : "Filters"))
            filter_.active = !filter_.active;
        if (hasFilters) ImGui::PopStyleColor();
        if (hasFilters)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d results)", (int)filtered.size());
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
        if (ImGui::SmallButton("Clear"))
            selection_.clear();
    }


    void StrategyTablePanel::DrawTableBody(std::vector<Strategy*>& filtered)
    {
        ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_HighlightHoveredColumn;

        ImGui::PushStyleColor(ImGuiCol_TableRowBg,    ui::kTableRowBg);
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ui::kTableRowBgAlt);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ui::kTableHdrHover);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ui::kTableHdrActive);

        if (!ImGui::BeginTable("##StratTable", kStratColCount, tableFlags, ImVec2(0.f, 0.f)))
        {
            ImGui::PopStyleColor(4);
            return;
        }

        ImGui::TableSetupScrollFreeze(0, filter_.active ? 2 : 1);

        // Setup columns from descriptors
        for (int i = 0; i < kStratColCount; ++i)
            ImGui::TableSetupColumn(kStratColumns[i].name, kStratColumns[i].flags, kStratColumns[i].width);

        // Header with select-all checkbox
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int col = 0; col < kStratColCount; ++col)
        {
            ImGui::TableSetColumnIndex(col);
            if (col == (int)StratCol::Check)
            {
                bool all = !filtered.empty() && selection_.size() == filtered.size();
                bool some = !selection_.empty() && !all;
                if (some) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox("##all", &all))
                {
                    if (all)
                        for (auto* p : filtered) selection_.insert(p->id);
                    else
                        selection_.clear();
                }
                if (some) ImGui::PopItemFlag();
            }
            else
                ImGui::TableHeader(ImGui::TableGetColumnName(col));
        }

        // Sort specs
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                int ci = specs->Specs[0].ColumnIndex;
                if (ci >= 0 && ci < kStratColCount && kStratColumns[ci].sortable)
                    sortCol_ = static_cast<StratCol>(ci);
                else
                    sortCol_ = StratCol::Check; // None
                sortAsc_ = (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                specs->SpecsDirty = false;
                SortStrategies(filtered);
            }
        }

        if (filter_.active)
            DrawFilterRow();

        int64_t deleteId = -1;
        const Strategy* viewStrat = nullptr;
        io_ = &ImGui::GetIO();
        *ctx_.hoveredStrategyId = -1;

        for (auto* ptr : filtered)
            DrawStrategyRow(*ptr, filtered, deleteId, viewStrat);

        ImGui::EndTable();
        ImGui::PopStyleColor(4);

        HandleDeferredActions(deleteId, viewStrat);
    }


    void StrategyTablePanel::DrawStrategyRow(
        Strategy& s, const std::vector<Strategy*>& filtered,
        int64_t& deleteId, const Strategy*& viewStrat)
    {
        isInSelection_ = selection_.count(s.id) > 0;
        bool isCellEditing = (editCellRowId_ == s.id);
        bool isActiveEdit  = (s.id == *ctx_.selectedStrategyId);

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(s.id));

        if (!s.enabled)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ui::kDisabledAlpha);

        // Row background
        if (isActiveEdit)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ui::kRowEditHighlight);
        else if (isInSelection_)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ui::kRowSelectTint);

        // Scroll to selected
        if (s.id == *ctx_.selectedStrategyId)
            ImGui::SetScrollHereY(0.5f);

        // Col 0: Checkbox with row-span selectable
        ImGui::TableNextColumn();
        {
            ImGui::Selectable("##rowsel", isInSelection_,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
            rowHovered_ = ImGui::IsItemHovered();
            if (rowHovered_ && !isInSelection_ && !isActiveEdit)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ui::kRowHoverTint);
            if (rowHovered_)
                *ctx_.hoveredStrategyId = s.id;
            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX());
            bool checked = isInSelection_;
            if (ImGui::Checkbox("##chk", &checked))
            {
                if (checked) selection_.insert(s.id);
                else selection_.erase(s.id);
                lastClickedId_ = s.id;
            }
        }

        DrawIdentityCells(s, isCellEditing);
        DrawPriceCells(s, isCellEditing);
        DrawFeeCells(s, isCellEditing);
        DrawAnalyticsCells(s);
        DrawStatusCells(s, isCellEditing);
        DrawAveragingCells(s);
        DrawActionCells(s, deleteId, viewStrat);

        if (!s.enabled) ImGui::PopStyleVar();
        ImGui::PopID();
    }


    void StrategyTablePanel::DrawIdentityCells(Strategy& s, bool isCellEditing)
    {
        // Symbol
        ImGui::TableNextColumn();
        if (isCellEditing && editCellCol_ == StratCol::Symbol)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##sym", cellEditBuf_, sizeof(cellEditBuf_),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                CommitCellEdit(s, StratCol::Symbol);
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
            { editCellRowId_ = -1; editCellCol_ = StratCol::Check; }
        }
        else
        {
            auto mkt = MarketHours::ClassifySymbol(s.symbol);
            ImGui::TextColored(ui::MarketTypeColor(mkt), "%s", s.symbol.c_str());
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Symbol);
        }

        // Type
        ImGui::TableNextColumn();
        if (isCellEditing && editCellCol_ == StratCol::Type)
        {
            int typeVal = static_cast<int>(s.type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##type", &typeVal, "TP/SL\0Position\0AI\0"))
            {
                s.type = static_cast<StrategyType>(typeVal);
                if (!ctx_.service->UpdateStrategy(s))
                    ctx_.PushToast("Failed to update strategy type", ui::kToastError);
                *ctx_.strategiesDirty = true;
                editCellRowId_ = -1; editCellCol_ = StratCol::Check;
            }
        }
        else
        {
            ImGui::TextColored(ui::StrategyTypeColor(s.type), "%s", ui::StrategyTypeLabel(s.type));
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Type);
        }

        // Direction
        ImGui::TableNextColumn();
        if (isCellEditing && editCellCol_ == StratCol::Dir)
        {
            int dir = static_cast<int>(s.direction);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##dir", &dir, "Long\0Short\0"))
            {
                s.direction = static_cast<StrategyDirection>(dir);
                if (!ctx_.service->UpdateStrategy(s))
                    ctx_.PushToast("Failed to update strategy direction", ui::kToastError);
                *ctx_.strategiesDirty = true;
                editCellRowId_ = -1; editCellCol_ = StratCol::Check;
            }
        }
        else
        {
            ImGui::TextColored(ui::DirectionColor(s.direction), "%s", ui::DirectionLabel(s.direction));
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Dir);
        }
    }


    void StrategyTablePanel::DrawPriceCells(Strategy& s, bool isCellEditing)
    {
        auto DrawFloatEdit = [&](StratCol col, float& target, const char* id) {
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == col)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputFloat(id, &cellEditFloat_, 0.f, 0.f, "%.2f",
                    ImGuiInputTextFlags_EnterReturnsTrue))
                    CommitCellEdit(s, col);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = StratCol::Check; }
            }
            else
            {
                if (target > 0.f)
                {
                    char pb[32];
                    ImVec4 col4 = (col == StratCol::TP) ? ui::kColorBullish
                                : (col == StratCol::SL) ? ui::kColorBearish
                                : ImVec4(1,1,1,1);
                    if (col == StratCol::Entry)
                        ImGui::Text("%s", FmtPrice(pb, sizeof(pb), target, s.symbol));
                    else
                        ImGui::TextColored(col4, "%s", FmtPrice(pb, sizeof(pb), target, s.symbol));
                }
                else
                    ImGui::TextDisabled("-");
                HandleRowClick(s, {});
                HandleCellDblClick(s, col);
            }
        };

        // Entry
        DrawFloatEdit(StratCol::Entry, s.entryPrice, "##entry");

        // Current Price (virtual, read-only)
        ImGui::TableNextColumn();
        {
            float curPrice = ctx_.getCurrentPrice(s.symbol);
            if (curPrice > 0.f)
            {
                bool up = curPrice >= s.entryPrice;
                char pb[32];
                ImGui::TextColored(up ? ui::kColorBullish : ui::kColorBearish,
                    "%s", FmtPrice(pb, sizeof(pb), curPrice, s.symbol));
            }
            else
                ImGui::TextDisabled("-");
        }
        HandleRowClick(s, {});

        // TP
        DrawFloatEdit(StratCol::TP, s.takeProfit, "##tp");
        // SL
        DrawFloatEdit(StratCol::SL, s.stopLoss, "##sl");

        // Qty
        ImGui::TableNextColumn();
        if (isCellEditing && editCellCol_ == StratCol::Qty)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputFloat("##qty", &cellEditFloat_, 0.f, 0.f, "%.2f",
                ImGuiInputTextFlags_EnterReturnsTrue))
                CommitCellEdit(s, StratCol::Qty);
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
            { editCellRowId_ = -1; editCellCol_ = StratCol::Check; }
        }
        else
        {
            if (s.quantity > 0.f)
                ImGui::Text("%.0f", s.quantity);
            else
                ImGui::TextDisabled("-");
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Qty);
        }
    }


    void StrategyTablePanel::DrawFeeCells(Strategy& s, bool isCellEditing)
    {
        auto DrawFee = [&](StratCol col, float& val, const char* id) {
            ImGui::TableNextColumn();
            if (isCellEditing && editCellCol_ == col)
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputFloat(id, &cellEditFloat_, 0.f, 0.f, "%.2f",
                    ImGuiInputTextFlags_EnterReturnsTrue))
                    CommitCellEdit(s, col);
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
                { editCellRowId_ = -1; editCellCol_ = StratCol::Check; }
            }
            else
            {
                if (val != 0.f)
                    ImGui::TextColored(ui::kColorFee, "%.2f", val);
                else
                    ImGui::TextDisabled("-");
                HandleRowClick(s, {});
                HandleCellDblClick(s, col);
            }
        };

        DrawFee(StratCol::EntryFee, s.entryFee, "##efee");
        DrawFee(StratCol::ExitFee,  s.exitFee,  "##xfee");
    }


    void StrategyTablePanel::DrawAnalyticsCells(Strategy& s)
    {
        // R:R
        ImGui::TableNextColumn();
        if (s.stopLoss > 0.f && s.takeProfit > 0.f)
        {
            float rr = s.RiskReward();
            ImGui::TextColored(ui::RiskRewardColor(rr), "%.1f", rr);
        }
        else
            ImGui::TextDisabled("-");
        HandleRowClick(s, {});

        // P/L%
        ImGui::TableNextColumn();
        {
            float pnlPct = 0.f;
            bool hasPnl = false;

            if (!s.IsActive())
            {
                if (s.closedPnlPct != 0.f) { pnlPct = s.closedPnlPct; hasPnl = true; }
                else if (s.exitPrice > 0.f && s.entryPrice > 0.f)
                { pnlPct = s.UnrealizedPnLPercent(s.EffectiveExitPrice()); hasPnl = true; }
            }
            else
            {
                float curPrice = ctx_.getCurrentPrice(s.symbol);
                if (curPrice > 0.f && s.entryPrice > 0.f)
                { pnlPct = s.UnrealizedPnLPercent(curPrice); hasPnl = true; }
            }

            if (hasPnl)
                ImGui::TextColored(ui::PnLColor(pnlPct), "%+.2f%%", pnlPct);
            else
                ImGui::TextDisabled("-");
        }
        HandleRowClick(s, {});

        // Exit Price
        ImGui::TableNextColumn();
        if (s.exitPrice > 0.f)
        { char pb[32]; ImGui::Text("%s", FmtPrice(pb, sizeof(pb), s.exitPrice, s.symbol)); }
        else
            ImGui::TextDisabled("-");
        HandleRowClick(s, {});
    }


    void StrategyTablePanel::DrawStatusCells(Strategy& s, bool isCellEditing)
    {
        // Status
        ImGui::TableNextColumn();
        if (isCellEditing && editCellCol_ == StratCol::Status)
        {
            int st = static_cast<int>(s.status);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##status", &st, "Active\0TP Hit\0SL Hit\0Cancelled\0"))
            {
                s.status = static_cast<StrategyStatus>(st);
                if (s.status != StrategyStatus::Active && s.triggeredAt == 0)
                    s.triggeredAt = std::time(nullptr);
                if (!ctx_.service->UpdateStrategy(s))
                    ctx_.PushToast("Failed to update strategy status", ui::kToastError);
                *ctx_.strategiesDirty = true;
                editCellRowId_ = -1; editCellCol_ = StratCol::Check;
            }
        }
        else
        {
            if (!s.enabled)
                ImGui::TextColored(ui::kColorDisabled, "Disabled");
            else
                ImGui::TextColored(ui::StatusColor(s.status), "%s", ui::StatusLabel(s.status));
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Status);
        }

        // Broker
        ImGui::TableNextColumn();
        if (isCellEditing && editCellCol_ == StratCol::Broker)
        {
            int brokerVal = static_cast<int>(s.broker);
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BrokerColor(s.broker));
            if (ImGui::BeginCombo("##broker", BrokerSourceToString(s.broker), ImGuiComboFlags_NoArrowButton))
            {
                ImGui::PopStyleColor();
                for (int i = 0; i < kBrokerSourceCount; ++i)
                {
                    auto src = static_cast<BrokerSource>(i);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::BrokerColor(src));
                    ImGui::Bullet(); ImGui::SameLine();
                    if (ImGui::Selectable(BrokerSourceToString(src), brokerVal == i))
                    {
                        s.broker = src;
                        if (!ctx_.service->UpdateStrategy(s))
                            ctx_.PushToast("Failed to update broker", ui::kToastError);
                        *ctx_.strategiesDirty = true;
                        editCellRowId_ = -1; editCellCol_ = StratCol::Check;
                    }
                    ImGui::PopStyleColor();
                    if (brokerVal == i) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            else
                ImGui::PopStyleColor();
        }
        else
        {
            ImVec4 col = ui::BrokerColor(s.broker);
            ImGui::TextColored(col, "%s %s", ui::BrokerIcon(s.broker), BrokerSourceToString(s.broker));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextColored(col, "%s", BrokerSourceToString(s.broker));
                ImGui::TextDisabled("%s", ui::BrokerDescription(s.broker));
                ImGui::EndTooltip();
            }
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Broker);
        }

        // Age
        ImGui::TableNextColumn();
        {
            int64_t startTime = s.entryDate > 0 ? s.entryDate : s.createdAt;
            int64_t endTime = (!s.IsActive() && s.triggeredAt > 0)
                            ? s.triggeredAt
                            : static_cast<int64_t>(std::time(nullptr));

            if (startTime > 0 && endTime > startTime)
            {
                char ageBuf[64];
                ui::FormatAge(ageBuf, sizeof(ageBuf), endTime - startTime);
                if (s.IsActive())
                    ImGui::TextColored(ui::kColorNeutral, "%s", ageBuf);
                else
                    ImGui::TextDisabled("%s", ageBuf);
            }
            else
                ImGui::TextDisabled("-");
        }
        HandleRowClick(s, {});
    }

    //
    // Both columns answer: "if I double my position at current price, what happens?"
    // Single "Avg" column that shows context-dependent info:
    //
    // Losing (price moved against you):
    //   "▼ $cost  -X%"  — cost to buy same qty and lower your average (avg down)
    //   Tooltip: new avg after doubling, total qty
    //
    // Winning (price moved in your favor):
    //   "▲ $profit  +X%"  — extractable profit (sell shares to recover gains, keep original capital)
    //   Tooltip: shares to sell, remaining position still worth your original investment

    void StrategyTablePanel::DrawAveragingCells(Strategy& s)
    {
        ImGui::TableNextColumn();

        float curPrice = ctx_.getCurrentPrice(s.symbol);
        float qty = s.quantity;
        float entry = s.EffectiveEntryPrice();
        bool hasData = curPrice > 0.f && qty > 0.f && entry > 0.f && s.IsActive();

        if (!hasData)
        {
            ImGui::TextDisabled("-");
            HandleRowClick(s, {});
            return;
        }

        bool isLong  = s.direction == StrategyDirection::Long;
        bool isLosing = isLong ? (curPrice < entry) : (curPrice > entry);
        char pb[32];

        if (isLosing)
        {
            float cost   = qty * curPrice;
            float newAvg = (entry + curPrice) / 2.f;
            float pct    = ((newAvg - entry) / entry) * 100.f;

            ImGui::TextColored(ui::kColorWarning, "- %s  %+.1f%%",
                               FmtPrice(pb, sizeof(pb), cost, s.symbol), pct);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextColored(ui::kColorWarning, "Average Down");
                ImGui::Separator();

                ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "Cost to double:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "%s",
                                   FmtPrice(pb, sizeof(pb), cost, s.symbol));
                ImGui::Spacing();
                ImGui::Text("Buy %.0f more @ %s", qty,
                            FmtPrice(pb, sizeof(pb), curPrice, s.symbol));
                ImGui::Spacing();
                ImGui::Text("Current avg:  %s", FmtPrice(pb, sizeof(pb), entry, s.symbol));
                ImGui::TextColored(ui::kColorBullish,
                                   "New avg:      %s (%+.1f%%)",
                                   FmtPrice(pb, sizeof(pb), newAvg, s.symbol), pct);
                ImGui::Text("Total qty:    %.0f", qty * 2.f);
                ImGui::EndTooltip();
            }
        }
        else
        {
            // ── AVG UP: extractable profit without affecting original capital ─
            // Profit = value at current price minus cost basis
            float basis  = qty * entry;
            float value  = qty * curPrice;
            float profit = isLong ? (value - basis) : (basis - value);
            float pct    = (profit / basis) * 100.f;

            // Shares you can sell to extract the profit, keeping remaining
            // shares still worth your original investment
            float sellQty      = profit / curPrice;
            float remainingQty = qty - sellQty;

            ImGui::TextColored(ui::kColorBullish, "+ %s  %+.1f%%",
                               FmtPrice(pb, sizeof(pb), profit, s.symbol), pct);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextColored(ui::kColorBullish, "Average Up (Take Profit)");
                ImGui::Separator();

                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.f, 1.f), "Extractable profit:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.f, 1.f), "%s",
                                   FmtPrice(pb, sizeof(pb), profit, s.symbol));
                ImGui::Spacing();
                ImGui::Text("Sell %.1f shares @ %s", sellQty,
                            FmtPrice(pb, sizeof(pb), curPrice, s.symbol));
                ImGui::Text("Keep %.1f shares (worth %s = original investment)",
                            remainingQty,
                            FmtPrice(pb, sizeof(pb), basis, s.symbol));
                ImGui::Spacing();
                ImGui::TextDisabled("Your remaining position covers your initial cost.");
                ImGui::TextDisabled("The profit above is free to withdraw.");
                ImGui::EndTooltip();
            }
        }

        HandleRowClick(s, {});
    }


    void StrategyTablePanel::DrawActionCells(
        Strategy& s, int64_t& deleteId, const Strategy*& viewStrat)
    {
        // Notes
        ImGui::TableNextColumn();
        bool isCellEditing = (editCellRowId_ == s.id);
        if (isCellEditing && editCellCol_ == StratCol::Notes)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##notes", cellEditBuf_, sizeof(cellEditBuf_),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                CommitCellEdit(s, StratCol::Notes);
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
            { editCellRowId_ = -1; editCellCol_ = StratCol::Check; }
        }
        else
        {
            if (!s.notes.empty())
                ImGui::TextWrapped("%s", s.notes.c_str());
            else
                ImGui::TextDisabled("-");
            HandleRowClick(s, {});
            HandleCellDblClick(s, StratCol::Notes);
        }

        // Actions
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("View"))
            viewStrat = &s;
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
            deleteId = s.id;
    }


    void StrategyTablePanel::HandleDeferredActions(
        int64_t deleteId, const Strategy* viewStrat)
    {
        if (deleteId > 0)
        {
            selection_.erase(deleteId);
            if (deleteId == *ctx_.selectedStrategyId)
                *ctx_.selectedStrategyId = -1;
            if (deleteId == editCellRowId_)
            { editCellRowId_ = -1; editCellCol_ = StratCol::Check; }

            bool wasActive = false;
            for (auto& s : *ctx_.strategies)
                if (s.id == deleteId && s.IsActive()) { wasActive = true; break; }

            if (wasActive)
            {
                float exitPrice = 0.f;
                for (auto& s : *ctx_.strategies)
                    if (s.id == deleteId) { exitPrice = ctx_.getCurrentPrice(s.symbol); break; }
                if (!ctx_.service->CancelStrategy(deleteId, exitPrice))
                    ctx_.PushToast("Failed to cancel strategy #" + std::to_string(deleteId), ui::kToastError);
            }
            else
            {
                if (!ctx_.service->DeleteStrategy(deleteId))
                    ctx_.PushToast("Failed to delete strategy #" + std::to_string(deleteId), ui::kToastError);
            }
            *ctx_.strategiesDirty = true;
            ctx_.telemetry->strategyDeletes++;
        }

        if (viewStrat)
        {
            float lo = viewStrat->entryPrice, hi = viewStrat->entryPrice;
            if (viewStrat->takeProfit > 0.f) { lo = std::min(lo, viewStrat->takeProfit); hi = std::max(hi, viewStrat->takeProfit); }
            if (viewStrat->stopLoss > 0.f) { lo = std::min(lo, viewStrat->stopLoss); hi = std::max(hi, viewStrat->stopLoss); }

            bool chartOpen = false;
            for (auto& panel : *ctx_.charts)
            {
                if (panel.symbol == viewStrat->symbol)
                {
                    panel.chart.FocusOnPriceRange(lo, hi);
                    chartOpen = true;
                    break;
                }
            }
            if (!chartOpen && ctx_.fetchSymbol)
                ctx_.fetchSymbol(viewStrat->symbol, "1d", "6mo");
        }
    }


    void StrategyTablePanel::HandleRowClick(
        Strategy& s, const std::vector<Strategy*>& filtered)
    {
        if (!ImGui::IsItemClicked(ImGuiMouseButton_Left)) return;

        if (io_->KeyCtrl)
        {
            if (isInSelection_) selection_.erase(s.id);
            else selection_.insert(s.id);
        }
        else if (io_->KeyShift && lastClickedId_ > 0)
        {
            selection_.clear();
            selection_.insert(s.id);
            *ctx_.selectedStrategyId = s.id;
            ctx_.wizard->OpenEdit(s);
            *ctx_.showStrategyWizard = true;
        }
        else
        {
            selection_.clear();
            selection_.insert(s.id);
            *ctx_.selectedStrategyId = s.id;
            ctx_.wizard->OpenEdit(s);
            *ctx_.showStrategyWizard = true;

            for (auto& panel : *ctx_.charts)
            {
                auto* sl = panel.chart.GetStrategyLayer();
                if (!sl) continue;
                if (panel.symbol == s.symbol)
                    sl->StartEditing(s.id);
                else if (sl->GetEditingId() > 0)
                    sl->StopEditing();
            }
        }
        lastClickedId_ = s.id;
    }

    void StrategyTablePanel::HandleCellDblClick(const Strategy& s, StratCol col)
    {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            ImVec2 rMin = ImGui::GetItemRectMin();
            ImVec2 rMax = ImGui::GetItemRectMax();
            ImVec2 mp   = ImGui::GetMousePos();
            if (mp.x >= rMin.x && mp.x <= rMax.x && mp.y >= rMin.y && mp.y <= rMax.y)
                StartCellEdit(s, col);
        }
    }


    void StrategyTablePanel::SortStrategies(std::vector<Strategy*>& ptrs)
    {
        if (sortCol_ == StratCol::Check) return; // None

        auto cmp = [&](const Strategy* a, const Strategy* b) -> bool {
            int result = 0;
            switch (sortCol_)
            {
            case StratCol::Symbol: result = a->symbol.compare(b->symbol); break;
            case StratCol::Type:   result = (int)a->type - (int)b->type; break;
            case StratCol::Dir:    result = (int)a->direction - (int)b->direction; break;
            case StratCol::Entry:  result = (a->entryPrice < b->entryPrice) ? -1 : (a->entryPrice > b->entryPrice) ? 1 : 0; break;
            case StratCol::TP:     result = (a->takeProfit < b->takeProfit) ? -1 : (a->takeProfit > b->takeProfit) ? 1 : 0; break;
            case StratCol::SL:     result = (a->stopLoss < b->stopLoss) ? -1 : (a->stopLoss > b->stopLoss) ? 1 : 0; break;
            case StratCol::Qty:    result = (a->quantity < b->quantity) ? -1 : (a->quantity > b->quantity) ? 1 : 0; break;
            case StratCol::PnL: {
                auto getPnl = [this](const Strategy* s) -> float {
                    if (s->IsActive())
                    {
                        float p = ctx_.getCurrentPrice(s->symbol);
                        return (p > 0.f && s->entryPrice > 0.f) ? s->UnrealizedPnLPercent(p) : 0.f;
                    }
                    if (s->closedPnlPct != 0.f) return s->closedPnlPct;
                    if (s->exitPrice > 0.f && s->entryPrice > 0.f)
                        return s->UnrealizedPnLPercent(s->EffectiveExitPrice());
                    return 0.f;
                };
                float pA = getPnl(a), pB = getPnl(b);
                result = (pA < pB) ? -1 : (pA > pB) ? 1 : 0;
            } break;
            case StratCol::Exit:   result = (a->exitPrice < b->exitPrice) ? -1 : (a->exitPrice > b->exitPrice) ? 1 : 0; break;
            case StratCol::Status: result = (int)a->status - (int)b->status; break;
            case StratCol::Notes:  result = a->notes.compare(b->notes); break;
            default: break;
            }
            return sortAsc_ ? (result < 0) : (result > 0);
        };
        std::sort(ptrs.begin(), ptrs.end(), cmp);
    }


    void StrategyTablePanel::StartCellEdit(const Strategy& strat, StratCol col)
    {
        editCellRowId_ = strat.id;
        editCellCol_   = col;
        cellEditBuf_[0] = '\0';

        switch (col)
        {
        case StratCol::Symbol:   snprintf(cellEditBuf_, sizeof(cellEditBuf_), "%s", strat.symbol.c_str()); break;
        case StratCol::Entry:    cellEditFloat_ = strat.entryPrice; break;
        case StratCol::TP:       cellEditFloat_ = strat.takeProfit; break;
        case StratCol::SL:       cellEditFloat_ = strat.stopLoss; break;
        case StratCol::Qty:      cellEditFloat_ = strat.quantity; break;
        case StratCol::EntryFee: cellEditFloat_ = strat.entryFee; break;
        case StratCol::ExitFee:  cellEditFloat_ = strat.exitFee; break;
        case StratCol::Notes:    snprintf(cellEditBuf_, sizeof(cellEditBuf_), "%s", strat.notes.c_str()); break;
        case StratCol::Broker:   break;
        default: break;
        }
    }

    void StrategyTablePanel::CommitCellEdit(Strategy& strat, StratCol col)
    {
        switch (col)
        {
        case StratCol::Symbol:   strat.symbol     = cellEditBuf_; break;
        case StratCol::Entry:    strat.entryPrice = cellEditFloat_; break;
        case StratCol::TP:       strat.takeProfit = cellEditFloat_; break;
        case StratCol::SL:       strat.stopLoss   = cellEditFloat_; break;
        case StratCol::Qty:      strat.quantity   = cellEditFloat_; break;
        case StratCol::EntryFee: strat.entryFee   = cellEditFloat_; break;
        case StratCol::ExitFee:  strat.exitFee    = cellEditFloat_; break;
        case StratCol::Notes:    strat.notes      = cellEditBuf_; break;
        default: break;
        }

        if (!ctx_.service->UpdateStrategy(strat))
            ctx_.PushToast("Failed to save changes for strategy #" + std::to_string(strat.id), ui::kToastError);
        *ctx_.strategiesDirty = true;
        editCellRowId_ = -1;
        editCellCol_   = StratCol::Check;
        spdlog::info("[Strategy] Cell edit committed for #{}", strat.id);
    }


    static bool ContainsInsensitive(const char* haystack, const char* needle)
    {
        if (!needle[0]) return true;
        std::string h(haystack), n(needle);
        std::transform(h.begin(), h.end(), h.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(n.begin(), n.end(), n.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return h.find(n) != std::string::npos;
    }

    bool StrategyTablePanel::MatchesFilter(const Strategy& strat) const
    {
        if (!filter_.HasAnyFilter()) return true;
        if (filter_.symbolBuf[0] && !ContainsInsensitive(strat.symbol.c_str(), filter_.symbolBuf))
            return false;
        if (filter_.typeFilter >= 0 && static_cast<int>(strat.type) != filter_.typeFilter)
            return false;
        if (filter_.dirFilter >= 0 && static_cast<int>(strat.direction) != filter_.dirFilter)
            return false;
        if (filter_.statusFilter >= 0)
        {
            if (filter_.statusFilter == 4) { if (strat.enabled) return false; }
            else { if (!strat.enabled || static_cast<int>(strat.status) != filter_.statusFilter) return false; }
        }
        if (filter_.entryMin != 0.f && strat.entryPrice < filter_.entryMin) return false;
        if (filter_.entryMax != 0.f && strat.entryPrice > filter_.entryMax) return false;
        if (filter_.tpMin != 0.f && strat.takeProfit < filter_.tpMin) return false;
        if (filter_.tpMax != 0.f && strat.takeProfit > filter_.tpMax) return false;
        if (filter_.slMin != 0.f && strat.stopLoss < filter_.slMin) return false;
        if (filter_.slMax != 0.f && strat.stopLoss > filter_.slMax) return false;
        if (filter_.qtyMin != 0.f && strat.quantity < filter_.qtyMin) return false;
        if (filter_.qtyMax != 0.f && strat.quantity > filter_.qtyMax) return false;
        if (filter_.pnlEnabled)
        {
            float pnl = 0.f;
            if (!strat.IsActive()) pnl = strat.closedPnlPct;
            else if (strat.entryPrice > 0.f)
            {
                float cp = ctx_.getCurrentPrice(strat.symbol);
                if (cp > 0.f) pnl = strat.UnrealizedPnLPercent(cp);
            }
            if (pnl < filter_.pnlMin || pnl > filter_.pnlMax) return false;
        }
        if (filter_.notesBuf[0] && !ContainsInsensitive(strat.notes.c_str(), filter_.notesBuf))
            return false;
        return true;
    }


    void StrategyTablePanel::DrawFilterRow()
    {
        auto& f = filter_;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.14f, 0.18f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 2.f));

        ImGui::TableNextRow();

        // Check: Clear button
        ImGui::TableNextColumn();
        if (f.HasAnyFilter())
        {
            if (ImGui::SmallButton("X##clr"))
                f.Clear();
        }

        // Symbol
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##fSym", f.symbolBuf, sizeof(f.symbolBuf));

        // Type
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##fType", &f.typeFilter, "All\0TP/SL\0Position\0AI\0");
        if (f.typeFilter == 0) f.typeFilter = -1;
        else f.typeFilter--;

        // Direction
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##fDir", &f.dirFilter, "All\0Long\0Short\0");
        if (f.dirFilter == 0) f.dirFilter = -1;
        else f.dirFilter--;

        // Entry, Price, TP, SL, Qty — skip
        for (int i = (int)StratCol::Entry; i <= (int)StratCol::Qty; ++i)
            ImGui::TableNextColumn();

        // EntryFee, ExitFee — skip
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();

        // R:R — skip
        ImGui::TableNextColumn();

        // P/L%
        ImGui::TableNextColumn();
        ImGui::Checkbox("##fPnl", &f.pnlEnabled);

        // Exit — skip
        ImGui::TableNextColumn();

        // Status
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        int stf = f.statusFilter + 1;
        ImGui::Combo("##fStat", &stf, "All\0Active\0TP Hit\0SL Hit\0Cancel\0Disabled\0");
        f.statusFilter = stf - 1;

        // Remaining cols (Broker, Age, Avg, Notes, Actions): skip
        for (int i = (int)StratCol::Broker; i < kStratColCount; ++i)
            ImGui::TableNextColumn();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }


    void StrategyTablePanel::ExportCSV()
    {
        auto dest = pfd::save_file("Export Strategies CSV", "strategies.csv",
            {"CSV Files", "*.csv", "All Files", "*"});

        std::string path = dest.result();
        if (path.empty()) return;

        FILE* f = fopen(path.c_str(), "w");
        if (!f) { spdlog::error("[CSV] Failed to open '{}' for writing", path); return; }

        fprintf(f, "id,symbol,type,direction,entry_price,take_profit,stop_loss,quantity,"
                   "status,priority,parent_id,exit_price,closed_pnl,entry_fee,exit_fee,broker,notes\n");

        for (auto& s : *ctx_.strategies)
        {
            if (!selection_.empty() && selection_.count(s.id) == 0)
                continue;

            std::string escaped = s.notes;
            size_t pos = 0;
            while ((pos = escaped.find('"', pos)) != std::string::npos)
            { escaped.insert(pos, "\""); pos += 2; }

            fprintf(f, "%lld,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%d,%d,%lld,%.4f,%.4f,%.4f,%.4f,%d,\"%s\"\n",
                (long long)s.id, s.symbol.c_str(),
                (int)s.type, (int)s.direction,
                s.entryPrice, s.takeProfit, s.stopLoss, s.quantity,
                (int)s.status, s.priority, (long long)s.parentId,
                s.exitPrice, s.closedPnlPct,
                s.entryFee, s.exitFee, (int)s.broker, escaped.c_str());
        }
        fclose(f);
        spdlog::info("[CSV] Exported to '{}'", path);
    }


    void StrategyTablePanel::ImportCSV()
    {
        auto src = pfd::open_file("Import Strategies CSV", "",
            {"CSV Files", "*.csv", "All Files", "*"});

        auto paths = src.result();
        if (paths.empty()) return;

        FILE* f = fopen(paths[0].c_str(), "r");
        if (!f) { spdlog::error("[CSV] Failed to open '{}'", paths[0]); return; }

        char line[1024];
        int lineNum = 0, imported = 0;

        while (fgets(line, sizeof(line), f))
        {
            lineNum++;
            if (lineNum == 1) continue;

            Strategy s;
            char symbol[128] = "";
            int type = 0, dir = 0, status = 0, priority = 0;
            long long parentId = 0, id = 0;

            int parsed = sscanf(line, "%lld,%127[^,],%d,%d,%f,%f,%f,%f,%d,%d,%lld",
                &id, symbol, &type, &dir,
                &s.entryPrice, &s.takeProfit, &s.stopLoss, &s.quantity,
                &status, &priority, &parentId);

            if (parsed < 7) continue;

            s.symbol    = symbol;
            s.type      = static_cast<StrategyType>(type);
            s.direction = static_cast<StrategyDirection>(dir);
            s.status    = static_cast<StrategyStatus>(status);
            s.priority  = priority;
            s.parentId  = parentId;
            s.createdAt = std::time(nullptr);

            const char* q = strchr(line, '"');
            if (q)
            {
                q++;
                const char* qEnd = strrchr(q, '"');
                if (qEnd && qEnd > q)
                    s.notes = std::string(q, qEnd);
            }

            if (ctx_.service->InsertStrategy(s) > 0) imported++;
        }

        fclose(f);
        *ctx_.strategiesDirty = true;
        spdlog::info("[CSV] Imported {} strategies from '{}'", imported, paths[0]);
    }


    void StrategyTablePanel::ClearCellEditForRow(int64_t rowId)
    {
        if (editCellRowId_ == rowId)
        {
            editCellRowId_ = -1;
            editCellCol_   = StratCol::Check;
        }
    }

    void StrategyTablePanel::SelectSingle(int64_t id)
    {
        selection_.clear();
        selection_.insert(id);
        lastClickedId_ = id;
    }

    void StrategyTablePanel::ClearSelection()
    {
        selection_.clear();
        lastClickedId_ = -1;
    }

} // namespace stnks
