#pragma once

#include <Server/IStrategyAction.hpp>
#include <Strategy/StrategyStore.hpp>
#include <Market/MarketService.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <AI/IMarketAnalyzer.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <string>

namespace stnks
{
    // Headless strategy monitoring server.
    //
    // Polls market data at a configurable interval, checks all active strategies
    // against live prices, and fires registered IStrategyAction implementations
    // when a TP or SL level is hit.
    //
    // Designed to run without any UI/graphics dependencies — only needs
    // ThreadRegistry, HttpClient, MarketService, and StrategyStore.
    //
    // Usage:
    //   StrategyServer server;
    //   server.AddAction(std::make_unique<BrokerAction>(...));
    //   server.AddAction(std::make_unique<NotificationAction>(...));
    //   server.Run();  // Blocks until Stop() is called or signal received
    //
    class StrategyServer
    {
    public:
        struct Config
        {
            int pollIntervalSec = 60;       // How often to check prices
            int sentimentIntervalSec = 600; // How often to run sentiment analysis (10 min)
            std::string dbName  = "strategies.db";
        };

        explicit StrategyServer(const Config& config = {});

        // Shared-ownership constructor: uses external store/market instead of creating own.
        // Caller must ensure store and market outlive this server.
        StrategyServer(const Config& config, StrategyStore& store,
                       MarketService& market);

        ~StrategyServer();

        StrategyServer(const StrategyServer&) = delete;
        StrategyServer& operator=(const StrategyServer&) = delete;

        // Register an action to execute when strategies trigger.
        // Actions are called in registration order.
        void AddAction(std::unique_ptr<IStrategyAction> action);

        // Blocking run loop. Polls market data and checks strategies.
        // Returns when Stop() is called.
        void Run();

        // Signal the server to stop (thread-safe).
        void Stop();

        bool IsRunning() const { return running_.load(); }

        // Access active services (for HttpApiServer co-hosting)
        StrategyStore&  GetStore()  { return *store_; }
        MarketService&  GetMarket() { return *market_; }
        bool HasOwnedStore() const  { return ownedStore_ != nullptr; }

        // Callback for receiving sentiment analysis results.
        // Called from the server's poll thread when analysis completes.
        using SentimentCallback = std::function<void(const AnalysisResult&)>;
        void SetSentimentCallback(SentimentCallback cb) { sentimentCallback_ = std::move(cb); }

        // Callback when market data is fetched (for broadcasting ticks)
        using TickCallback = std::function<void(const std::string& symbol, const StockQuote& quote)>;
        void SetTickCallback(TickCallback cb) { tickCallback_ = std::move(cb); }

        // Callback invoked during idle sleep (for polling ENet, etc.)
        using IdleCallback = std::function<void()>;
        void SetIdleCallback(IdleCallback cb) { idleCallback_ = std::move(cb); }

    private:
        void PollAndCheck();
        void CheckStrategies(const std::string& symbol, const StockQuote& quote);
        void RunSentimentAnalysis();
        void FireActions(const Strategy& strategy, StrategyStatus triggerType, float currentPrice);

        // Collect unique symbols from active strategies
        std::vector<std::string> GetActiveSymbols();

        Config config_;

        // Infrastructure — owned when standalone, borrowed when embedded
        ThreadRegistry                          threads_;
        std::unique_ptr<HttpClient>             ownedHttp_;
        std::unique_ptr<MarketService>          ownedMarket_;
        std::unique_ptr<StrategyStore>          ownedStore_;

        // Active pointers (point to owned or external)
        StrategyStore*  store_  = nullptr;
        MarketService*  market_ = nullptr;

        // Registered actions
        std::vector<std::unique_ptr<IStrategyAction>> actions_;

        // Sentiment tracking
        SentimentCallback sentimentCallback_;
        std::chrono::steady_clock::time_point lastSentimentCheck_{};

        // Market tick broadcast
        TickCallback tickCallback_;
        IdleCallback idleCallback_;

        std::atomic<bool> running_{false};
        int lastEffectiveInterval_ = 0; // For logging interval changes
    };

} // namespace stnks
