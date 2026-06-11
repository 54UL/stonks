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

    private:
        UIContext& ctx_;

        void DrawEventRow(GraphEvent& ev, int evIdx);
        void DrawActionButtons(const GraphEvent& ev);
    };

} // namespace stnks
