#pragma once

#include <Charts/ChartLayer.hpp>
#include <Strategy/Strategy.hpp>
#include <imgui.h>

namespace stnks
{
    // Result of gizmo interaction for a single frame
    struct StrategyInteraction
    {
        bool modified  = false;  // Prices changed via drag
        bool confirmed = false;  // User clicked confirm
        bool cancelled = false;  // User clicked cancel / delete
    };

    // Base interface for strategy chart visualization.
    // Each strategy type (TP/SL, trailing stop, etc.) implements this
    // to provide its own rendering and interactive gizmos.
    class IStrategyRenderer
    {
    public:
        virtual ~IStrategyRenderer() = default;

        // Human-readable name for context menus
        virtual const char* TypeName() const = 0;

        // Draw the strategy visualization (zones, lines, labels, status badges).
        // Called for every visible strategy each frame.
        virtual void Draw(ImDrawList* drawList, const ChartViewport& vp,
                          const Strategy& strategy, bool editing) = 0;

        // Draw interactive gizmos and handle drag input.
        // Called only for the strategy currently being edited.
        // May modify strategy prices in-place during drag.
        virtual StrategyInteraction HandleGizmos(ImDrawList* drawList, const ChartViewport& vp,
                                                 Strategy& strategy) = 0;
    };

} // namespace stnks
