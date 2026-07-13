#pragma once

#include <Strategy/Strategy.hpp>
#include <Service/IStrategyService.hpp>
#include <Market/MarketService.hpp>
#include <Charts/StrategyWizard.hpp>
#include <Charts/StockChart.hpp>
#include <Charts/Timeframes.hpp>
#include <Charts/DetachedChart.hpp>
#include <Charts/ChartLayer.hpp>
#include <Events/GraphEventService.hpp>
#include <Signals/MarketSignalService.hpp>
#include <AI/IMarketAnalyzer.hpp>
#include <AI/ClaudeAnalyzer.hpp>
#include <News/NewsService.hpp>
#include <Engine.hpp>
#include <Broker/IBrokerConnector.hpp>
#include <UI/UIConstants.hpp>
#include <imgui.h>

#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdlib>
#include <fstream>

namespace stnks
{
    struct EnvOverrides
    {
        struct Entry
        {
            std::string value;
            bool        overridden = false;
            bool        isSecret   = false;
        };
        std::unordered_map<std::string, Entry> entries;

        std::string Get(const std::string& key) const
        {
            auto it = entries.find(key);
            if (it != entries.end() && it->second.overridden)
                return it->second.value;
            const char* env = std::getenv(key.c_str());
            return env ? env : "";
        }

        bool IsSet(const std::string& key) const
        {
            auto it = entries.find(key);
            if (it != entries.end() && it->second.overridden)
                return !it->second.value.empty();
            return std::getenv(key.c_str()) != nullptr;
        }

        bool SaveToFile(const std::string& path) const
        {
            std::ofstream out(path);
            if (!out.is_open()) return false;

            out << "# Auto-generated — do not edit while app is running\n";
            for (auto& [key, entry] : entries)
            {
                std::string val = Get(key);
                if (!val.empty())
                    out << key << "=" << val << "\n";
            }
            return out.good();
        }

        bool LoadFromFile(const std::string& path)
        {
            std::ifstream in(path);
            if (!in.is_open()) return false;

            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#') continue;
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;

                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);

                auto it = entries.find(key);
                if (it != entries.end())
                {
                    it->second.value = val;
                    it->second.overridden = true;
                }
            }
            return true;
        }
    };

    struct SystemToggles
    {
        bool ai              = true;
        bool news            = true;
        bool strategyMonitor = true;
        bool graphEvents     = true;
        bool binance         = false;
        bool metaTrader      = false;
    };

    struct Toast
    {
        std::string message;
        ImVec4      color;
        float       lifetime = 5.f;
        float       maxLife  = 5.f;
    };

    struct ChartPanelData
    {
        std::string  symbol;
        StockQuote   quote;
        StockChart   chart;
        bool         loading      = false;
        bool         refreshing   = false;
        bool         open         = true;
        int          timeframeIdx = kDefaultTimeframe;
        float        refreshTimer = 0.f;
    };

    struct Telemetry
    {
        int64_t marketFetches   = 0;
        int64_t strategySaves   = 0;
        int64_t strategyDeletes = 0;
        int64_t chartRefreshes  = 0;
        float   uptimeSec       = 0.f;
        float   avgFrameMs      = 0.f;
    };

    struct PortfolioSample
    {
        float timestamp = 0.f;
        float usdValue  = 0.f;
        float mxnValue  = 0.f;
    };

    struct PendingStrategyView
    {
        bool active = false;
        std::string symbol;
        float priceLo = 0.f;
        float priceHi = 0.f;
    };

    struct UIContext
    {
        Engine*              engine          = nullptr;
        IStrategyService*    service         = nullptr;
        MarketService*       marketService   = nullptr;
        NewsService*         newsService     = nullptr;
        ClaudeAnalyzer*      analyzer        = nullptr;

        std::vector<Strategy>*  strategies   = nullptr;
        bool*                   strategiesDirty = nullptr;

        std::vector<ChartPanelData>* charts         = nullptr;
        std::vector<DetachedChart>*  detachedCharts  = nullptr;
        StrategyWizard*       wizard         = nullptr;

        GraphEventService*    graphEvents    = nullptr;
        MarketSignalService*  signalService  = nullptr;

        std::vector<MarketInsight>* recommendations = nullptr;
        std::vector<MarketInsight>* warnings        = nullptr;
        std::vector<AIOperation>*   operations      = nullptr;
        bool*                       insightsLoading = nullptr;
        float*                      insightRefreshTimer = nullptr;
        float*                      insightRefreshInterval = nullptr;

        std::vector<Toast>*   toasts         = nullptr;
        Telemetry*            telemetry      = nullptr;
        std::vector<PortfolioSample>* portfolioHistory = nullptr;

        int64_t*              selectedStrategyId = nullptr;
        int64_t*              hoveredStrategyId  = nullptr;
        bool*                 showStrategyWizard = nullptr;
        bool*                 showServerLauncher = nullptr;
        SharedCrosshair*      sharedCrosshair = nullptr;

        bool*                 liveTradingEnabled = nullptr;
        bool*                 aiAutoTrade        = nullptr;
        BrokerSource*         defaultBroker  = nullptr;
        std::string*          lastSelectedSource = nullptr;
        ui::EventTimeRange*   eventTimeRange = nullptr;

        SystemToggles*        systemToggles  = nullptr;
        EnvOverrides*         envOverrides   = nullptr;
        float*                marketRefreshInterval = nullptr;
        float*                priceCacheInterval    = nullptr;

        std::function<float(const std::string&)> getCurrentPrice;
        std::function<void(const std::string&, const char*, const char*)> fetchSymbol;
        std::function<void()> refreshInsights;
        std::function<bool(const std::string&, BrokerSource, OrderSide, float, float)> executeOrder;
        std::function<void(const std::string& serverUrl)> connectToServer;
        std::function<void(const std::string& brokerName)> wireBroker;
        std::function<void()> saveEnv;

        void PushToast(const std::string& msg, const ImVec4& color, float duration = 5.f)
        {
            if (toasts)
                toasts->push_back({msg, color, duration, duration});
        }
    };

} // namespace stnks
