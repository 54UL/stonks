#pragma once

#include <Market/MarketData.hpp>
#include <Market/IMarketSource.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

namespace stnks
{
    struct QuoteFetchResult
    {
        std::string symbol;
        StockQuote  quote;
    };

    // Fetches stock data via pluggable IMarketSource implementations.
    // Default source is Yahoo Finance. Additional sources can be added at runtime.
    //
    // Threading model:
    //   FetchQuote() / SearchSymbols() -- synchronous, call from any thread
    //   FetchQuoteAsync() / SearchSymbolsAsync() -- submits to ThreadRegistry pool
    //   DrainResults() / DrainSearchResults() -- call from main thread to collect
    //
    // Uses mutex-guarded queues (MPSC: multiple pool threads push, main thread drains)
    class MarketService
    {
    public:
        MarketService(HttpClient& http, ThreadRegistry& threads);

        // Add a market data source. The first source added becomes the active one.
        void AddSource(std::unique_ptr<IMarketSource> source);

        // Switch the active source by name (returns false if not found)
        bool SetActiveSource(const std::string& name);

        // Get info about the active source
        IMarketSource* GetActiveSource() const;
        const std::vector<std::unique_ptr<IMarketSource>>& GetSources() const { return sources_; }

        // Synchronous fetch (blocks calling thread) -- delegates to active source
        StockQuote FetchQuote(const std::string& symbol,
                              const std::string& interval = "1d",
                              const std::string& range    = "6mo");

        // Synchronous symbol search
        std::vector<SymbolMatch> SearchSymbols(const std::string& query);

        // Async fetch -- dispatches to thread pool
        void FetchQuoteAsync(const std::string& symbol,
                             const std::string& interval = "1d",
                             const std::string& range    = "6mo");

        // Async symbol search -- dispatches to thread pool
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
    };

} // namespace stnks
