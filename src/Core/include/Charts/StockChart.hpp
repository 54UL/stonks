#pragma once

#include <Charts/ChartLayer.hpp>
#include <Charts/ChartGrid.hpp>
#include <Charts/CandlestickLayer.hpp>
#include <Charts/VolumeLayer.hpp>
#include <Charts/RSILayer.hpp>
#include <Charts/MACDLayer.hpp>
#include <Charts/VolumeProfileLayer.hpp>
#include <Charts/StrategyLayer.hpp>
#include <Events/GraphEvent.hpp>
#include <Market/MarketData.hpp>

#include <imgui.h>
#include <vector>
#include <memory>
#include <string>
#include <functional>
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
        void DrawIndicatorCombo();
        void ResetView();

        bool suppressHeader = false;

        void SetStrategies(const std::vector<Strategy>& strategies);
        StrategyLayer* GetStrategyLayer() { return strategyLayer_; }
        int GetFocusedCandle() const { return viewport_.focusedCandle; }
        int GetCandleCount() const { return (int)data_.candles.size(); }
        void ScrollToCandle(int candleIdx);
        void FocusOnPriceRange(float priceLo, float priceHi);
        void SetEventMarkers(const std::vector<GraphEvent>& events);

        std::function<void(const std::string& symbol, int candleIdx)> onEventMarkerClicked;
        std::function<void(const std::string& indicatorName)> onIndicatorTearOut;
        std::function<void()> onDuplicateChart;

        const StockQuote& GetData() const { return data_; }
        void SetSharedCrosshair(SharedCrosshair* shared) { sharedCrosshair_ = shared; }

        // Root nodes are separate panels, children are overlaid (merged).
        struct LayerNode
        {
            int              layerIdx = -1;   // Index into layers_
            std::vector<int> children;        // Layer indices merged into this panel
        };
        const std::vector<LayerNode>& GetLayerTree() const { return layerTree_; }
        void ApplyLayerTree();

    private:
        void DrawEventMarkers(ImDrawList* drawList, const ChartViewport& vp);
        void HandleInput();
        void ResolveFocusedCandle();
        void DrawGrid(ImDrawList* drawList, const ChartViewport& vp);
        void DrawPriceAxis(ImDrawList* drawList, const ChartViewport& vp);
        void DrawValueAxis(ImDrawList* drawList, const ChartViewport& vp,
                           float valMin, float valMax);
        void DrawTimeAxis(ImDrawList* drawList, const ChartViewport& vp);
        void DrawSyncedCrosshair(ImDrawList* drawList);
        void DrawTooltip();
        void AutoScalePrice();

        StockQuote                                data_;
        ChartViewport                             viewport_;
        std::vector<std::unique_ptr<ChartLayer>>  layers_;
        StrategyLayer* strategyLayer_ = nullptr;

        struct PanelRegion
        {
            ImVec2 origin;
            ImVec2 size;
            ChartLayer* layer = nullptr;
            float valMin = 0.f;
            float valMax = 0.f;
            bool  hasRange = false;
        };
        std::vector<PanelRegion> panelRegions_;
        float totalChartTop_    = 0.f;
        float totalChartBottom_ = 0.f;
        float chartLeft_        = 0.f;
        float chartWidth_       = 0.f;

        // Divider drag state
        int         dividerDragIdx_   = -1;
        ChartLayer* dividerDragLayer_ = nullptr;

        // Interaction state
        bool  isDragging_     = false;
        float dragStartX_     = 0.f;
        int   dragStartIndex_ = 0;
        float dragCandleStep_ = 0.f;
        bool  yLocked_        = false;  // True when user manually panned Y axis

        // Grid helpers
        static float NiceStep(float range, float targetLines);

        // Config
        int   minVisibleCandles_ = 4;
        int   maxVisibleCandles_ = 500;
        float priceMarginPct_    = 0.05f;
        float rightAxisWidth_    = 60.f;
        float zoomSpeed_         = 0.15f;  // 15% per scroll notch

        static constexpr ImU32 kGridMajorColor = IM_COL32(42, 48, 65, 200);
        static constexpr ImU32 kGridMinorColor = IM_COL32(30, 34, 48, 130);
        static constexpr ImU32 kAxisTextColor  = IM_COL32(145, 155, 175, 255);
        static constexpr ImU32 kCrosshairColor = IM_COL32(90, 110, 140, 160);
        static constexpr ImU32 kBgColor        = IM_COL32(14, 16, 22, 255);

        std::vector<GraphEvent> eventMarkers_;
        int highlightCandleIdx_ = -1;
        float highlightTimer_   = 0.f;

        SharedCrosshair* sharedCrosshair_ = nullptr;

        std::vector<LayerNode> layerTree_;
        bool layerTreeDirty_ = true;

        struct LayerOriginal { std::string name; float height; };
        std::vector<LayerOriginal> originalHeights_;
        bool heightsSaved_ = false;

        int FindParentRoot(int layerIdx) const;
        bool IsRootLayer(int layerIdx) const;
        void DetachFromTree(int layerIdx);
        void RebuildDefaultTree();
    };

} // namespace stnks
