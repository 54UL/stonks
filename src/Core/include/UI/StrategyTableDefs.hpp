#pragma once

#include <imgui.h>

namespace stnks
{
    // Column identifiers for the strategy table.
    // Used for cell editing, sorting, filter row, and column setup.
    enum class StratCol : int
    {
        Check    = 0,
        Symbol   = 1,
        Type     = 2,
        Dir      = 3,
        Entry    = 4,
        Price    = 5,
        TP       = 6,
        SL       = 7,
        Qty      = 8,
        EntryFee = 9,
        ExitFee  = 10,
        RR       = 11,
        PnL      = 12,
        Exit     = 13,
        Status   = 14,
        Broker   = 15,
        Age      = 16,
        AvgDown  = 17,
        AvgUp    = 18,
        Notes    = 19,
        Actions  = 20
    };

    struct StratColDef
    {
        const char*           name;
        float                 width;
        ImGuiTableColumnFlags flags;
        bool                  sortable;
    };

    // Column descriptors — order MUST match StratCol enum.
    // flags already include WidthFixed where appropriate.
    inline const StratColDef kStratColumns[] = {
        // name       width  flags                                                                          sortable
        {"##chk",      18.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, false},
        {"Symbol",     90.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,          true },
        {"Type",       55.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Dir",        50.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Entry",      75.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Price",      75.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"TP",         75.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"SL",         75.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Qty",        50.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"E.Fee",      60.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"X.Fee",      60.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"R:R",        45.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"P/L%",       60.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Exit",       70.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Status",     70.f, ImGuiTableColumnFlags_WidthFixed,                                              true },
        {"Broker",     70.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"Age",       110.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"Avg Down",   70.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"Avg Up",     70.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
        {"Notes",       0.f, ImGuiTableColumnFlags_WidthStretch,                                            true },
        {"",           80.f, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,               false},
    };

    // Derived from array — no hardcoded count anywhere
    inline constexpr int kStratColCount = sizeof(kStratColumns) / sizeof(kStratColumns[0]);

} // namespace stnks
