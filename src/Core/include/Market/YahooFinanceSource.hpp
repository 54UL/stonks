#pragma once

#include <Market/IMarketSource.hpp>
#include <Http/HttpClient.hpp>

namespace stnks
{
    class YahooFinanceSource : public IMarketSource
    {
    public:
        explicit YahooFinanceSource(HttpClient& http);

        const char* GetName() const override { return "Yahoo Finance"; }
        bool IsRealtime() const override { return false; }
        int GetDelaySeconds() const override { return 900; } // ~15 min delay

        StockQuote FetchQuote(const std::string& symbol,
                              const std::string& interval = "1d",
                              const std::string& range    = "6mo") override;

        std::vector<SymbolMatch> SearchSymbols(const std::string& query) override;

    private:
        StockQuote ParseChart(const std::string& json, const std::string& symbol);
        std::vector<SymbolMatch> ParseSearch(const std::string& json);

        HttpClient& http_;
    };

} // namespace stnks
