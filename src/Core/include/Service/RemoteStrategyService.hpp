#pragma once

#include <Service/IStrategyService.hpp>
#include <Http/HttpClient.hpp>
#include <string>

namespace stnks
{
    // Remote mode: connects to a running stnks-server via REST API.
    // All calls are synchronous HTTP requests using HttpClient (cpr).
    class RemoteStrategyService : public IStrategyService
    {
    public:
        explicit RemoteStrategyService(HttpClient& http,
                                        const std::string& serverUrl = "http://localhost:8099");

        // ── IStrategyService ─────────────────────────────────────────────────

        int64_t               InsertStrategy(const Strategy& s) override;
        bool                  UpdateStrategy(const Strategy& s) override;
        bool                  DeleteStrategy(int64_t id) override;
        bool                  CancelStrategy(int64_t id) override;
        Strategy              GetStrategy(int64_t id) override;
        std::vector<Strategy> GetAllStrategies() override;
        std::vector<Strategy> GetActiveStrategies() override;
        std::vector<Strategy> GetStrategiesBySymbol(const std::string& symbol) override;

        StockQuote FetchQuote(const std::string& symbol,
                              const std::string& interval = "1d",
                              const std::string& range    = "6mo") override;

        std::vector<SymbolMatch> SearchSymbols(const std::string& query) override;

        bool IsConnected() const override;
        bool IsMonitoring() const override;
        std::string GetServerUrl() const override { return serverUrl_; }

    private:
        // JSON deserialization helpers
        static Strategy              ParseStrategy(const std::string& json);
        static std::vector<Strategy> ParseStrategies(const std::string& json);
        static StockQuote            ParseQuote(const std::string& json);
        static std::vector<SymbolMatch> ParseSymbolMatches(const std::string& json);
        static std::string           StrategyToJson(const Strategy& s);

        HttpClient& http_;
        std::string serverUrl_;
        mutable bool lastConnected_ = false;
    };

} // namespace stnks
