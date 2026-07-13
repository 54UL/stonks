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

    struct MACDData
    {
        std::vector<float> macd;      // MACD line (fast EMA - slow EMA)
        std::vector<float> signal;    // Signal line (EMA of MACD)
        std::vector<float> histogram; // MACD - signal
        int fastPeriod   = 12;
        int slowPeriod   = 26;
        int signalPeriod = 9;
    };

    inline MACDData ComputeMACD(const std::vector<Candle>& candles,
                                 int fastPeriod = 12, int slowPeriod = 26, int signalPeriod = 9)
    {
        MACDData result;
        result.fastPeriod   = fastPeriod;
        result.slowPeriod   = slowPeriod;
        result.signalPeriod = signalPeriod;

        int n = (int)candles.size();
        result.macd.resize(n, NAN);
        result.signal.resize(n, NAN);
        result.histogram.resize(n, NAN);

        if (n < slowPeriod + signalPeriod) return result;

        // EMA helper: multiplier = 2 / (period + 1)
        auto ema = [](float prev, float val, int period) -> float {
            float k = 2.f / (float)(period + 1);
            return val * k + prev * (1.f - k);
        };

        // Seed fast EMA with SMA
        float fastEma = 0.f;
        for (int i = 0; i < fastPeriod; ++i)
            fastEma += candles[i].close;
        fastEma /= (float)fastPeriod;

        // Seed slow EMA with SMA
        float slowEma = 0.f;
        for (int i = 0; i < slowPeriod; ++i)
            slowEma += candles[i].close;
        slowEma /= (float)slowPeriod;

        // Compute MACD line from slowPeriod-1 onward
        for (int i = slowPeriod - 1; i < n; ++i)
        {
            if (i == slowPeriod - 1)
            {
                // Recalculate fast EMA up to this point
                fastEma = 0.f;
                for (int j = 0; j < fastPeriod; ++j)
                    fastEma += candles[j].close;
                fastEma /= (float)fastPeriod;
                for (int j = fastPeriod; j <= i; ++j)
                    fastEma = ema(fastEma, candles[j].close, fastPeriod);
                // slowEma already seeded
            }
            else
            {
                fastEma = ema(fastEma, candles[i].close, fastPeriod);
                slowEma = ema(slowEma, candles[i].close, slowPeriod);
            }
            result.macd[i] = fastEma - slowEma;
        }

        // Compute signal line (EMA of MACD) starting after enough MACD values
        int signalStart = slowPeriod - 1 + signalPeriod - 1; //this can be refactored to -2? instead of -1 xxxx -1
        if (signalStart >= n) return result;

        // Seed signal with SMA of first signalPeriod MACD values
        float signalEma = 0.f;
        for (int i = slowPeriod - 1; i < slowPeriod - 1 + signalPeriod; ++i)
            signalEma += result.macd[i];
        signalEma /= (float)signalPeriod;
        result.signal[signalStart] = signalEma;
        result.histogram[signalStart] = result.macd[signalStart] - signalEma;

        for (int i = signalStart + 1; i < n; ++i)
        {
            if (std::isnan(result.macd[i])) continue;
            signalEma = ema(signalEma, result.macd[i], signalPeriod);
            result.signal[i] = signalEma;
            result.histogram[i] = result.macd[i] - signalEma;
        }

        return result;
    }

    struct SymbolMatch
    {
        std::string symbol;
        std::string name;
        std::string exchange;
        std::string type;    // "EQUITY", "ETF", "CRYPTO", etc.
        std::string source;  // Data source name ("Yahoo Finance", "Binance", "MT5", etc.)
    };

} // namespace stnks
