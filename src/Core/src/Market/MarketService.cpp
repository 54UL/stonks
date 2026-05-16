#include <Market/MarketService.hpp>
#include <Market/YahooFinanceSource.hpp>
#include <spdlog/spdlog.h>

namespace stnks
{
    MarketService::MarketService(HttpClient& http, ThreadRegistry& threads)
        : http_(http), threads_(threads)
    {
        // Register Yahoo Finance as the default source
        AddSource(std::make_unique<YahooFinanceSource>(http));
    }

    void MarketService::AddSource(std::unique_ptr<IMarketSource> source)
    {
        spdlog::info("[MarketService] Added source: {} (realtime={})",
                     source->GetName(), source->IsRealtime());
        sources_.push_back(std::move(source));
    }

    bool MarketService::SetActiveSource(const std::string& name)
    {
        for (size_t i = 0; i < sources_.size(); ++i)
        {
            if (name == sources_[i]->GetName())
            {
                activeSourceIdx_ = i;
                spdlog::info("[MarketService] Active source: {}", name);
                return true;
            }
        }
        spdlog::warn("[MarketService] Source '{}' not found", name);
        return false;
    }

    IMarketSource* MarketService::GetActiveSource() const
    {
        if (sources_.empty()) return nullptr;
        return sources_[activeSourceIdx_].get();
    }

    // ── Synchronous operations ───────────────────────────────────────────────────

    StockQuote MarketService::FetchQuote(const std::string& symbol,
                                         const std::string& interval,
                                         const std::string& range)
    {
        auto* source = GetActiveSource();
        if (!source)
        {
            spdlog::error("[MarketService] No market source configured");
            StockQuote empty;
            empty.symbol = symbol;
            return empty;
        }
        return source->FetchQuote(symbol, interval, range);
    }

    std::vector<SymbolMatch> MarketService::SearchSymbols(const std::string& query)
    {
        auto* source = GetActiveSource();
        if (!source) return {};
        return source->SearchSymbols(query);
    }

    // ── Async operations ─────────────────────────────────────────────────────────

    void MarketService::FetchQuoteAsync(const std::string& symbol,
                                         const std::string& interval,
                                         const std::string& range)
    {
        threads_.Submit([this, symbol, interval, range]() {
            StockQuote quote = FetchQuote(symbol, interval, range);

            QuoteFetchResult result;
            result.symbol = symbol;
            result.quote  = std::move(quote);

            std::lock_guard<std::mutex> lock(quotesMutex_);
            pendingQuotes_.push_back(std::move(result));
        });
    }

    void MarketService::SearchSymbolsAsync(const std::string& query)
    {
        threads_.Submit([this, query]() {
            auto matches = SearchSymbols(query);

            std::lock_guard<std::mutex> lock(searchMutex_);
            pendingSearches_.push_back(std::move(matches));
        });
    }

} // namespace stnks
