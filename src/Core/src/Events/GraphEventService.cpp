#include <Events/GraphEventService.hpp>
#include <Market/MarketHours.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <numeric>

namespace stnks
{
    // ── Constructor ──────────────────────────────────────────────────────────

    GraphEventService::GraphEventService()
    {
        InitPatterns();
    }

    // ── Built-in Pattern Definitions ─────────────────────────────────────────

    void GraphEventService::InitPatterns()
    {
        // Bullish Reversal Setup: oversold RSI + bullish candle + volume confirmation
        patterns_.push_back({
            "Bullish Reversal Setup",
            "Multiple indicators suggest a bullish reversal: RSI recovering from oversold, "
            "bullish candlestick pattern, and volume confirmation",
            EventSeverity::Alert,
            {
                {EventSource::RSI,    "Oversold",          2.0f},
                {EventSource::Candle, "Bullish",           1.5f},
                {EventSource::Candle, "Hammer",            1.5f},
                {EventSource::Volume, "Spike",             1.0f},
                {EventSource::MACD,   "Bullish",           1.0f},
                {EventSource::EMA,    "Bullish",           0.5f},
            },
            6,    // 6-candle window
            0.35f // Relatively low threshold — partial matches are useful
        });

        // Bearish Reversal Setup: overbought RSI + bearish candle + volume
        patterns_.push_back({
            "Bearish Reversal Setup",
            "Multiple indicators suggest a bearish reversal: RSI dropping from overbought, "
            "bearish candlestick pattern, and volume confirmation",
            EventSeverity::Alert,
            {
                {EventSource::RSI,    "Overbought",        2.0f},
                {EventSource::Candle, "Bearish",           1.5f},
                {EventSource::Candle, "Shooting",          1.5f},
                {EventSource::Volume, "Spike",             1.0f},
                {EventSource::MACD,   "Bearish",           1.0f},
                {EventSource::EMA,    "Bearish",           0.5f},
            },
            6,
            0.35f
        });

        // Golden Cross Momentum: EMA golden cross + MACD bullish + RSI mid-range
        patterns_.push_back({
            "Golden Cross Momentum",
            "EMA golden cross confirmed by MACD bullish momentum and healthy RSI",
            EventSeverity::Alert,
            {
                {EventSource::EMA,  "Golden Cross",        3.0f},
                {EventSource::MACD, "Bullish Cross",       2.0f},
                {EventSource::MACD, "Above Zero",          1.0f},
                {EventSource::RSI,  "Oversold Exit",       1.0f},
            },
            8,
            0.45f
        });

        // Death Cross Warning: EMA death cross + MACD bearish
        patterns_.push_back({
            "Death Cross Warning",
            "EMA death cross confirmed by MACD bearish momentum — strong downtrend signal",
            EventSeverity::Alert,
            {
                {EventSource::EMA,  "Death Cross",         3.0f},
                {EventSource::MACD, "Bearish Cross",       2.0f},
                {EventSource::MACD, "Below Zero",          1.0f},
                {EventSource::RSI,  "Overbought Exit",     1.0f},
            },
            8,
            0.45f
        });

        // Bollinger Squeeze Breakout: squeeze + volume spike + directional candle
        patterns_.push_back({
            "Squeeze Breakout",
            "Bollinger Band squeeze followed by breakout with volume — expect strong directional move",
            EventSeverity::Warning,
            {
                {EventSource::Bollinger, "Squeeze",        2.5f},
                {EventSource::Bollinger, "Break",          2.0f},
                {EventSource::Volume,    "Spike",          1.5f},
                {EventSource::Candle,    "Engulfing",      1.0f},
            },
            5,
            0.40f
        });

        // Divergence Warning: price making new high but RSI/MACD declining
        patterns_.push_back({
            "Bearish Divergence",
            "Price making higher highs while RSI shows weakness — potential hidden bearish divergence",
            EventSeverity::Warning,
            {
                {EventSource::RSI,       "Divergence",     3.0f},
                {EventSource::MACD,      "Bearish",        1.5f},
                {EventSource::Bollinger, "Upper",          1.0f},
            },
            6,
            0.40f
        });

        // Accumulation Pattern: low volume base + BB squeeze + bullish candles
        patterns_.push_back({
            "Accumulation Zone",
            "Low volatility accumulation with Bollinger squeeze — watching for breakout",
            EventSeverity::Info,
            {
                {EventSource::Bollinger, "Squeeze",        2.0f},
                {EventSource::Volume,    "Low",            1.5f},
                {EventSource::Candle,    "Doji",           1.0f},
                {EventSource::RSI,       "Oversold",       1.0f},
            },
            8,
            0.35f
        });

        // Strong Trend Confirmation: EMA aligned + MACD positive + volume
        patterns_.push_back({
            "Strong Uptrend",
            "Multiple indicators confirm strong uptrend: EMA alignment, MACD momentum, volume",
            EventSeverity::Info,
            {
                {EventSource::EMA,    "Bullish",           2.0f},
                {EventSource::EMA,    "Golden",            1.5f},
                {EventSource::MACD,   "Above Zero",        1.0f},
                {EventSource::Volume, "Spike",             0.5f},
            },
            10,
            0.40f
        });
    }

    // ── Public API ──────────────────────────────────────────────────────────

    void GraphEventService::Scan(const std::string& symbol, const StockQuote& quote)
    {
        int n = (int)quote.candles.size();
        if (n < 30) return; // Need enough history

        std::lock_guard<std::mutex> lock(mutex_);
        auto& state = scanStates_[symbol];

        // Detect if the underlying data changed (timeframe switch, different range).
        // If candle count dropped below what we previously scanned, or the first
        // candle's timestamp shifted, the data is fundamentally different — clear
        // stale events and rescan from scratch.
        bool dataChanged = false;
        if (state.lastCandleCount > 0)
        {
            if (n < state.lastScannedIdx + 1)
                dataChanged = true;
            else if (!quote.candles.empty() && state.firstTimestamp != 0 &&
                     quote.candles.front().timestamp != state.firstTimestamp)
                dataChanged = true;
        }

        if (dataChanged)
        {
            events_.erase(symbol);
            patternMatches_.erase(symbol);
            state.lastScannedIdx  = -1;
            state.lastCandleCount = 0;
            state.firstTimestamp  = 0;
        }

        // Skip if no new candles since last scan
        if (n == state.lastCandleCount && state.lastScannedIdx >= n - 1)
            return;

        ScanCandles(symbol, quote.candles, state);
        ScanVolume(symbol, quote.candles, state);
        ScanRSI(symbol, quote.candles, state);
        ScanMACD(symbol, quote.candles, state);
        ScanEMA(symbol, quote.candles, state);
        ScanBollinger(symbol, quote.candles, state);
        ScanVolumeProfile(symbol, quote.candles, state);

        state.lastScannedIdx  = n - 1;
        state.lastCandleCount = n;
        if (!quote.candles.empty())
            state.firstTimestamp = quote.candles.front().timestamp;

        // Pattern recognition runs after all individual scans
        ScanPatterns(symbol, quote.candles);
    }

    void GraphEventService::RecordStrategyEvent(const Strategy& s, StrategyStatus trigger, float exitPrice)
    {
        GraphEvent ev;
        ev.source    = EventSource::Strategy;
        ev.severity  = EventSeverity::Alert;
        ev.symbol    = s.symbol;
        ev.timestamp = std::time(nullptr);

        float pnlPct = s.UnrealizedPnLPercent(exitPrice);

        if (trigger == StrategyStatus::TPHit)
        {
            ev.title = "TP Hit";
            char buf[128]; char pb[32]; FmtPrice(pb, sizeof(pb), exitPrice, s.symbol);
            snprintf(buf, sizeof(buf), "%s %s TP hit @ %s (%+.2f%%)",
                     s.symbol.c_str(), DirectionToString(s.direction), pb, pnlPct);
            ev.detail = buf;
        }
        else if (trigger == StrategyStatus::SLHit)
        {
            ev.title = "SL Hit";
            char buf[128]; char pb[32]; FmtPrice(pb, sizeof(pb), exitPrice, s.symbol);
            snprintf(buf, sizeof(buf), "%s %s SL hit @ %s (%+.2f%%)",
                     s.symbol.c_str(), DirectionToString(s.direction), pb, pnlPct);
            ev.detail = buf;
        }
        else
        {
            ev.title = "Cancelled";
            ev.severity = EventSeverity::Info;
            char buf[128]; char pb[32]; FmtPrice(pb, sizeof(pb), exitPrice, s.symbol);
            snprintf(buf, sizeof(buf), "%s strategy cancelled @ %s", s.symbol.c_str(), pb);
            ev.detail = buf;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        Push(s.symbol, std::move(ev));
    }

    std::vector<GraphEvent> GraphEventService::GetEvents(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = events_.find(symbol);
        if (it == events_.end()) return {};
        return it->second;
    }

    std::vector<GraphEvent> GraphEventService::GetAllEvents() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GraphEvent> all;
        for (auto& [sym, evs] : events_)
            all.insert(all.end(), evs.begin(), evs.end());

        std::sort(all.begin(), all.end(),
                  [](const GraphEvent& a, const GraphEvent& b) { return a.timestamp > b.timestamp; });
        return all;
    }

    std::vector<GraphEvent> GraphEventService::ConsumeNew()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GraphEvent> result = std::move(newEvents_);
        newEvents_.clear();
        return result;
    }

    int GraphEventService::UnreadCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return (int)newEvents_.size();
    }

    void GraphEventService::PushEvent(const std::string& symbol, GraphEvent event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Push(symbol, std::move(event));
    }

    void GraphEventService::Clear(const std::string& symbol)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (symbol.empty())
        {
            events_.clear();
            scanStates_.clear();
            newEvents_.clear();
            patternMatches_.clear();
        }
        else
        {
            events_.erase(symbol);
            scanStates_.erase(symbol);
            patternMatches_.erase(symbol);
        }
    }

    std::vector<PatternMatch> GraphEventService::GetPatternMatches(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = patternMatches_.find(symbol);
        if (it == patternMatches_.end()) return {};
        return it->second;
    }

    void GraphEventService::Push(const std::string& symbol, GraphEvent&& event)
    {
        // Caller must hold mutex_
        auto& vec = events_[symbol];

        // Dedup: reject if an event with the same title + candleIdx already exists
        for (const auto& existing : vec)
        {
            if (existing.candleIdx == event.candleIdx &&
                existing.title == event.title &&
                existing.source == event.source)
                return; // Already recorded
        }

        vec.push_back(event);
        newEvents_.push_back(event);

        // Trim oldest if over limit
        if ((int)vec.size() > maxEventsPerSymbol)
            vec.erase(vec.begin(), vec.begin() + ((int)vec.size() - maxEventsPerSymbol));
    }

    // ── EMA / SMA Helpers ────────────────────────────────────────────────────

    std::vector<float> GraphEventService::ComputeEMA(const std::vector<Candle>& candles, int period)
    {
        int n = (int)candles.size();
        std::vector<float> ema(n, NAN);
        if (n < period) return ema;

        // Seed with SMA
        float sum = 0.f;
        for (int i = 0; i < period; ++i)
            sum += candles[i].close;
        ema[period - 1] = sum / (float)period;

        float k = 2.f / (float)(period + 1);
        for (int i = period; i < n; ++i)
            ema[i] = candles[i].close * k + ema[i - 1] * (1.f - k);

        return ema;
    }

    std::vector<float> GraphEventService::ComputeSMA(const std::vector<Candle>& candles, int period)
    {
        int n = (int)candles.size();
        std::vector<float> sma(n, NAN);
        if (n < period) return sma;

        float sum = 0.f;
        for (int i = 0; i < period; ++i)
            sum += candles[i].close;
        sma[period - 1] = sum / (float)period;

        for (int i = period; i < n; ++i)
        {
            sum += candles[i].close - candles[i - period].close;
            sma[i] = sum / (float)period;
        }
        return sma;
    }

    // ── Candlestick Patterns ────────────────────────────────────────────────

    void GraphEventService::ScanCandles(const std::string& symbol,
                                         const std::vector<Candle>& candles,
                                         ScanState& state)
    {
        int start = std::max(1, state.lastScannedIdx + 1);
        int n = (int)candles.size();

        for (int i = start; i < n; ++i)
        {
            const auto& c = candles[i];
            float body    = std::abs(c.close - c.open);
            float range   = c.high - c.low;
            if (range <= 0.f) continue;

            float upperWick = c.high - std::max(c.open, c.close);
            float lowerWick = std::min(c.open, c.close) - c.low;
            float bodyRatio = body / range;

            // Doji: very small body relative to range
            if (bodyRatio < 0.1f && range > 0.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::Candle;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = c.timestamp;
                ev.candleIdx = i;
                ev.title     = "Doji";
                char buf[128];
                { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                snprintf(buf, sizeof(buf), "Doji at %s - indecision, potential reversal", pb); }
                ev.detail = buf;
                Push(symbol, std::move(ev));
                continue;
            }

            // Hammer: small body at top, long lower wick (>= 2x body)
            if (lowerWick >= body * 2.f && upperWick < body * 0.5f && bodyRatio > 0.1f)
            {
                bool bullish = (i > 0 && candles[i - 1].close > c.close);
                GraphEvent ev;
                ev.source    = EventSource::Candle;
                ev.severity  = bullish ? EventSeverity::Warning : EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = c.timestamp;
                ev.candleIdx = i;
                ev.title     = "Hammer";
                char buf[128];
                { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                snprintf(buf, sizeof(buf), "Hammer at %s - potential bullish reversal", pb); }
                ev.detail = buf;
                Push(symbol, std::move(ev));
                continue;
            }

            // Shooting Star: small body at bottom, long upper wick (>= 2x body)
            if (upperWick >= body * 2.f && lowerWick < body * 0.5f && bodyRatio > 0.1f)
            {
                bool bearish = (i > 0 && candles[i - 1].close < c.close);
                GraphEvent ev;
                ev.source    = EventSource::Candle;
                ev.severity  = bearish ? EventSeverity::Warning : EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = c.timestamp;
                ev.candleIdx = i;
                ev.title     = "Shooting Star";
                char buf[128];
                { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                snprintf(buf, sizeof(buf), "Shooting star at %s - potential bearish reversal", pb); }
                ev.detail = buf;
                Push(symbol, std::move(ev));
                continue;
            }

            // Marubozu: very large body, no wicks (>95% body)
            if (bodyRatio > 0.95f)
            {
                bool bullish = c.IsBullish();
                GraphEvent ev;
                ev.source    = EventSource::Candle;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = c.timestamp;
                ev.candleIdx = i;
                ev.title     = bullish ? "Bullish Marubozu" : "Bearish Marubozu";
                char buf[128];
                { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                snprintf(buf, sizeof(buf), "%s marubozu at %s - strong %s conviction",
                         bullish ? "Bullish" : "Bearish", pb, bullish ? "buying" : "selling"); }
                ev.detail = buf;
                Push(symbol, std::move(ev));
                continue;
            }

            // Spinning Top: small body, roughly equal wicks
            if (bodyRatio > 0.1f && bodyRatio < 0.35f)
            {
                float wickRatio = (upperWick > 0.f && lowerWick > 0.f)
                    ? std::min(upperWick, lowerWick) / std::max(upperWick, lowerWick) : 0.f;
                if (wickRatio > 0.6f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Candle;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = c.timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Spinning Top";
                    char buf[128];
                    { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                    snprintf(buf, sizeof(buf), "Spinning top at %s - indecision between bulls and bears", pb); }
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                    continue;
                }
            }

            // Engulfing patterns
            if (i > 0)
            {
                const auto& prev = candles[i - 1];
                float prevBody = std::abs(prev.close - prev.open);

                if (body > prevBody * 1.5f && prevBody > 0.f)
                {
                    bool bullishEngulf = !prev.IsBullish() && c.IsBullish() &&
                                         c.close > prev.open && c.open < prev.close;
                    bool bearishEngulf = prev.IsBullish() && !c.IsBullish() &&
                                         c.close < prev.open && c.open > prev.close;

                    if (bullishEngulf)
                    {
                        GraphEvent ev;
                        ev.source    = EventSource::Candle;
                        ev.severity  = EventSeverity::Warning;
                        ev.symbol    = symbol;
                        ev.timestamp = c.timestamp;
                        ev.candleIdx = i;
                        ev.title     = "Bullish Engulfing";
                        char buf[128];
                        { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                        snprintf(buf, sizeof(buf), "Bullish engulfing at %s - reversal signal", pb); }
                        ev.detail = buf;
                        Push(symbol, std::move(ev));
                    }
                    else if (bearishEngulf)
                    {
                        GraphEvent ev;
                        ev.source    = EventSource::Candle;
                        ev.severity  = EventSeverity::Warning;
                        ev.symbol    = symbol;
                        ev.timestamp = c.timestamp;
                        ev.candleIdx = i;
                        ev.title     = "Bearish Engulfing";
                        char buf[128];
                        { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, symbol);
                        snprintf(buf, sizeof(buf), "Bearish engulfing at %s - reversal signal", pb); }
                        ev.detail = buf;
                        Push(symbol, std::move(ev));
                    }
                }
            }

            // Morning Star (3-candle bullish reversal): bearish → small body → bullish
            if (i >= 2)
            {
                const auto& c0 = candles[i - 2]; // First: bearish
                const auto& c1 = candles[i - 1]; // Second: small body (star)
                const auto& c2 = candles[i];     // Third: bullish

                float body0 = std::abs(c0.close - c0.open);
                float body1 = std::abs(c1.close - c1.open);
                float body2 = std::abs(c2.close - c2.open);
                float range0 = c0.high - c0.low;

                if (!c0.IsBullish() && body0 > 0.f &&
                    body1 < body0 * 0.3f &&            // Star has tiny body
                    c2.IsBullish() && body2 > body0 * 0.5f &&
                    c2.close > (c0.open + c0.close) / 2.f)  // Closes above midpoint of first
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Candle;
                    ev.severity  = EventSeverity::Alert;
                    ev.symbol    = symbol;
                    ev.timestamp = c2.timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Morning Star";
                    char buf[128];
                    { char pb[32]; FmtPrice(pb, sizeof(pb), c2.close, symbol);
                    snprintf(buf, sizeof(buf), "Morning star at %s - strong bullish reversal (3-candle)", pb); }
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // Evening Star (3-candle bearish reversal): bullish → small body → bearish
            if (i >= 2)
            {
                const auto& c0 = candles[i - 2];
                const auto& c1 = candles[i - 1];
                const auto& c2 = candles[i];

                float body0 = std::abs(c0.close - c0.open);
                float body1 = std::abs(c1.close - c1.open);
                float body2 = std::abs(c2.close - c2.open);

                if (c0.IsBullish() && body0 > 0.f &&
                    body1 < body0 * 0.3f &&
                    !c2.IsBullish() && body2 > body0 * 0.5f &&
                    c2.close < (c0.open + c0.close) / 2.f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Candle;
                    ev.severity  = EventSeverity::Alert;
                    ev.symbol    = symbol;
                    ev.timestamp = c2.timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Evening Star";
                    char buf[128];
                    { char pb[32]; FmtPrice(pb, sizeof(pb), c2.close, symbol);
                    snprintf(buf, sizeof(buf), "Evening star at %s - strong bearish reversal (3-candle)", pb); }
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // Three White Soldiers: 3 consecutive bullish candles with higher closes
            if (i >= 2)
            {
                const auto& c0 = candles[i - 2];
                const auto& c1 = candles[i - 1];
                const auto& c2 = candles[i];

                if (c0.IsBullish() && c1.IsBullish() && c2.IsBullish() &&
                    c1.close > c0.close && c2.close > c1.close &&
                    c1.open > c0.open && c2.open > c1.open)
                {
                    float body0 = std::abs(c0.close - c0.open);
                    float body1 = std::abs(c1.close - c1.open);
                    float body2 = std::abs(c2.close - c2.open);
                    float range0 = c0.high - c0.low;
                    float range1 = c1.high - c1.low;
                    float range2 = c2.high - c2.low;

                    // Bodies should be significant (not spinning tops)
                    if (range0 > 0.f && range1 > 0.f && range2 > 0.f &&
                        body0 / range0 > 0.5f && body1 / range1 > 0.5f && body2 / range2 > 0.5f)
                    {
                        GraphEvent ev;
                        ev.source    = EventSource::Candle;
                        ev.severity  = EventSeverity::Warning;
                        ev.symbol    = symbol;
                        ev.timestamp = c2.timestamp;
                        ev.candleIdx = i;
                        ev.title     = "Three White Soldiers";
                        char buf[128];
                        { char pb[32]; FmtPrice(pb, sizeof(pb), c2.close, symbol);
                        snprintf(buf, sizeof(buf), "Three white soldiers at %s - strong bullish continuation", pb); }
                        ev.detail = buf;
                        Push(symbol, std::move(ev));
                    }
                }
            }

            // Three Black Crows: 3 consecutive bearish candles with lower closes
            if (i >= 2)
            {
                const auto& c0 = candles[i - 2];
                const auto& c1 = candles[i - 1];
                const auto& c2 = candles[i];

                if (!c0.IsBullish() && !c1.IsBullish() && !c2.IsBullish() &&
                    c1.close < c0.close && c2.close < c1.close &&
                    c1.open < c0.open && c2.open < c1.open)
                {
                    float body0 = std::abs(c0.close - c0.open);
                    float body1 = std::abs(c1.close - c1.open);
                    float body2 = std::abs(c2.close - c2.open);
                    float range0 = c0.high - c0.low;
                    float range1 = c1.high - c1.low;
                    float range2 = c2.high - c2.low;

                    if (range0 > 0.f && range1 > 0.f && range2 > 0.f &&
                        body0 / range0 > 0.5f && body1 / range1 > 0.5f && body2 / range2 > 0.5f)
                    {
                        GraphEvent ev;
                        ev.source    = EventSource::Candle;
                        ev.severity  = EventSeverity::Warning;
                        ev.symbol    = symbol;
                        ev.timestamp = c2.timestamp;
                        ev.candleIdx = i;
                        ev.title     = "Three Black Crows";
                        char buf[128];
                        { char pb[32]; FmtPrice(pb, sizeof(pb), c2.close, symbol);
                        snprintf(buf, sizeof(buf), "Three black crows at %s - strong bearish continuation", pb); }
                        ev.detail = buf;
                        Push(symbol, std::move(ev));
                    }
                }
            }
        }
    }

    // ── Volume Analysis ─────────────────────────────────────────────────────

    void GraphEventService::ScanVolume(const std::string& symbol,
                                        const std::vector<Candle>& candles,
                                        ScanState& state)
    {
        int n = (int)candles.size();
        if (n < 21) return;

        int start = std::max(20, state.lastScannedIdx + 1);

        for (int i = start; i < n; ++i)
        {
            // 20-period average volume
            float avgVol = 0.f;
            for (int j = i - 20; j < i; ++j)
                avgVol += candles[j].volume;
            avgVol /= 20.f;

            if (avgVol <= 0.f) continue;

            float ratio = candles[i].volume / avgVol;

            if (ratio >= 3.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::Volume;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "Volume Spike";
                char buf[128];
                snprintf(buf, sizeof(buf), "Volume %.0f is %.1fx average (%.0f) - unusual activity",
                         candles[i].volume, ratio, avgVol);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            else if (ratio <= 0.2f && i == n - 1)
            {
                GraphEvent ev;
                ev.source    = EventSource::Volume;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "Low Volume";
                char buf[128];
                snprintf(buf, sizeof(buf), "Volume %.0f is only %.0f%% of average - thin liquidity",
                         candles[i].volume, ratio * 100.f);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }

            // Volume-price divergence: price up but volume declining (or vice versa)
            if (i >= 5 && i == n - 1)
            {
                float priceChange = candles[i].close - candles[i - 5].close;
                float volChange   = candles[i].volume - candles[i - 5].volume;

                // Price rising but volume falling significantly
                if (priceChange > 0.f && volChange < -avgVol * 0.3f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Volume;
                    ev.severity  = EventSeverity::Warning;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Volume-Price Divergence (Bearish)";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Price rising but volume declining — rally may lack conviction");
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
                // Price falling but volume falling (selling pressure easing)
                else if (priceChange < 0.f && volChange < -avgVol * 0.3f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Volume;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Volume-Price Divergence (Bullish)";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Price falling but selling volume declining — sellers may be exhausted");
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }
        }
    }

    // ── RSI Crossovers + Divergence ──────────────────────────────────────────

    void GraphEventService::ScanRSI(const std::string& symbol,
                                     const std::vector<Candle>& candles,
                                     ScanState& state)
    {
        RSIData rsi = ComputeRSI(candles, 14);
        int n = (int)rsi.values.size();
        if (n < 2) return;

        int start = std::max(1, state.lastScannedIdx + 1);

        for (int i = start; i < n; ++i)
        {
            if (std::isnan(rsi.values[i]) || std::isnan(rsi.values[i - 1]))
                continue;

            float prev = rsi.values[i - 1];
            float curr = rsi.values[i];

            // Crossed into overbought (70)
            if (prev < 70.f && curr >= 70.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "RSI Overbought";
                char buf[96];
                snprintf(buf, sizeof(buf), "RSI crossed above 70 (%.1f) - overbought territory", curr);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            else if (prev >= 70.f && curr < 70.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Alert;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "RSI Overbought Exit";
                char buf[96];
                snprintf(buf, sizeof(buf), "RSI crossed below 70 (%.1f) - potential sell signal", curr);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            else if (prev > 30.f && curr <= 30.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "RSI Oversold";
                char buf[96];
                snprintf(buf, sizeof(buf), "RSI crossed below 30 (%.1f) - oversold territory", curr);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            else if (prev <= 30.f && curr > 30.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Alert;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "RSI Oversold Exit";
                char buf[96];
                snprintf(buf, sizeof(buf), "RSI crossed above 30 (%.1f) - potential buy signal", curr);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }

            // RSI mid-line cross (50)
            if (prev < 50.f && curr >= 50.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "RSI Bullish Mid-Cross";
                char buf[96];
                snprintf(buf, sizeof(buf), "RSI crossed above 50 (%.1f) - momentum shifting bullish", curr);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            else if (prev >= 50.f && curr < 50.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "RSI Bearish Mid-Cross";
                char buf[96];
                snprintf(buf, sizeof(buf), "RSI crossed below 50 (%.1f) - momentum shifting bearish", curr);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
        }

        // RSI Divergence detection (only on last few candles)
        if (n >= 20)
        {
            // Look back 10-20 candles for price highs/lows vs RSI highs/lows
            int lookback = std::min(20, n - 1);
            int endIdx = n - 1;
            int startIdx = endIdx - lookback;

            // Find highest price and highest RSI in the lookback window
            float maxPrice = -1e18f, maxRSI = -1e18f;
            int maxPriceIdx = startIdx, maxRSIIdx = startIdx;
            float minPrice = 1e18f, minRSI = 1e18f;
            int minPriceIdx = startIdx, minRSIIdx = startIdx;

            for (int j = startIdx; j <= endIdx; ++j)
            {
                if (std::isnan(rsi.values[j])) continue;
                if (candles[j].high > maxPrice) { maxPrice = candles[j].high; maxPriceIdx = j; }
                if (candles[j].low  < minPrice) { minPrice = candles[j].low;  minPriceIdx = j; }
                if (rsi.values[j] > maxRSI) { maxRSI = rsi.values[j]; maxRSIIdx = j; }
                if (rsi.values[j] < minRSI) { minRSI = rsi.values[j]; minRSIIdx = j; }
            }

            // Bearish divergence: price at new high but RSI at lower high
            // (latest candle near price high, but RSI peaked earlier)
            if (maxPriceIdx > endIdx - 3 && maxRSIIdx < maxPriceIdx - 2 &&
                !std::isnan(rsi.values[endIdx]) && rsi.values[endIdx] < maxRSI * 0.95f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[endIdx].timestamp;
                ev.candleIdx = endIdx;
                ev.title     = "RSI Bearish Divergence";
                char buf[128];
                snprintf(buf, sizeof(buf), "Price making new highs but RSI declining (%.1f vs peak %.1f) - bearish divergence",
                         rsi.values[endIdx], maxRSI);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }

            // Bullish divergence: price at new low but RSI at higher low
            if (minPriceIdx > endIdx - 3 && minRSIIdx < minPriceIdx - 2 &&
                !std::isnan(rsi.values[endIdx]) && rsi.values[endIdx] > minRSI * 1.05f)
            {
                GraphEvent ev;
                ev.source    = EventSource::RSI;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[endIdx].timestamp;
                ev.candleIdx = endIdx;
                ev.title     = "RSI Bullish Divergence";
                char buf[128];
                snprintf(buf, sizeof(buf), "Price making new lows but RSI rising (%.1f vs trough %.1f) - bullish divergence",
                         rsi.values[endIdx], minRSI);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
        }
    }

    // ── MACD Crossovers ─────────────────────────────────────────────────────

    void GraphEventService::ScanMACD(const std::string& symbol,
                                      const std::vector<Candle>& candles,
                                      ScanState& state)
    {
        MACDData macd = ComputeMACD(candles);
        int n = (int)macd.histogram.size();
        if (n < 2) return;

        int start = std::max(1, state.lastScannedIdx + 1);

        for (int i = start; i < n; ++i)
        {
            if (std::isnan(macd.histogram[i]) || std::isnan(macd.histogram[i - 1]))
                continue;

            float prevHist = macd.histogram[i - 1];
            float currHist = macd.histogram[i];

            // Bullish crossover
            if (prevHist < 0.f && currHist >= 0.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::MACD;
                ev.severity  = EventSeverity::Alert;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "MACD Bullish Cross";
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "MACD crossed above signal (MACD: %.4f, Signal: %.4f) - bullish momentum",
                         std::isnan(macd.macd[i]) ? 0.f : macd.macd[i],
                         std::isnan(macd.signal[i]) ? 0.f : macd.signal[i]);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            else if (prevHist >= 0.f && currHist < 0.f)
            {
                GraphEvent ev;
                ev.source    = EventSource::MACD;
                ev.severity  = EventSeverity::Alert;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "MACD Bearish Cross";
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "MACD crossed below signal (MACD: %.4f, Signal: %.4f) - bearish momentum",
                         std::isnan(macd.macd[i]) ? 0.f : macd.macd[i],
                         std::isnan(macd.signal[i]) ? 0.f : macd.signal[i]);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }

            // Zero line cross
            if (!std::isnan(macd.macd[i]) && !std::isnan(macd.macd[i - 1]))
            {
                if (macd.macd[i - 1] < 0.f && macd.macd[i] >= 0.f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::MACD;
                    ev.severity  = EventSeverity::Warning;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "MACD Above Zero";
                    char buf[96];
                    snprintf(buf, sizeof(buf), "MACD crossed above zero (%.4f) - bullish trend shift", macd.macd[i]);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
                else if (macd.macd[i - 1] >= 0.f && macd.macd[i] < 0.f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::MACD;
                    ev.severity  = EventSeverity::Warning;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "MACD Below Zero";
                    char buf[96];
                    snprintf(buf, sizeof(buf), "MACD crossed below zero (%.4f) - bearish trend shift", macd.macd[i]);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // MACD Histogram momentum shift (histogram shrinking = momentum weakening)
            if (i >= 3 &&
                !std::isnan(macd.histogram[i]) && !std::isnan(macd.histogram[i-1]) &&
                !std::isnan(macd.histogram[i-2]) && !std::isnan(macd.histogram[i-3]))
            {
                bool histShrinking = std::abs(macd.histogram[i]) < std::abs(macd.histogram[i-1]) &&
                                     std::abs(macd.histogram[i-1]) < std::abs(macd.histogram[i-2]) &&
                                     std::abs(macd.histogram[i-2]) < std::abs(macd.histogram[i-3]);
                if (histShrinking && i == (int)candles.size() - 1)
                {
                    bool bullish = macd.histogram[i] < 0.f;
                    GraphEvent ev;
                    ev.source    = EventSource::MACD;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = bullish ? "MACD Momentum Recovery" : "MACD Momentum Fading";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "MACD histogram shrinking for 3+ bars — %s momentum weakening",
                             bullish ? "bearish" : "bullish");
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }
        }
    }

    // ── EMA Crossovers ──────────────────────────────────────────────────────

    void GraphEventService::ScanEMA(const std::string& symbol,
                                     const std::vector<Candle>& candles,
                                     ScanState& state)
    {
        int n = (int)candles.size();
        if (n < 55) return; // Need at least 50-period EMA + a few bars

        auto ema9  = ComputeEMA(candles, 9);
        auto ema21 = ComputeEMA(candles, 21);
        auto ema50 = ComputeEMA(candles, 50);

        int start = std::max(50, state.lastScannedIdx + 1);

        for (int i = start; i < n; ++i)
        {
            // EMA 9/21 crossover (short-term trend)
            if (!std::isnan(ema9[i]) && !std::isnan(ema21[i]) &&
                !std::isnan(ema9[i-1]) && !std::isnan(ema21[i-1]))
            {
                bool prevAbove = ema9[i-1] > ema21[i-1];
                bool currAbove = ema9[i] > ema21[i];

                if (!prevAbove && currAbove)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::EMA;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "EMA 9/21 Bullish Cross";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "EMA(9) crossed above EMA(21) [%.2f > %.2f] - short-term bullish",
                             ema9[i], ema21[i]);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
                else if (prevAbove && !currAbove)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::EMA;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "EMA 9/21 Bearish Cross";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "EMA(9) crossed below EMA(21) [%.2f < %.2f] - short-term bearish",
                             ema9[i], ema21[i]);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // EMA 21/50 crossover (medium-term, golden/death cross)
            if (!std::isnan(ema21[i]) && !std::isnan(ema50[i]) &&
                !std::isnan(ema21[i-1]) && !std::isnan(ema50[i-1]))
            {
                bool prevAbove = ema21[i-1] > ema50[i-1];
                bool currAbove = ema21[i] > ema50[i];

                if (!prevAbove && currAbove)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::EMA;
                    ev.severity  = EventSeverity::Alert;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Golden Cross (EMA 21/50)";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "EMA(21) crossed above EMA(50) [%.2f > %.2f] - medium-term bullish trend",
                             ema21[i], ema50[i]);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
                else if (prevAbove && !currAbove)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::EMA;
                    ev.severity  = EventSeverity::Alert;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Death Cross (EMA 21/50)";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "EMA(21) crossed below EMA(50) [%.2f < %.2f] - medium-term bearish trend",
                             ema21[i], ema50[i]);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // Price vs EMA(50) cross (trend filter)
            if (!std::isnan(ema50[i]) && !std::isnan(ema50[i-1]))
            {
                bool prevAbove = candles[i-1].close > ema50[i-1];
                bool currAbove = candles[i].close > ema50[i];

                if (!prevAbove && currAbove)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::EMA;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Price Above EMA(50)";
                    char buf[128];
                    { char pb[32]; FmtPrice(pb, sizeof(pb), candles[i].close, symbol);
                    snprintf(buf, sizeof(buf), "Price %s crossed above EMA(50) %.2f - bullish trend territory",
                             pb, ema50[i]); }
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
                else if (prevAbove && !currAbove)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::EMA;
                    ev.severity  = EventSeverity::Info;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "Price Below EMA(50)";
                    char buf[128];
                    { char pb[32]; FmtPrice(pb, sizeof(pb), candles[i].close, symbol);
                    snprintf(buf, sizeof(buf), "Price %s crossed below EMA(50) %.2f - bearish trend territory",
                             pb, ema50[i]); }
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }
        }
    }

    // ── Bollinger Bands ─────────────────────────────────────────────────────

    void GraphEventService::ScanBollinger(const std::string& symbol,
                                           const std::vector<Candle>& candles,
                                           ScanState& state)
    {
        int n = (int)candles.size();
        int period = 20;
        float numStdDev = 2.f;

        if (n < period + 2) return;

        auto sma = ComputeSMA(candles, period);
        int start = std::max(period, state.lastScannedIdx + 1);

        for (int i = start; i < n; ++i)
        {
            if (std::isnan(sma[i])) continue;

            // Compute standard deviation
            float sumSq = 0.f;
            for (int j = i - period + 1; j <= i; ++j)
            {
                float diff = candles[j].close - sma[i];
                sumSq += diff * diff;
            }
            float stdDev = std::sqrt(sumSq / (float)period);

            float upper = sma[i] + numStdDev * stdDev;
            float lower = sma[i] - numStdDev * stdDev;
            float bandwidth = (upper - lower) / sma[i]; // Normalized bandwidth

            // Previous bar's bands (for cross detection)
            if (i < 1 || std::isnan(sma[i-1])) continue;

            float prevSumSq = 0.f;
            for (int j = i - period; j < i; ++j)
            {
                float diff = candles[j].close - sma[i-1];
                prevSumSq += diff * diff;
            }
            float prevStdDev = std::sqrt(prevSumSq / (float)period);
            float prevUpper = sma[i-1] + numStdDev * prevStdDev;
            float prevLower = sma[i-1] - numStdDev * prevStdDev;

            // Price touching/breaking upper band
            if (candles[i].close >= upper && candles[i-1].close < prevUpper)
            {
                GraphEvent ev;
                ev.source    = EventSource::Bollinger;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "BB Upper Band Break";
                char buf[128];
                { char pb[32]; FmtPrice(pb, sizeof(pb), candles[i].close, symbol);
                snprintf(buf, sizeof(buf), "Price %s broke above upper Bollinger (%.2f) - overbought or breakout",
                         pb, upper); }
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }

            // Price touching/breaking lower band
            if (candles[i].close <= lower && candles[i-1].close > prevLower)
            {
                GraphEvent ev;
                ev.source    = EventSource::Bollinger;
                ev.severity  = EventSeverity::Warning;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "BB Lower Band Break";
                char buf[128];
                { char pb[32]; FmtPrice(pb, sizeof(pb), candles[i].close, symbol);
                snprintf(buf, sizeof(buf), "Price %s broke below lower Bollinger (%.2f) - oversold or breakdown",
                         pb, lower); }
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }

            // Bollinger squeeze: bandwidth very narrow (below 4% of price)
            if (bandwidth < 0.04f && i == n - 1)
            {
                // Check if bandwidth has been contracting
                float prevBW = (prevUpper - prevLower) / sma[i-1];
                if (prevBW < 0.05f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Bollinger;
                    ev.severity  = EventSeverity::Warning;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "BB Squeeze";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Bollinger bandwidth very tight (%.2f%%) - volatility contraction, breakout imminent",
                             bandwidth * 100.f);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // Bollinger band expansion (after squeeze): bandwidth suddenly widens
            if (i >= 2 && i == n - 1)
            {
                float bw2 = 0.f;
                if (!std::isnan(sma[i-2]))
                {
                    float ss2 = 0.f;
                    for (int j = i - period - 1; j < i - 1; ++j)
                    {
                        if (j < 0) continue;
                        float diff = candles[j].close - sma[i-2];
                        ss2 += diff * diff;
                    }
                    float sd2 = std::sqrt(ss2 / (float)period);
                    bw2 = (2.f * numStdDev * sd2) / sma[i-2];
                }
                float prevBW = (prevUpper - prevLower) / sma[i-1];

                if (bw2 > 0.f && bw2 < 0.04f && prevBW < 0.05f && bandwidth > prevBW * 1.5f)
                {
                    GraphEvent ev;
                    ev.source    = EventSource::Bollinger;
                    ev.severity  = EventSeverity::Alert;
                    ev.symbol    = symbol;
                    ev.timestamp = candles[i].timestamp;
                    ev.candleIdx = i;
                    ev.title     = "BB Squeeze Breakout";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Bollinger bands expanding after squeeze (%.2f%% → %.2f%%) - breakout in progress!",
                             bw2 * 100.f, bandwidth * 100.f);
                    ev.detail = buf;
                    Push(symbol, std::move(ev));
                }
            }

            // Price returning to middle band from outside (mean reversion)
            if (candles[i-1].close > prevUpper && candles[i].close < upper && candles[i].close > sma[i])
            {
                GraphEvent ev;
                ev.source    = EventSource::Bollinger;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "BB Mean Reversion (From Upper)";
                char buf[128];
                snprintf(buf, sizeof(buf), "Price returning from upper band — mean reversion toward SMA(20) %.2f", sma[i]);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
            if (candles[i-1].close < prevLower && candles[i].close > lower && candles[i].close < sma[i])
            {
                GraphEvent ev;
                ev.source    = EventSource::Bollinger;
                ev.severity  = EventSeverity::Info;
                ev.symbol    = symbol;
                ev.timestamp = candles[i].timestamp;
                ev.candleIdx = i;
                ev.title     = "BB Mean Reversion (From Lower)";
                char buf[128];
                snprintf(buf, sizeof(buf), "Price recovering from lower band — mean reversion toward SMA(20) %.2f", sma[i]);
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
        }
    }

    // ── Volume Profile Pattern Detection ──────────────────────────────────────

    void GraphEventService::ScanVolumeProfile(const std::string& symbol,
                                               const std::vector<Candle>& candles,
                                               ScanState& state)
    {
        int n = (int)candles.size();
        const int windowSize = 20;
        const int stepSize   = 5;  // Check every 5 candles to avoid spam
        const int bucketCount = 30;

        if (n < windowSize + 5) return;

        int start = std::max(windowSize, state.lastScannedIdx + 1);
        // Align to step grid
        start = ((start - windowSize) / stepSize) * stepSize + windowSize;

        for (int i = start; i < n; i += stepSize)
        {
            int lo = i - windowSize;
            int hi = i;

            // Find price range for this window
            float pMin = 1e18f, pMax = -1e18f;
            for (int j = lo; j < hi; ++j)
            {
                pMin = std::min(pMin, candles[j].low);
                pMax = std::max(pMax, candles[j].high);
            }
            if (pMax <= pMin) continue;

            float bucketSize = (pMax - pMin) / (float)bucketCount;
            if (bucketSize <= 0.f) continue;

            // Build volume buckets
            std::vector<float> buckets(bucketCount, 0.f);
            for (int j = lo; j < hi; ++j)
            {
                const auto& c = candles[j];
                float cLo = std::min(c.open, c.close);
                float cHi = std::max(c.open, c.close);

                for (int b = 0; b < bucketCount; ++b)
                {
                    float bLo = pMin + (float)b * bucketSize;
                    float bHi = pMin + (float)(b + 1) * bucketSize;
                    float overlapLo = std::max(cLo, bLo);
                    float overlapHi = std::min(cHi, bHi);

                    if (overlapLo < overlapHi)
                    {
                        float bodyRange = cHi - cLo;
                        float frac = (bodyRange > 0.f)
                            ? (overlapHi - overlapLo) / bodyRange
                            : 1.f / (float)bucketCount;
                        buckets[b] += c.volume * frac;
                    }
                    else if (cLo == cHi && c.close >= bLo && c.close < bHi)
                    {
                        buckets[b] += c.volume;
                    }
                }
            }

            // Find POC
            float maxVol = 0.f;
            int pocIdx = 0;
            float totalVol = 0.f;
            for (int b = 0; b < bucketCount; ++b)
            {
                totalVol += buckets[b];
                if (buckets[b] > maxVol) { maxVol = buckets[b]; pocIdx = b; }
            }
            if (totalVol <= 0.f) continue;

            float pocRelative = (float)pocIdx / (float)bucketCount;

            // Volume skew: above vs below POC
            float volAbove = 0.f, volBelow = 0.f;
            for (int b = 0; b < pocIdx; ++b) volBelow += buckets[b];
            for (int b = pocIdx + 1; b < bucketCount; ++b) volAbove += buckets[b];
            float skew = (volAbove - volBelow) / totalVol;

            // Compute Value Area (70%)
            float vaTarget = totalVol * 0.70f;
            float vaVol = buckets[pocIdx];
            int vaLo = pocIdx, vaHi = pocIdx;
            while (vaVol < vaTarget && (vaLo > 0 || vaHi < bucketCount - 1))
            {
                float belowVol = (vaLo > 0) ? buckets[vaLo - 1] : 0.f;
                float aboveVol = (vaHi < bucketCount - 1) ? buckets[vaHi + 1] : 0.f;
                if (belowVol >= aboveVol && vaLo > 0)
                    vaVol += buckets[--vaLo];
                else if (vaHi < bucketCount - 1)
                    vaVol += buckets[++vaHi];
                else if (vaLo > 0)
                    vaVol += buckets[--vaLo];
                else break;
            }

            float pocPrice = pMin + ((float)pocIdx + 0.5f) * bucketSize;
            float vahPrice = pMin + (float)(vaHi + 1) * bucketSize;
            float valPrice = pMin + (float)vaLo * bucketSize;

            // Detect shape
            const char* shapeName = nullptr;
            EventSeverity severity = EventSeverity::Info;
            float confidence = 0.f;

            if (pocRelative < 0.38f && skew < -0.15f)
            {
                shapeName  = "P-Shape (Accumulation)";
                severity   = EventSeverity::Alert;
                confidence = std::min(1.f, std::abs(skew) * 1.5f + (0.38f - pocRelative));
            }
            else if (pocRelative > 0.62f && skew > 0.15f)
            {
                shapeName  = "D-Shape (Distribution)";
                severity   = EventSeverity::Warning;
                confidence = std::min(1.f, std::abs(skew) * 1.5f + (pocRelative - 0.62f));
            }
            else if (pocRelative >= 0.30f && pocRelative <= 0.70f &&
                     std::abs(skew) < 0.20f)
            {
                float vaWidthRel = (float)(vaHi - vaLo + 1) / (float)bucketCount;
                if (vaWidthRel < 0.50f)
                {
                    shapeName  = "B-Shape (Balanced)";
                    severity   = EventSeverity::Info;
                    confidence = std::min(1.f,
                        (1.f - std::abs(skew)) * 0.5f +
                        (1.f - vaWidthRel) * 0.3f +
                        (0.5f - std::abs(pocRelative - 0.5f)) * 0.4f);
                }
            }

            if (!shapeName || confidence < 0.35f) continue;

            GraphEvent ev;
            ev.source    = EventSource::VolProfile;
            ev.severity  = severity;
            ev.symbol    = symbol;
            ev.timestamp = candles[hi - 1].timestamp;
            ev.candleIdx = hi - 1;
            ev.score     = confidence;
            ev.title     = std::string("VP ") + shapeName;

            char buf[256];
            snprintf(buf, sizeof(buf),
                     "%s VP %s over %d bars | POC: %.2f | VAH: %.2f | VAL: %.2f | Confidence: %.0f%%",
                     symbol.c_str(), shapeName, windowSize,
                     pocPrice, vahPrice, valPrice, confidence * 100.f);
            ev.detail = buf;

            Push(symbol, std::move(ev));
        }
    }

    // ── Pattern Recognition (Fuzzy Multi-Indicator Matching) ─────────────────

    void GraphEventService::ScanPatterns(const std::string& symbol,
                                          const std::vector<Candle>& candles)
    {
        // Caller holds mutex_
        auto it = events_.find(symbol);
        if (it == events_.end()) return;

        const auto& symbolEvents = it->second;
        if (symbolEvents.empty()) return;

        int n = (int)candles.size();
        if (n < 5) return;

        auto& matches = patternMatches_[symbol];
        matches.clear();

        for (const auto& pattern : patterns_)
        {
            // Define the window: last N candles
            int windowEnd = n - 1;
            int windowStart = std::max(0, windowEnd - pattern.windowSize);

            float totalWeight = 0.f;
            float matchedWeight = 0.f;
            std::vector<std::string> matched;
            std::vector<std::string> missed;

            for (const auto& cond : pattern.conditions)
            {
                totalWeight += cond.weight;

                // Search symbol events for a matching event within the candle window
                bool found = false;
                float bestSimilarity = 0.f;

                for (const auto& ev : symbolEvents)
                {
                    if (ev.source != cond.source) continue;
                    if (ev.candleIdx < windowStart || ev.candleIdx > windowEnd) continue;

                    // Fuzzy title matching: substring containment with similarity scoring
                    // Check if the condition's titleMatch appears in the event title
                    std::string evTitle = ev.title;
                    std::string condMatch = cond.titleMatch;

                    // Case-insensitive substring search
                    auto toLower = [](std::string s) {
                        for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);
                        return s;
                    };
                    std::string evLower = toLower(evTitle);
                    std::string condLower = toLower(condMatch);

                    float similarity = 0.f;

                    // Exact substring match = 1.0
                    if (evLower.find(condLower) != std::string::npos)
                    {
                        similarity = 1.0f;
                    }
                    else
                    {
                        // Partial match: check individual words
                        // Split condition into words and check how many appear in title
                        int wordCount = 0, wordMatches = 0;
                        size_t pos = 0;
                        while (pos < condLower.size())
                        {
                            size_t end = condLower.find(' ', pos);
                            if (end == std::string::npos) end = condLower.size();
                            std::string word = condLower.substr(pos, end - pos);
                            if (!word.empty())
                            {
                                ++wordCount;
                                if (evLower.find(word) != std::string::npos)
                                    ++wordMatches;
                            }
                            pos = end + 1;
                        }
                        if (wordCount > 0)
                            similarity = (float)wordMatches / (float)wordCount * 0.7f; // Partial = max 70%
                    }

                    if (similarity > bestSimilarity)
                        bestSimilarity = similarity;

                    if (similarity >= 0.5f)
                    {
                        found = true;
                        break;
                    }
                }

                if (found)
                {
                    matchedWeight += cond.weight * bestSimilarity;
                    matched.push_back(cond.titleMatch + " (" + EventSourceName(cond.source) + ")");
                }
                else
                {
                    // Even partial matches contribute fractionally
                    matchedWeight += cond.weight * bestSimilarity * 0.3f;
                    if (bestSimilarity > 0.f)
                        missed.push_back(cond.titleMatch + " ~" + std::to_string((int)(bestSimilarity * 100)) + "%");
                    else
                        missed.push_back(cond.titleMatch + " (" + EventSourceName(cond.source) + ")");
                }
            }

            float score = (totalWeight > 0.f) ? (matchedWeight / totalWeight) : 0.f;

            if (score >= pattern.minScore && !matched.empty())
            {
                PatternMatch pm;
                pm.patternName = pattern.name;
                pm.description = pattern.description;
                pm.score       = score;
                pm.candleIdx   = windowEnd;
                pm.timestamp   = candles[windowEnd].timestamp;
                pm.matched     = std::move(matched);
                pm.missed      = std::move(missed);
                matches.push_back(pm);

                // Also emit as a GraphEvent
                GraphEvent ev;
                ev.source    = EventSource::Pattern;
                ev.severity  = pattern.severity;
                ev.symbol    = symbol;
                ev.timestamp = candles[windowEnd].timestamp;
                ev.candleIdx = windowEnd;
                ev.score     = score;
                ev.title     = pattern.name;

                char buf[256];
                snprintf(buf, sizeof(buf), "%s (%.0f%% match, %d/%d signals)",
                         pattern.description.c_str(), score * 100.f,
                         (int)pm.matched.size(), (int)(pm.matched.size() + pm.missed.size()));
                ev.detail = buf;
                Push(symbol, std::move(ev));
            }
        }
    }

} // namespace stnks
