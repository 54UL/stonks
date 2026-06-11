#pragma once

#include <Strategy/StrategyStore.hpp>
#include <Market/MarketService.hpp>
#include <Server/StrategyServer.hpp>
#include <Net/UdpMarketFeed.hpp>

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace httplib { class Server; }

namespace stnks
{
    // REST API server for remote UI connections.
    // Exposes strategy CRUD + market data over HTTP.
    // Streams real-time market ticks via ENet (unreliable channel).
    //
    // Endpoints:
    //   GET    /api/strategies          - list all
    //   GET    /api/strategies/active   - list active
    //   GET    /api/strategies/:id      - get by ID
    //   GET    /api/strategies/symbol/:sym - get by symbol
    //   POST   /api/strategies          - insert (JSON body)
    //   PUT    /api/strategies/:id      - update (JSON body)
    //   DELETE /api/strategies/:id      - delete
    //   POST   /api/strategies/:id/cancel - cancel
    //   GET    /api/market/quote?symbol=X&interval=1d&range=6mo
    //   GET    /api/market/search?q=X
    //   GET    /api/status              - server health + monitoring + clients
    //
    class HttpApiServer
    {
    public:
        struct Config
        {
            std::string host = "0.0.0.0";
            int         port = 8099;
            NetFeedConfig feed;   // ENet market feed config (default port 8100)
        };

        HttpApiServer(StrategyStore& store, MarketService& market,
                      StrategyServer* server);
        HttpApiServer(StrategyStore& store, MarketService& market,
                      StrategyServer* server, const Config& config);
        ~HttpApiServer();

        void Start();
        void Stop();
        bool IsRunning() const { return running_.load(); }

        std::string GetUrl() const;

        // Get number of connected ENet clients
        int GetConnectedClients() const { return feedServer_.GetClientCount(); }

        // ENet feed server access (for polling from server loop)
        MarketFeedServer& GetFeedServer() { return feedServer_; }

        // Broadcast a market tick to all connected clients via ENet
        void BroadcastTick(const std::string& symbol, float price,
                           float open, float high, float low, float volume, int64_t timestamp);

    private:
        void SetupRoutes();

        // JSON serialization helpers
        static std::string StrategyToJson(const Strategy& s);
        static std::string StrategiesToJson(const std::vector<Strategy>& list);
        static Strategy    JsonToStrategy(const std::string& json);
        static std::string QuoteToJson(const StockQuote& q);
        static std::string SymbolMatchesToJson(const std::vector<SymbolMatch>& matches);

        Config                          config_;
        StrategyStore&                  store_;
        MarketService&                  market_;
        StrategyServer*                 server_;
        std::unique_ptr<httplib::Server> httpServer_;
        std::thread                     thread_;
        std::atomic<bool>               running_{false};

        // ENet market feed server
        MarketFeedServer                feedServer_;

        // Telemetry
        std::atomic<int64_t>            requestCount_{0};
        std::atomic<int64_t>            ticksBroadcast_{0};
        std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
    };

} // namespace stnks
