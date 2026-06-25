#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace stnks
{
    // Which indicator/source generated the event
    enum class EventSource : int
    {
        Strategy   = 0,  // TP/SL hit, cancel
        Candle     = 1,  // Candlestick pattern (hammer, doji, long wick, engulfing)
        Volume     = 2,  // Unusual volume
        RSI        = 3,  // Overbought/oversold crossover
        MACD       = 4,  // Signal line crossover
        EMA        = 5,  // EMA crossovers (golden cross, death cross)
        Bollinger  = 6,  // Bollinger Band signals
        Pattern    = 7,  // Multi-indicator pattern recognition
        VolProfile = 8,  // Volume profile shape (P/D/B)
    };

    // Severity / importance
    enum class EventSeverity : int
    {
        Info    = 0,  // Informational (e.g., normal crossover)
        Warning = 1,  // Heads-up (e.g., approaching overbought)
        Alert   = 2,  // Actionable (e.g., TP/SL hit, bearish divergence)
    };

    struct GraphEvent
    {
        EventSource   source;
        EventSeverity severity;
        std::string   symbol;
        int64_t       timestamp  = 0;  // Candle timestamp where event occurred
        int           candleIdx  = -1; // Index into candles array
        std::string   title;           // Short label: "MACD Bullish Cross"
        std::string   detail;          // Longer description with values
        float         score      = 1.f; // Confidence/similarity score (0..1), used by pattern recognition
    };

    inline const char* EventSourceName(EventSource s)
    {
        switch (s)
        {
        case EventSource::Strategy:  return "Strategy";
        case EventSource::Candle:    return "Candle";
        case EventSource::Volume:    return "Volume";
        case EventSource::RSI:       return "RSI";
        case EventSource::MACD:      return "MACD";
        case EventSource::EMA:       return "EMA";
        case EventSource::Bollinger: return "BB";
        case EventSource::Pattern:   return "Pattern";
        case EventSource::VolProfile: return "VP";
        }
        return "Unknown";
    }

    inline const char* EventSeverityName(EventSeverity s)
    {
        switch (s)
        {
        case EventSeverity::Info:    return "Info";
        case EventSeverity::Warning: return "Warn";
        case EventSeverity::Alert:   return "Alert";
        }
        return "?";
    }

    // ── Pattern Recognition ──────────────────────────────────────────────────

    // A single condition in a multi-indicator pattern
    struct PatternCondition
    {
        EventSource source;
        std::string titleMatch;   // Substring to match in event title
        float       weight = 1.f; // Importance weight for scoring
    };

    // Definition of a composite pattern (set of co-occurring signals)
    struct PatternDefinition
    {
        std::string                    name;
        std::string                    description;
        EventSeverity                  severity;
        std::vector<PatternCondition>  conditions;
        int                            windowSize = 5;   // Candle window for co-occurrence
        float                          minScore   = 0.5f; // Minimum weighted score to trigger
    };

    // Result of a pattern match attempt
    struct PatternMatch
    {
        std::string              patternName;
        std::string              description;
        float                    score;          // 0.0 - 1.0 weighted similarity
        int                      candleIdx;
        int64_t                  timestamp;
        std::vector<std::string> matched;        // Conditions that matched
        std::vector<std::string> missed;         // Conditions that didn't match
    };

} // namespace stnks
