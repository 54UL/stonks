#pragma once

namespace stnks
{
    struct Timeframe
    {
        const char* label;
        const char* interval;
        const char* range;
        bool        realtime;
    };
    // hmmmmm
    inline constexpr Timeframe kTimeframes[] = {
        {"R",   "rt",  "1d",  true},
        {"1m",  "1m",  "1d",  false},
        {"5m",  "5m",  "5d",  false},
        {"15m", "15m", "5d",  false},
        {"30m", "30m", "1mo", false},
        {"1H",  "1h",  "1mo", false},
        {"4H",  "1h",  "6mo", false},
        {"1D",  "1d",  "6mo", false},
        {"1W",  "1wk", "2y",  false},
        {"1M",  "1mo", "5y",  false},
        {"1Y",  "1mo", "max", false},
        {"4Y",  "1wk", "max", false},
    };
    inline constexpr int kTimeframeCount   = 12;
    inline constexpr int kDefaultTimeframe = 7;

} // namespace stnks
