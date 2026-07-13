#pragma once

#include <Charts/StockChart.hpp>
#include <Charts/Timeframes.hpp>
#include <Market/MarketData.hpp>
#include <Market/MarketHours.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>
#include <string>
#include <functional>
#include <cstring>
#include <ctime>

namespace stnks
{
    struct DetachedChart
    {
        std::string windowId;
        std::string symbol;

        StockQuote  quote;
        StockChart  chart;
        bool        layersCreated = false;
        bool        open          = true;
        bool        refreshing    = false;
        float       refreshTimer  = 30.f;

        std::string focusedIndicator;

        char  searchBuf[64]   = "";
        bool  searchDirty     = false;

        std::function<void(int, const std::string&)> onSymbolChanged;
        std::function<void(StrategyLayer&, const std::string&)> onWireStrategyLayer;
        std::function<void(StockChart&, const std::string&)> onWireTearOut;
        std::function<void(int, const std::string&, const char*, const char*)> onTimeframeChanged;

        int timeframeIdx = 7;

        struct SourceInfo
        {
            const char* name     = nullptr;
            bool        realtime = false;
            int         delaySec = 0;
            const char* brokerName = nullptr; // non-null if RT broker active
        };
        std::function<SourceInfo()> getSourceInfo;

        static int nextId_;
        int        id = 0;

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

            if (focusIndicator.empty())                               dc.viewModeIdx = 0;
            else if (focusIndicator == "Candles")                     dc.viewModeIdx = 1;
            else if (focusIndicator == "Volume")                      dc.viewModeIdx = 2;
            else if (focusIndicator.find("RSI") != std::string::npos) dc.viewModeIdx = 3;
            else if (focusIndicator.find("MACD") != std::string::npos) dc.viewModeIdx = 4;

            return dc;
        }

        std::string GetTitle() const
        {
            std::string sym = symbol.empty() ? "Chart" : symbol;
            if (focusedIndicator.empty())
                return sym + " (Detached)" + windowId;
            return sym + " - " + focusedIndicator + windowId;
        }

        void EnsureLayers()
        {
            if (layersCreated) return;
            layersCreated = true;

            chart.suppressHeader = true;

            StrategyLayer* sl = nullptr;

            if (focusedIndicator.empty())
            {
                chart.AddLayer<CandlestickLayer>();
                sl = &chart.AddLayer<StrategyLayer>();
                chart.AddLayer<VolumeLayer>();
                chart.AddLayer<RSILayer>();
                chart.AddLayer<MACDLayer>();
                chart.AddLayer<VolumeProfileLayer>();
            }
            else
            {
                sl = &chart.AddLayer<StrategyLayer>();

                auto addFocused = [&](auto& layer) {
                    layer.height  = 0.f;
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
                    chart.AddLayer<CandlestickLayer>();
                    chart.AddLayer<VolumeLayer>();
                    chart.AddLayer<RSILayer>();
                    chart.AddLayer<MACDLayer>();
                    chart.AddLayer<VolumeProfileLayer>();
                }
            }

            chart.SetData(quote);

            if (sl && onWireStrategyLayer)
                onWireStrategyLayer(*sl, symbol);

            if (onWireTearOut)
                onWireTearOut(chart, symbol);
        }

        static constexpr const char* kViewModes[] = {
            "Full", "Candles", "Volume", "RSI", "MACD"
        };
        int viewModeIdx = 0;

        bool Draw()
        {
            if (!open) return false;

            EnsureLayers();

            ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse;

            std::string title = GetTitle();
            ImGui::Begin(title.c_str(), &open, flags);

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

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.f);
            if (ImGui::Combo("##dcView", &viewModeIdx, kViewModes, IM_ARRAYSIZE(kViewModes)))
            {
                switch (viewModeIdx)
                {
                case 0: SetFocusedIndicator(""); break;
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
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset"))
                    chart.ResetView();
            }

            DrawTimeframeBar();

            if (!quote.candles.empty())
            {
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

    private:
        void DrawTimeframeBar()
        {
            for (int tf = 0; tf < kTimeframeCount; ++tf)
            {
                if (tf > 0) ImGui::SameLine(0.f, 2.f);
                bool selected = (timeframeIdx == tf);

                if (selected)
                {
                    ImVec4 btnCol = kTimeframes[tf].realtime
                        ? ImVec4(0.15f, 0.55f, 0.3f, 1.f)
                        : ImVec4(0.2f, 0.35f, 0.6f, 1.f);
                    ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
                }
                if (ImGui::SmallButton(kTimeframes[tf].label))
                {
                    if (timeframeIdx != tf)
                    {
                        timeframeIdx = tf;
                        quote = StockQuote{};
                        chart = StockChart{};
                        layersCreated = false;
                        searchDirty = true;

                        if (onTimeframeChanged)
                        {
                            const char* interval = kTimeframes[tf].realtime
                                ? "1m" : kTimeframes[tf].interval;
                            onTimeframeChanged(id, symbol, interval, kTimeframes[tf].range);
                        }
                    }
                }
                if (selected)
                    ImGui::PopStyleColor();
            }

            DrawFreshnessInfo();
        }

        void DrawFreshnessInfo()
        {
            if (quote.candles.empty() || quote.fetchedAt <= 0) return;

            int64_t now = (int64_t)std::time(nullptr);
            int64_t age = now - quote.fetchedAt;
            int64_t candleAge = now - quote.candles.back().timestamp;

            char ageBuf[16], candleBuf[16];
            ui::FormatDuration(ageBuf, sizeof(ageBuf), age);
            ui::FormatDuration(candleBuf, sizeof(candleBuf), candleAge);

            ImVec4 color = ui::FreshnessColor((float)age);

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 280.f);
            ImGui::TextColored(color, "Data: %s ago", ageBuf);
            ImGui::SameLine();
            ImGui::TextDisabled("| Candle: %s ago", candleBuf);

            if (getSourceInfo)
            {
                auto si = getSourceInfo();
                if (kTimeframes[timeframeIdx].realtime)
                {
                    ImGui::SameLine();
                    if (si.brokerName)
                        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.f), "(RT: %s)", si.brokerName);
                    else
                        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.2f, 1.f), "(RT: %s)",
                            si.name ? si.name : "none");
                }
                else if (si.name && !si.realtime)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s ~%dm)", si.name, si.delaySec / 60);
                }
            }

            if (!symbol.empty())
            {
                MarketState mktState = MarketHours::GetState(symbol);
                MarketType mktType = MarketHours::ClassifySymbol(symbol);
                ImVec4 stateCol;
                switch (mktState)
                {
                case MarketState::Open:       stateCol = ImVec4(0.3f, 0.85f, 0.4f, 1.f); break;
                case MarketState::PreMarket:
                case MarketState::AfterHours: stateCol = ImVec4(0.9f, 0.75f, 0.2f, 1.f); break;
                default:                      stateCol = ImVec4(0.5f, 0.5f, 0.5f, 1.f); break;
                }
                ImGui::SameLine();
                ImGui::TextColored(stateCol, "[%s %s]",
                    MarketHours::MarketTypeToString(mktType),
                    MarketHours::StateToString(mktState));
            }
        }

    public:

        void UpdateData(const StockQuote& newQuote)
        {
            quote = newQuote;
            searchDirty = false;
            refreshing  = false;
            if (layersCreated)
                chart.SetData(quote);
        }

        void ChangeSymbol(const std::string& newSymbol, const StockQuote& newQuote)
        {
            symbol = newSymbol;
            quote  = newQuote;
            searchDirty = false;
            std::strncpy(searchBuf, newSymbol.c_str(), sizeof(searchBuf) - 1);
            searchBuf[sizeof(searchBuf) - 1] = '\0';

            chart = StockChart{};
            layersCreated = false;
        }

        void SetFocusedIndicator(const std::string& indicator)
        {
            focusedIndicator = indicator;
            chart = StockChart{};
            layersCreated = false;
        }
    };

} // namespace stnks
