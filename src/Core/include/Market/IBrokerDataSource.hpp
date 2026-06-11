#pragma once

#include <Market/IMarketSource.hpp>
#include <string>
#include <functional>

namespace stnks
{
    // Real-time tick data from a broker or fast data provider.
    // Extends IMarketSource so implementations can serve both
    // historical candles AND live ticks through a single object.
    //
    // To add a new broker:
    //   1. Subclass IBrokerDataSource
    //   2. Implement FetchCurrentPrice() (HTTP poll, WebSocket, FIX, etc.)
    //   3. Register via MarketService::AddSource()
    //
    // The "R" (real-time) timeframe in the UI triggers FetchCurrentPrice()
    // at high frequency (~1-2s) instead of the normal candle fetch cycle.
    class IBrokerDataSource : public IMarketSource
    {
    public:
        ~IBrokerDataSource() override = default;

        // Fetch the latest price for a symbol (bid/ask midpoint, last trade, etc.)
        // Returns 0.f on failure. Must be thread-safe.
        virtual float FetchCurrentPrice(const std::string& symbol) = 0;

        // Whether this source supports streaming (WebSocket/FIX push).
        // If true, the engine can subscribe once instead of polling.
        virtual bool SupportsStreaming() const { return false; }

        // Subscribe to real-time price updates for a symbol.
        // Default is no-op (polling sources don't need this).
        // The callback is invoked from the source's network thread —
        // callers must handle thread-safety.
        using TickCallback = std::function<void(const std::string& symbol, float price, int64_t timestamp)>;
        virtual void Subscribe(const std::string& symbol, TickCallback cb) { (void)symbol; (void)cb; }
        virtual void Unsubscribe(const std::string& symbol) { (void)symbol; }
    };

} // namespace stnks
