#pragma once

#include <Service/IStrategyService.hpp>
#include <Strategy/StrategyStore.hpp>
#include <Market/MarketService.hpp>
#include <Server/StrategyServer.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>

#include <memory>
#include <thread>
#include <atomic>

namespace stnks
{
    // Monolith mode: runs StrategyServer in a background thread, uses local
    // StrategyStore and MarketService directly. No network involved.
    class LocalStrategyService : public IStrategyService
    {
    public:
        struct Config
        {
            StrategyServer::Config server;
            bool startMonitoring = true;  // Auto-start the server loop
        };

        explicit LocalStrategyService(HttpClient& http, ThreadRegistry& threads);
        explicit LocalStrategyService(HttpClient& http, ThreadRegistry& threads,
                                       const Config& config);
        ~LocalStrategyService() override;

        // Optionally register actions on the embedded server
        StrategyServer& GetServer() { return *server_; }

        // ── IStrategyService ─────────────────────────────────────────────────

        int64_t               InsertStrategy(const Strategy& s) override;
        bool                  UpdateStrategy(const Strategy& s) override;
        bool                  DeleteStrategy(int64_t id) override;
        bool                  CancelStrategy(int64_t id, float exitPrice = 0.f) override;
        Strategy              GetStrategy(int64_t id) override;
        std::vector<Strategy> GetAllStrategies() override;
        std::vector<Strategy> GetActiveStrategies() override;
        std::vector<Strategy> GetStrategiesBySymbol(const std::string& symbol) override;

        StockQuote FetchQuote(const std::string& symbol,
                              const std::string& interval = "1d",
                              const std::string& range    = "6mo") override;

        std::vector<SymbolMatch> SearchSymbols(const std::string& query) override;

        bool IsConnected() const override { return true; }
        bool IsMonitoring() const override;
        std::string GetServerUrl() const override { return "local://embedded"; }

    private:
        std::unique_ptr<StrategyStore>  store_;
        std::unique_ptr<MarketService>  market_;
        std::unique_ptr<StrategyServer> server_;
        std::thread                     serverThread_;
    };

} // namespace stnks
