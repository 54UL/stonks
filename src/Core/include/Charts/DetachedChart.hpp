#pragma once

#include <Charts/StockChart.hpp>
#include <Market/MarketData.hpp>
#include <imgui.h>
#include <string>
#include <functional>
#include <cstring>

namespace stnks
{
    // A self-contained dockable chart window that can be torn out of the main
    // Stock Charts panel. Each DetachedChart has its own StockChart instance,
    // data, timeline, symbol search, and full set of indicators.
    //
    // Created dynamically when the user drags an indicator out of a chart panel
    // or duplicates a chart view. Rendered as an ImGui window with docking enabled
    // so it can float, dock into the main layout, or become an OS-native window.
    //
    // Each detached chart is fully independent — changing symbol in main panel
    // does not affect detached charts.
    struct DetachedChart
    {
        // Unique window ID (stable across title changes via ###)
        std::string windowId;

        // The symbol this chart shows (independent from main panel)
        std::string symbol;

        // Chart data and renderer
        StockQuote  quote;
        StockChart  chart;
        bool        layersCreated = false;
        bool        open          = true;
        bool        refreshing    = false;   // Background data refresh in progress
        float       refreshTimer  = 30.f;    // Countdown to next auto-refresh

        // Which indicator to focus (empty = show all)
        std::string focusedIndicator;

        // Symbol search bar state
        char  searchBuf[64]   = "";
        bool  searchDirty     = false;  // True when user typed and needs fetch

        // Callback: fired when this detached chart requests data for a new symbol.
        // UI should hook this to call marketService_->FetchQuoteAsync().
        // Args: (detachedChartId, symbol)
        std::function<void(int, const std::string&)> onSymbolChanged;

        // Callback: wires strategy layer callbacks after layers are created.
        // Args: (strategyLayer, symbol)
        std::function<void(StrategyLayer&, const std::string&)> onWireStrategyLayer;

        // Callback: wires tear-out/duplicate callbacks on the StockChart.
        // Args: (chart, symbol)
        std::function<void(StockChart&, const std::string&)> onWireTearOut;

        // Unique counter for generating window IDs
        static int nextId_;
        int        id = 0;

        // Create a detached chart for a symbol, optionally focusing one indicator
        static DetachedChart Create(const std::string& sym,
                                    const StockQuote& data,
                                    const std::string& focusIndicator = "")
        {
            DetachedChart dc;
            dc.id     = nextId_++;
            dc.symbol = sym;
            dc.quote  = data;
            dc.focusedIndicator = focusIndicator;
            dc.windowId = "###detached_" + std::to_string(dc.id);

            std::strncpy(dc.searchBuf, sym.c_str(), sizeof(dc.searchBuf) - 1);
            dc.searchBuf[sizeof(dc.searchBuf) - 1] = '\0';

            // Sync view mode index with focused indicator
            if (focusIndicator.empty())                               dc.viewModeIdx = 0;
            else if (focusIndicator == "Candles")                     dc.viewModeIdx = 1;
            else if (focusIndicator == "Volume")                      dc.viewModeIdx = 2;
            else if (focusIndicator.find("RSI") != std::string::npos) dc.viewModeIdx = 3;
            else if (focusIndicator.find("MACD") != std::string::npos) dc.viewModeIdx = 4;

            return dc;
        }

        // Get display title (changes with symbol, but window ID is stable)
        std::string GetTitle() const
        {
            std::string sym = symbol.empty() ? "Chart" : symbol;
            if (focusedIndicator.empty())
                return sym + " (Detached)" + windowId;
            return sym + " - " + focusedIndicator + windowId;
        }

        // Initialize layers (call once after creation, deferred so data is ready)
        void EnsureLayers()
        {
            if (layersCreated) return;
            layersCreated = true;

            StrategyLayer* sl = nullptr;

            if (focusedIndicator.empty())
            {
                // Full chart: candlestick main + all indicators stacked
                chart.AddLayer<CandlestickLayer>();
                sl = &chart.AddLayer<StrategyLayer>();
                chart.AddLayer<VolumeLayer>();
                chart.AddLayer<RSILayer>();
                chart.AddLayer<MACDLayer>();
            }
            else
            {
                // Focused view: the requested indicator becomes the main chart area.
                sl = &chart.AddLayer<StrategyLayer>();

                auto addFocused = [&](auto& layer) {
                    layer.height  = 0.f;  // Render in main area
                    layer.visible = true;
                };

                if (focusedIndicator == "Volume")
                {
                    addFocused(chart.AddLayer<VolumeLayer>());
                }
                else if (focusedIndicator.find("RSI") != std::string::npos)
                {
                    addFocused(chart.AddLayer<RSILayer>());
                }
                else if (focusedIndicator.find("MACD") != std::string::npos)
                {
                    addFocused(chart.AddLayer<MACDLayer>());
                }
                else if (focusedIndicator == "Candles")
                {
                    chart.AddLayer<CandlestickLayer>();
                }
                else
                {
                    // Unknown indicator — fall back to full chart
                    chart.AddLayer<CandlestickLayer>();
                    chart.AddLayer<VolumeLayer>();
                    chart.AddLayer<RSILayer>();
                    chart.AddLayer<MACDLayer>();
                }
            }

            chart.SetData(quote);

            // Wire strategy layer callbacks so detached charts are fully functional
            if (sl && onWireStrategyLayer)
                onWireStrategyLayer(*sl, symbol);

            // Wire tear-out/duplicate callbacks
            if (onWireTearOut)
                onWireTearOut(chart, symbol);
        }

        // Chart view mode names (for the swap combo)
        static constexpr const char* kViewModes[] = {
            "Full", "Candles", "Volume", "RSI", "MACD"
        };
        int viewModeIdx = 0; // 0=Full, 1=Candles, 2=Volume, 3=RSI, 4=MACD

        // Render the detached chart window. Returns false if closed.
        bool Draw()
        {
            if (!open) return false;

            EnsureLayers();

            ImGui::SetNextWindowSize(ImVec2(640, 440), ImGuiCond_FirstUseEver);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse;

            std::string title = GetTitle();
            ImGui::Begin(title.c_str(), &open, flags);

            // --- Toolbar: symbol search + view mode swap + indicators ---
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(120.f);
            if (ImGui::InputText("##dcSearch", searchBuf, sizeof(searchBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                std::string newSym(searchBuf);
                if (!newSym.empty() && newSym != symbol)
                {
                    quote = StockQuote{};
                    quote.symbol = newSym;
                    symbol = newSym;
                    searchDirty = true;
                    if (onSymbolChanged)
                        onSymbolChanged(id, symbol);
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Type symbol and press Enter to change");

            // View mode swap combo
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.f);
            if (ImGui::Combo("##dcView", &viewModeIdx, kViewModes, IM_ARRAYSIZE(kViewModes)))
            {
                // Map combo index to focused indicator string
                switch (viewModeIdx)
                {
                case 0: SetFocusedIndicator(""); break;          // Full
                case 1: SetFocusedIndicator("Candles"); break;
                case 2: SetFocusedIndicator("Volume"); break;
                case 3: SetFocusedIndicator("RSI"); break;
                case 4: SetFocusedIndicator("MACD"); break;
                }
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (!quote.candles.empty())
            {
                chart.DrawIndicatorCombo();
                chart.Draw(("##dc_" + std::to_string(id)).c_str());
            }
            else if (searchDirty)
            {
                ImGui::TextDisabled("Loading %s...", symbol.c_str());
            }
            else
            {
                ImGui::TextDisabled("No data — type a symbol above");
            }

            ImGui::PopID();
            ImGui::End();
            return open;
        }

        // Update chart data (called when fetch completes)
        void UpdateData(const StockQuote& newQuote)
        {
            quote = newQuote;
            searchDirty = false;
            refreshing  = false;
            if (layersCreated)
                chart.SetData(quote);
        }

        // Reset chart for a new symbol (clears layers so they get re-created)
        void ChangeSymbol(const std::string& newSymbol, const StockQuote& newQuote)
        {
            symbol = newSymbol;
            quote  = newQuote;
            searchDirty = false;
            std::strncpy(searchBuf, newSymbol.c_str(), sizeof(searchBuf) - 1);
            searchBuf[sizeof(searchBuf) - 1] = '\0';

            // Reset chart completely for new symbol (layers re-created and re-wired via EnsureLayers)
            chart = StockChart{};
            layersCreated = false;
        }

        // Change which indicator is the main (center) chart panel.
        // Pass empty string for full chart with all indicators.
        void SetFocusedIndicator(const std::string& indicator)
        {
            focusedIndicator = indicator;
            chart = StockChart{};
            layersCreated = false;
            // EnsureLayers() on next Draw() will re-create with the new focus
        }
    };

} // namespace stnks
