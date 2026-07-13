#include <Market/MarketService.hpp>
#include <Market/YahooFinanceSource.hpp>
#include <Broker/IBrokerConnector.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace stnks
{
    MarketService::MarketService(HttpClient& http, ThreadRegistry& threads)
        : http_(http), threads_(threads)
    {
        // Register Yahoo Finance as the default/fallback source (index 0)
        AddSource(std::make_unique<YahooFinanceSource>(http));
    }

    MarketService::~MarketService()
    {
        StopAllPolling();
    }

    void MarketService::StopAllPolling()
    {
        {
            std::lock_guard<std::mutex> lock(pollingMutex_);
            for (auto& [symbol, active] : pollingActive_)
                active = false;
        }

        int remaining = pollingThreadCount_.load();
        if (remaining > 0)
        {
            spdlog::info("[MarketService] Waiting for {} polling thread(s) to exit...", remaining);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
            while (pollingThreadCount_.load() > 0 &&
                   std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            remaining = pollingThreadCount_.load();
            if (remaining > 0)
                spdlog::warn("[MarketService] {} polling thread(s) still running at shutdown", remaining);
            else
                spdlog::info("[MarketService] All polling threads stopped");
        }
    }

    void MarketService::AddSource(std::unique_ptr<IMarketSource> source)
    {
        spdlog::info("[MarketService] Added source: {} (realtime={})",
                     source->GetName(), source->IsRealtime());
        sources_.push_back(std::move(source));
    }

    bool MarketService::RemoveSource(const std::string& name)
    {
        for (auto it = sources_.begin(); it != sources_.end(); ++it)
        {
            if (name == (*it)->GetName())
            {
                // Disconnect if it's a broker connector
                auto* connector = dynamic_cast<IBrokerConnector*>(it->get());
                if (connector)
                    connector->Disconnect();

                size_t idx = static_cast<size_t>(it - sources_.begin());
                sources_.erase(it);

                // Fix activeSourceIdx_ if it was pointing at or past the removed slot
                if (activeSourceIdx_ >= sources_.size())
                    activeSourceIdx_ = 0;
                else if (activeSourceIdx_ > idx)
                    --activeSourceIdx_;

                spdlog::info("[MarketService] Removed source: {}", name);
                return true;
            }
        }
        return false;
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

    IBrokerDataSource* MarketService::GetActiveBrokerSource() const
    {
        auto* source = GetActiveSource();
        return dynamic_cast<IBrokerDataSource*>(source);
    }

    IBrokerDataSource* MarketService::FindBrokerSource(const std::string& name) const
    {
        for (auto& src : sources_)
        {
            if (name == src->GetName())
                return dynamic_cast<IBrokerDataSource*>(src.get());
        }
        return nullptr;
    }

    IMarketSource* MarketService::GetFallbackSource() const
    {
        // Yahoo Finance is always index 0 (registered in constructor)
        if (sources_.empty()) return nullptr;
        return sources_[0].get();
    }


    PriceCacheEntry* MarketService::GetOrCreateCacheEntry(const std::string& symbol)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = priceCache_.find(symbol);
        if (it != priceCache_.end())
            return it->second.get();
        auto entry = std::make_unique<PriceCacheEntry>();
        auto* ptr = entry.get();
        priceCache_[symbol] = std::move(entry);
        return ptr;
    }

    float MarketService::GetCachedPrice(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = priceCache_.find(symbol);
        if (it == priceCache_.end()) return 0.f;
        return it->second->price.load(std::memory_order_relaxed);
    }

    void MarketService::UpdatePriceCache(const std::string& symbol, float price)
    {
        auto* entry = GetOrCreateCacheEntry(symbol);
        entry->price.store(price, std::memory_order_relaxed);
        entry->timestamp.store(std::time(nullptr), std::memory_order_relaxed);
    }

    bool MarketService::IsRealtimePolling(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(pollingMutex_);
        auto it = pollingActive_.find(symbol);
        return it != pollingActive_.end() && it->second;
    }

    void MarketService::StartRealtimePolling(const std::string& symbol)
    {
        {
            std::lock_guard<std::mutex> lock(pollingMutex_);
            if (pollingActive_[symbol])
                return;
            pollingActive_[symbol] = true;
        }

        auto* entry = GetOrCreateCacheEntry(symbol);

        threads_.Submit([this, symbol, entry]() {
            pollingThreadCount_.fetch_add(1);
            spdlog::info("[MarketService] RT polling started for {}", symbol);

            while (true)
            {
                {
                    std::lock_guard<std::mutex> lock(pollingMutex_);
                    auto it = pollingActive_.find(symbol);
                    if (it == pollingActive_.end() || !it->second)
                        break;
                }

                float price = 0.f;

                auto* broker = GetActiveBrokerSource();
                if (broker)
                {
                    price = broker->FetchCurrentPrice(symbol);
                }

                if (price <= 0.f)
                {
                    for (auto& src : sources_)
                    {
                        auto* bs = dynamic_cast<IBrokerDataSource*>(src.get());
                        if (bs && bs != broker)
                        {
                            price = bs->FetchCurrentPrice(symbol);
                            if (price > 0.f) break;
                        }
                    }
                }

                if (price <= 0.f)
                {
                    auto* fallback = GetFallbackSource();
                    if (fallback)
                    {
                        auto quote = fallback->FetchQuote(symbol, "1m", "1d");
                        if (!quote.candles.empty())
                            price = quote.candles.back().close;
                    }
                }

                if (price > 0.f)
                    entry->price.store(price, std::memory_order_relaxed);

                entry->timestamp.store(std::time(nullptr), std::memory_order_relaxed);

                std::this_thread::sleep_for(std::chrono::milliseconds(broker ? 500 : 2000));
            }

            spdlog::info("[MarketService] RT polling stopped for {}", symbol);
            pollingThreadCount_.fetch_sub(1);
        });
    }

    void MarketService::StopRealtimePolling(const std::string& symbol)
    {
        std::lock_guard<std::mutex> lock(pollingMutex_);
        pollingActive_[symbol] = false;
    }


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

        auto quote = source->FetchQuote(symbol, interval, range);

        // Fall back to Yahoo if active source returned no candles (e.g. broker without history)
        if (quote.candles.empty() && source != GetFallbackSource())
        {
            auto* fallback = GetFallbackSource();
            if (fallback)
            {
                spdlog::debug("[MarketService] Active source '{}' returned no candles for {} — falling back to '{}'",
                    source->GetName(), symbol, fallback->GetName());
                quote = fallback->FetchQuote(symbol, interval, range);
            }
        }

        if (!quote.candles.empty())
            UpdatePriceCache(symbol, quote.candles.back().close);

        return quote;
    }

    std::vector<SymbolMatch> MarketService::SearchSymbols(const std::string& query)
    {
        auto* source = GetActiveSource();
        if (!source) return {};

        auto results = source->SearchSymbols(query);

        for (auto& r : results)
        {
            if (r.source.empty())
                r.source = source->GetName();
        }

        if (results.size() < 5 && source != GetFallbackSource())
        {
            auto* fallback = GetFallbackSource();
            if (fallback)
            {
                auto fallbackResults = fallback->SearchSymbols(query);
                for (auto& r : fallbackResults)
                {
                    bool exists = false;
                    for (auto& existing : results)
                    {
                        if (existing.symbol == r.symbol) { exists = true; break; }
                    }
                    if (!exists)
                    {
                        if (r.source.empty())
                            r.source = fallback->GetName();
                        results.push_back(std::move(r));
                    }
                }
            }
        }

        return results;
    }


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
