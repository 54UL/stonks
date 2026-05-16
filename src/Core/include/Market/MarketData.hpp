#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace stnks
{
    struct Candle
    {
        int64_t timestamp = 0;   // Unix epoch seconds
        float   open      = 0.f;
        float   high      = 0.f;
        float   low       = 0.f;
        float   close     = 0.f;
        float   volume    = 0.f;

        bool IsBullish() const { return close >= open; }
    };

    struct StockQuote
    {
        std::string          symbol;
        std::string          name;
        std::string          exchange;    // "NYSE", "NASDAQ", "BMV", etc.
        std::string          currency;
        std::string          interval;    // "1d", "1h", "5m", etc.
        std::string          source;      // Data provider name ("Yahoo Finance", etc.)
        int64_t              fetchedAt = 0; // Unix epoch when data was fetched
        std::vector<Candle>  candles;

        float PriceMin() const
        {
            float m = 1e18f;
            for (auto& c : candles) m = std::min(m, c.low);
            return m;
        }
        float PriceMax() const
        {
            float m = -1e18f;
            for (auto& c : candles) m = std::max(m, c.high);
            return m;
        }
        float VolumeMax() const
        {
            float m = 0.f;
            for (auto& c : candles) m = std::max(m, c.volume);
            return m;
        }
    };

    // Computed indicators
    struct RSIData
    {
        std::vector<float> values;  // RSI values aligned with candles (NaN for insufficient data)
        int period = 14;
    };

    inline RSIData ComputeRSI(const std::vector<Candle>& candles, int period = 14)
    {
        RSIData rsi;
        rsi.period = period;
        rsi.values.resize(candles.size(), NAN);

        if ((int)candles.size() < period + 1) return rsi;

        float avgGain = 0.f, avgLoss = 0.f;

        // Initial average
        for (int i = 1; i <= period; ++i)
        {
            float change = candles[i].close - candles[i - 1].close;
            if (change > 0) avgGain += change;
            else            avgLoss += -change;
        }
        avgGain /= (float)period;
        avgLoss /= (float)period;

        auto calcRSI = [](float gain, float loss) -> float {
            if (loss == 0.f) return 100.f;
            float rs = gain / loss;
            return 100.f - (100.f / (1.f + rs));
        };

        rsi.values[period] = calcRSI(avgGain, avgLoss);

        // Smoothed
        for (int i = period + 1; i < (int)candles.size(); ++i)
        {
            float change = candles[i].close - candles[i - 1].close;
            float gain   = change > 0.f ? change : 0.f;
            float loss   = change < 0.f ? -change : 0.f;

            avgGain = (avgGain * (period - 1) + gain) / (float)period;
            avgLoss = (avgLoss * (period - 1) + loss) / (float)period;

            rsi.values[i] = calcRSI(avgGain, avgLoss);
        }

        return rsi;
    }

    struct SymbolMatch
    {
        std::string symbol;
        std::string name;
        std::string exchange;
        std::string type;  // "EQUITY", "ETF", etc.
    };

} // namespace stnks
