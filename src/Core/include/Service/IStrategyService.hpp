#pragma once

#include <Strategy/Strategy.hpp>
#include <Market/MarketData.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace stnks
{
    // Abstract service interface for the UI.
    //
    // Two implementations:
    //   LocalStrategyService  — embedded server in same process (monolith)
    //   RemoteStrategyService — connects to a running stnks-server via REST
    //
    // All methods are synchronous from the caller's perspective.
    // Async wrappers live in the UI layer (using ThreadRegistry).
    class IStrategyService
    {
    public:
        virtual ~IStrategyService() = default;


        virtual int64_t               InsertStrategy(const Strategy& s) = 0;
        virtual bool                  UpdateStrategy(const Strategy& s) = 0;
        virtual bool                  DeleteStrategy(int64_t id) = 0;
        virtual bool                  CancelStrategy(int64_t id, float exitPrice = 0.f) = 0;
        virtual Strategy              GetStrategy(int64_t id) = 0;
        virtual std::vector<Strategy> GetAllStrategies() = 0;
        virtual std::vector<Strategy> GetActiveStrategies() = 0;
        virtual std::vector<Strategy> GetStrategiesBySymbol(const std::string& symbol) = 0;


        virtual StockQuote FetchQuote(const std::string& symbol,
                                      const std::string& interval = "1d",
                                      const std::string& range    = "6mo") = 0;

        virtual std::vector<SymbolMatch> SearchSymbols(const std::string& query) = 0;


        virtual bool IsConnected() const = 0;
        virtual bool IsMonitoring() const = 0;
        virtual std::string GetServerUrl() const = 0;
    };

} // namespace stnks
