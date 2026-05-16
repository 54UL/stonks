#pragma once

#include <Charts/ChartLayer.hpp>
#include <Charts/CandlestickLayer.hpp>
#include <Charts/VolumeLayer.hpp>
#include <Charts/RSILayer.hpp>
#include <Charts/StrategyLayer.hpp>
#include <Market/MarketData.hpp>

#include <imgui.h>
#include <vector>
#include <memory>
#include <string>
#include <ctime>

namespace stnks
{
    class StockChart
    {
    public:
        StockChart();

        void SetData(const StockQuote& quote);

        template<typename T, typename... Args>
        T& AddLayer(Args&&... args)
        {
            auto layer = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *layer;
            layers_.push_back(std::move(layer));
            return ref;
        }

        void Draw(const char* label = "##StockChart");
        void ResetView();

        // Update strategies displayed on the chart (called from UI each frame)
        void SetStrategies(const std::vector<Strategy>& strategies);

        // Get the strategy layer (may be null if not added yet)
        StrategyLayer* GetStrategyLayer() { return strategyLayer_; }

    private:
        void HandleInput();
        void ResolveFocusedCandle();
        void DrawGrid(ImDrawList* drawList, const ChartViewport& vp);
        void DrawPriceAxis(ImDrawList* drawList, const ChartViewport& vp);
        void DrawTimeAxis(ImDrawList* drawList, const ChartViewport& vp);
        void DrawSyncedCrosshair(ImDrawList* drawList);
        void DrawTooltip();
        void AutoScalePrice();

        StockQuote                                data_;
        ChartViewport                             viewport_;
        std::vector<std::unique_ptr<ChartLayer>>  layers_;
        StrategyLayer* strategyLayer_ = nullptr;  // Cached pointer into layers_

        // Layout regions (computed each frame, used for crosshair sync)
        struct PanelRegion
        {
            ImVec2 origin;
            ImVec2 size;
            ChartLayer* layer = nullptr;  // nullptr = main chart
        };
        std::vector<PanelRegion> panelRegions_;
        float totalChartTop_    = 0.f;
        float totalChartBottom_ = 0.f;
        float chartLeft_        = 0.f;
        float chartWidth_       = 0.f;

        // Interaction state
        bool  isDragging_     = false;
        float dragStartX_     = 0.f;
        int   dragStartIndex_ = 0;
        float dragCandleStep_ = 0.f;

        // Grid helpers
        static float NiceStep(float range, float targetLines);

        // Config
        int   minVisibleCandles_ = 4;
        int   maxVisibleCandles_ = 500;
        float priceMarginPct_    = 0.05f;
        float rightAxisWidth_    = 60.f;
        float zoomSpeed_         = 0.15f;  // 15% per scroll notch

        static constexpr ImU32 kGridMajorColor = IM_COL32(45, 45, 58, 255);
        static constexpr ImU32 kGridMinorColor = IM_COL32(30, 30, 40, 255);
        static constexpr ImU32 kAxisTextColor  = IM_COL32(160, 160, 170, 255);
        static constexpr ImU32 kCrosshairColor = IM_COL32(100, 100, 120, 180);
        static constexpr ImU32 kBgColor        = IM_COL32(18, 18, 24, 255);
    };

} // namespace stnks
