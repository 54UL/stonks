#pragma once

#include <UI/UIContext.hpp>
#include <Events/GraphEventService.hpp>
#include <imgui.h>

namespace stnks
{
    class GraphEventsPanel
    {
    public:
        explicit GraphEventsPanel(UIContext& ctx) : ctx_(ctx) {}
        void Draw(bool* open);

        // Draw only the content (no Begin/End wrapper) — for embedding in tabbed panels
        void DrawContent();

    private:
        UIContext& ctx_;

        void DrawEventRow(GraphEvent& ev, int evIdx);
        void DrawActionButtons(const GraphEvent& ev);
    };

} // namespace stnks
