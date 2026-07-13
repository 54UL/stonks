#pragma once

#include <UI/UIContext.hpp>
#include <AI/IMarketAnalyzer.hpp>
#include <imgui.h>

namespace stnks
{
    class RecommendationsPanel
    {
    public:
        explicit RecommendationsPanel(UIContext& ctx) : ctx_(ctx) {}
        void Draw(bool* open);
    private:
        UIContext& ctx_;
    };

    class MarketWarningsPanel
    {
    public:
        explicit MarketWarningsPanel(UIContext& ctx) : ctx_(ctx) {}
        void Draw(bool* open);

        // Draw only the content (no Begin/End wrapper) — for embedding in tabbed panels
        void DrawContent();
    private:
        UIContext& ctx_;
    };

    class AIOperationsPanel
    {
    public:
        explicit AIOperationsPanel(UIContext& ctx) : ctx_(ctx) {}
        void Draw(bool* open);
    private:
        UIContext& ctx_;
    };

} // namespace stnks
