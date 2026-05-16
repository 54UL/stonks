#pragma once

#include <Market/MarketData.hpp>
#include <string>
#include <vector>

namespace stnks
{
    // Abstract interface for market data providers.
    // Implementations wrap specific APIs (Yahoo Finance, Polygon, Twelve Data, etc.)
    class IMarketSource
    {
    public:
        virtual ~IMarketSource() = default;

        // Human-readable name of this source (for UI display)
        virtual const char* GetName() const = 0;

        // Whether this source provides real-time data (vs delayed)
        virtual bool IsRealtime() const = 0;

        // Approximate delay in seconds (0 for real-time, ~900 for Yahoo free tier)
        virtual int GetDelaySeconds() const = 0;

        // Fetch OHLCV candle data for a symbol
        virtual StockQuote FetchQuote(const std::string& symbol,
                                      const std::string& interval = "1d",
                                      const std::string& range    = "6mo") = 0;

        // Search for symbols matching a query string
        virtual std::vector<SymbolMatch> SearchSymbols(const std::string& query) = 0;
    };

} // namespace stnks
