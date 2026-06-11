#pragma once

#include <UI/UIContext.hpp>
#include <Signals/MarketSignalService.hpp>
#include <imgui.h>

namespace stnks
{
    class MarketSignalsPanel
    {
    public:
        explicit MarketSignalsPanel(UIContext& ctx);
        void Draw(bool* open);

    private:
        UIContext& ctx_;
        int filterTab_ = 0;

        void DrawToolbar(int actionable, size_t total);
        void DrawFilterTabs();
        void DrawSignalItem(MarketSignal& sig);
        void DrawSignalResources(const MarketSignal& sig);
        void DrawSignalActions(MarketSignal& sig);
    };

} // namespace stnks
