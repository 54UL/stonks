#pragma once

#include <Service/IStrategyService.hpp>
#include <Net/UdpMarketFeed.hpp>
#include <Http/HttpClient.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace stnks
{
    // Remote mode: connects to a running stnks-server via REST API.
    // Strategy CRUD via HTTP. Market data streamed via ENet for low latency.
    // Heartbeat handled by ENet connection state (no separate HTTP ping needed).
    class RemoteStrategyService : public IStrategyService
    {
    public:
        explicit RemoteStrategyService(HttpClient& http,
                                        const std::string& serverUrl = "http://localhost:8099");
        ~RemoteStrategyService() override;

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

        bool IsConnected() const override;
        bool IsMonitoring() const override;
        std::string GetServerUrl() const override { return serverUrl_; }

        // ── ENet market feed ─────────────────────────────────────────────────

        // Get real-time price from ENet feed (0 if no data yet)
        float GetLivePrice(const std::string& symbol) const;

        // Network state and latency
        NetState GetNetState() const { return feedClient_.GetState(); }
        float GetLatencyMs() const { return feedClient_.GetLatencyMs(); }

        // Server telemetry (from /api/status polling)
        struct ServerTelemetry
        {
            int64_t requestsServed = 0;
            int64_t ticksBroadcast = 0;
            int     clients        = 0;
            int     uptimeSec      = 0;
        };
        ServerTelemetry GetServerTelemetry() const
        {
            ServerTelemetry t;
            t.requestsServed = cachedServerRequests_.load();
            t.ticksBroadcast = cachedServerTicks_.load();
            t.clients        = cachedServerClients_.load();
            t.uptimeSec      = cachedServerUptime_.load();
            return t;
        }

        // Access the feed client for advanced use
        MarketFeedClient& GetFeedClient() { return feedClient_; }
        const MarketFeedClient& GetFeedClient() const { return feedClient_; }

    private:
        // JSON deserialization helpers
        static Strategy              ParseStrategy(const std::string& json);
        static std::vector<Strategy> ParseStrategies(const std::string& json);
        static StockQuote            ParseQuote(const std::string& json);
        static std::vector<SymbolMatch> ParseSymbolMatches(const std::string& json);
        static std::string           StrategyToJson(const Strategy& s);

        // Extract host from serverUrl_ (e.g. "http://localhost:8099" → "localhost")
        std::string ExtractHost() const;

        // Background status polling
        void PollStatusAsync();

        HttpClient& http_;
        std::string serverUrl_;

        // ENet market feed client (handles heartbeat + market data)
        mutable MarketFeedClient feedClient_;

        // Cached monitoring status (refreshed async, never blocks UI)
        mutable std::atomic<bool> cachedMonitoring_{false};
        mutable std::atomic<bool> statusPollInFlight_{false};
        mutable std::chrono::steady_clock::time_point lastStatusPoll_{};

        // Cached server telemetry
        mutable std::atomic<int64_t> cachedServerRequests_{0};
        mutable std::atomic<int64_t> cachedServerTicks_{0};
        mutable std::atomic<int>     cachedServerClients_{0};
        mutable std::atomic<int>     cachedServerUptime_{0};
    };

} // namespace stnks
