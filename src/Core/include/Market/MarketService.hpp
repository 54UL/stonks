#pragma once

#include <Market/MarketData.hpp>
#include <Market/IMarketSource.hpp>
#include <Market/IBrokerDataSource.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace stnks
{
    struct QuoteFetchResult
    {
        std::string symbol;
        StockQuote  quote;
    };

    // Cached real-time price entry (lock-free read via atomic float)
    struct PriceCacheEntry
    {
        std::atomic<float>   price{0.f};
        std::atomic<int64_t> timestamp{0};
    };

    // Fetches stock data via pluggable IMarketSource implementations.
    // Default source is Yahoo Finance. Additional sources can be added at runtime.
    //
    // Threading model:
    //   FetchQuote() / SearchSymbols() -- synchronous, call from any thread
    //   FetchQuoteAsync() / SearchSymbolsAsync() -- submits to ThreadRegistry pool
    //   DrainResults() / DrainSearchResults() -- call from main thread to collect
    //
    // Real-time price cache:
    //   GetCachedPrice() -- lock-free read, safe to call every frame
    //   StartRealtimePolling() -- spawns background thread polling broker source
    //   StopRealtimePolling() -- stops background polling for a symbol
    //
    // Uses mutex-guarded queues (MPSC: multiple pool threads push, main thread drains)
    class MarketService
    {
    public:
        MarketService(HttpClient& http, ThreadRegistry& threads);
        ~MarketService();

        // Add a market data source. The first source added becomes the active one.
        void AddSource(std::unique_ptr<IMarketSource> source);

        // Switch the active source by name (returns false if not found)
        bool SetActiveSource(const std::string& name);

        // Get info about the active source
        IMarketSource* GetActiveSource() const;
        const std::vector<std::unique_ptr<IMarketSource>>& GetSources() const { return sources_; }

        // Get the active source as IBrokerDataSource (nullptr if not a broker source)
        IBrokerDataSource* GetActiveBrokerSource() const;

        // Find a broker source by name (e.g., "Binance", "GBM+"). Returns nullptr if not found.
        IBrokerDataSource* FindBrokerSource(const std::string& name) const;

        // Get the Yahoo Finance fallback source (always the first source registered).
        IMarketSource* GetFallbackSource() const;

        // ── Real-time price cache ─────────────────────────────────────────────────

        // Read cached price — lock-free, safe to call every frame from UI thread.
        // Returns 0.f if no cached price available.
        float GetCachedPrice(const std::string& symbol) const;

        // Update the cache (called from background poll thread or broker callback).
        void UpdatePriceCache(const std::string& symbol, float price);

        // Start background polling for a symbol (~1s interval).
        // Uses IBrokerDataSource::FetchCurrentPrice if available, else 1m Yahoo fetch.
        void StartRealtimePolling(const std::string& symbol);

        // Stop background polling for a symbol.
        void StopRealtimePolling(const std::string& symbol);

        // Stop all polling and wait for threads to exit (call before destruction).
        void StopAllPolling();

        // Check if a symbol is being polled in real-time.
        bool IsRealtimePolling(const std::string& symbol) const;

        // ── Synchronous operations ────────────────────────────────────────────────

        StockQuote FetchQuote(const std::string& symbol,
                              const std::string& interval = "1d",
                              const std::string& range    = "6mo");

        std::vector<SymbolMatch> SearchSymbols(const std::string& query);

        // ── Async operations ──────────────────────────────────────────────────────

        void FetchQuoteAsync(const std::string& symbol,
                             const std::string& interval = "1d",
                             const std::string& range    = "6mo");

        void SearchSymbolsAsync(const std::string& query);

        // Drain completed quote results (call from main/UI thread)
        template <typename Fn>
        size_t DrainQuoteResults(Fn&& fn)
        {
            std::lock_guard<std::mutex> lock(quotesMutex_);
            size_t count = pendingQuotes_.size();
            for (auto& r : pendingQuotes_)
                fn(std::move(r));
            pendingQuotes_.clear();
            return count;
        }

        // Drain completed search results (call from main/UI thread)
        template <typename Fn>
        size_t DrainSearchResults(Fn&& fn)
        {
            std::lock_guard<std::mutex> lock(searchMutex_);
            size_t count = pendingSearches_.size();
            for (auto& r : pendingSearches_)
                fn(std::move(r));
            pendingSearches_.clear();
            return count;
        }

    private:
        HttpClient&     http_;
        ThreadRegistry& threads_;

        // Pluggable sources
        std::vector<std::unique_ptr<IMarketSource>> sources_;
        size_t activeSourceIdx_ = 0;

        // MPSC queues: pool threads push (under lock), main thread drains
        std::mutex                    quotesMutex_;
        std::vector<QuoteFetchResult> pendingQuotes_;

        std::mutex                       searchMutex_;
        std::vector<std::vector<SymbolMatch>> pendingSearches_;

        // Real-time price cache: symbol → atomic price+timestamp
        mutable std::mutex cacheMutex_;  // Protects map structure only (not reads of entries)
        std::unordered_map<std::string, std::unique_ptr<PriceCacheEntry>> priceCache_;

        // Active real-time polling symbols
        mutable std::mutex pollingMutex_;
        std::unordered_map<std::string, bool> pollingActive_;  // symbol → running flag
        std::atomic<int> pollingThreadCount_{0};                // live polling threads

        PriceCacheEntry* GetOrCreateCacheEntry(const std::string& symbol);
    };

} // namespace stnks
