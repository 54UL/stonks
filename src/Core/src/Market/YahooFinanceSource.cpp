#include <Market/YahooFinanceSource.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace stnks
{
    YahooFinanceSource::YahooFinanceSource(HttpClient& http)
        : http_(http) {}

    StockQuote YahooFinanceSource::FetchQuote(const std::string& symbol,
                                               const std::string& interval,
                                               const std::string& range)
    {
        std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/"
                        + symbol
                        + "?interval=" + interval
                        + "&range=" + range;

        auto resp = http_.Get(url);
        if (!resp.Ok())
        {
            spdlog::error("[YahooFinance] Failed to fetch {}: {} ({})",
                          symbol, resp.statusCode, resp.error);
            StockQuote empty;
            empty.symbol = symbol;
            empty.source = GetName();
            empty.fetchedAt = std::time(nullptr);
            return empty;
        }

        return ParseChart(resp.body, symbol);
    }

    std::vector<SymbolMatch> YahooFinanceSource::SearchSymbols(const std::string& query)
    {
        std::string url = "https://query2.finance.yahoo.com/v1/finance/search?q="
                        + query
                        + "&quotesCount=12&newsCount=0&listsCount=0";

        auto resp = http_.Get(url);
        if (!resp.Ok())
        {
            spdlog::warn("[YahooFinance] Symbol search failed for '{}': {} ({})",
                         query, resp.statusCode, resp.error);
            return {};
        }

        return ParseSearch(resp.body);
    }

    StockQuote YahooFinanceSource::ParseChart(const std::string& jsonStr,
                                               const std::string& symbol)
    {
        StockQuote quote;
        quote.symbol = symbol;
        quote.source = GetName();
        quote.fetchedAt = std::time(nullptr);

        try
        {
            auto j = nlohmann::json::parse(jsonStr);
            auto& result = j["chart"]["result"][0];
            auto& meta   = result["meta"];

            quote.exchange = meta.value("exchangeName", "");
            quote.currency = meta.value("currency", "");
            quote.name     = meta.value("shortName", symbol);
            quote.interval = meta.value("dataGranularity", "1d");

            if (meta.contains("regularMarketTime") && meta["regularMarketTime"].is_number())
                quote.fetchedAt = meta["regularMarketTime"].get<int64_t>();

            auto& timestamps = result["timestamp"];
            auto& indicators = result["indicators"]["quote"][0];
            auto& opens      = indicators["open"];
            auto& highs      = indicators["high"];
            auto& lows       = indicators["low"];
            auto& closes     = indicators["close"];
            auto& volumes    = indicators["volume"];

            size_t count = timestamps.size();
            quote.candles.reserve(count);

            for (size_t i = 0; i < count; ++i)
            {
                if (opens[i].is_null() || closes[i].is_null()) continue;

                Candle c;
                c.timestamp = timestamps[i].get<int64_t>();
                c.open      = opens[i].get<float>();
                c.high      = highs[i].is_null()   ? c.open : highs[i].get<float>();
                c.low       = lows[i].is_null()     ? c.open : lows[i].get<float>();
                c.close     = closes[i].get<float>();
                c.volume    = volumes[i].is_null()  ? 0.f    : volumes[i].get<float>();
                quote.candles.push_back(c);
            }

            spdlog::info("[YahooFinance] {} ({}) loaded {} candles",
                         symbol, quote.exchange, quote.candles.size());
        }
        catch (const std::exception& e)
        {
            spdlog::error("[YahooFinance] JSON parse error for {}: {}", symbol, e.what());
        }

        return quote;
    }

    std::vector<SymbolMatch> YahooFinanceSource::ParseSearch(const std::string& jsonStr)
    {
        std::vector<SymbolMatch> results;

        try
        {
            auto j = nlohmann::json::parse(jsonStr);
            auto& quotes = j["quotes"];

            for (auto& q : quotes)
            {
                SymbolMatch m;
                m.symbol   = q.value("symbol", "");
                m.name     = q.value("shortname", q.value("longname", ""));
                m.exchange = q.value("exchDisp", "");
                m.type     = q.value("quoteType", "");

                if (!m.symbol.empty())
                    results.push_back(std::move(m));
            }
        }
        catch (const std::exception& e)
        {
            spdlog::error("[YahooFinance] Search parse error: {}", e.what());
        }

        return results;
    }

} // namespace stnks
