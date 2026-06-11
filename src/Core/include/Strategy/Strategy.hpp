#pragma once

#include <string>
#include <cstdint>

namespace stnks
{
    enum class StrategyStatus : int
    {
        Active    = 0,  // Monitoring price
        TPHit     = 1,  // Take profit triggered
        SLHit     = 2,  // Stop loss triggered
        Cancelled = 3   // Manually cancelled
    };

    enum class StrategyDirection : int
    {
        Long  = 0,  // Buying: TP above entry, SL below entry
        Short = 1   // Selling: TP below entry, SL above entry
    };

    enum class StrategyType : int
    {
        TPSL     = 0,  // Classic take-profit / stop-loss
        Position = 1,  // Track an open position (entry tracking, P&L, news)
        AI       = 2   // AI-managed: news analysis → auto-trade or operations/warnings
    };

    enum class BrokerSource : int
    {
        Auto       = 0,  // Automatically determined from data source
        Binance    = 1,  // Binance Spot
        GBM        = 2,  // GBM+ (Bolsa Mexicana)
        MetaTrader = 3,  // MetaTrader 5
    };

    struct Strategy
    {
        int64_t            id          = 0;
        std::string        symbol;
        StrategyDirection  direction   = StrategyDirection::Long;
        StrategyType       type        = StrategyType::TPSL;
        float              entryPrice  = 0.f;
        float              takeProfit  = 0.f;
        float              stopLoss    = 0.f;
        StrategyStatus     status      = StrategyStatus::Active;
        int64_t            createdAt   = 0;   // Unix timestamp
        int64_t            triggeredAt = 0;   // When TP/SL was hit (0 = not triggered)
        std::string        notes;

        // Composition: link to a parent strategy (0 = root/standalone)
        int64_t            parentId    = 0;
        int                priority    = 0;     // Lower = higher priority

        // Position tracking fields
        float              quantity    = 0.f;   // Number of shares/contracts
        int64_t            entryDate   = 0;     // When the position was opened

        // Close tracking: recorded when position is closed/cancelled/triggered
        float              exitPrice      = 0.f;   // Price at which position was exited
        float              closedPnlPct   = 0.f;   // Frozen P/L% at close time

        // Fees: total flat amounts (commissions, spread cost, etc.)
        float              entryFee       = 0.f;   // Total fee paid on entry
        float              exitFee        = 0.f;   // Total fee paid on exit

        // Enable/disable: disabled strategies are not monitored but still shown
        bool               enabled        = true;

        // Broker source: which broker this operation was made on
        BrokerSource       broker         = BrokerSource::Auto;

        bool IsActive() const { return status == StrategyStatus::Active; }
        bool IsEnabled() const { return enabled; }
        bool IsPosition() const { return type == StrategyType::Position; }
        bool IsTPSL() const { return type == StrategyType::TPSL; }
        bool IsAI() const { return type == StrategyType::AI; }

        // Effective prices adjusted for fees (per-unit cost basis)
        float EffectiveEntryPrice() const
        {
            if (entryFee == 0.f) return entryPrice;
            float q = (quantity > 0.f) ? quantity : 1.f;
            return entryPrice + (entryFee / q);
        }

        float EffectiveExitPrice() const
        {
            if (exitFee == 0.f) return exitPrice;
            float q = (quantity > 0.f) ? quantity : 1.f;
            return exitPrice - (exitFee / q);
        }

        // Risk/reward ratio (uses effective entry to reflect real cost basis)
        float RiskReward() const
        {
            float eff   = EffectiveEntryPrice();
            float risk   = std::abs(eff - stopLoss);
            float reward = std::abs(takeProfit - eff);
            if (risk <= 0.f) return 0.f;
            return reward / risk;
        }

        // Profit/loss percentages (uses effective entry)
        float TPPercent() const
        {
            float eff = EffectiveEntryPrice();
            if (eff <= 0.f) return 0.f;
            return ((takeProfit - eff) / eff) * 100.f;
        }
        float SLPercent() const
        {
            float eff = EffectiveEntryPrice();
            if (eff <= 0.f) return 0.f;
            return ((stopLoss - eff) / eff) * 100.f;
        }

        // P&L for position tracking (uses effective entry as real cost basis)
        float UnrealizedPnL(float currentPrice) const
        {
            float eff = EffectiveEntryPrice();
            float diff = (direction == StrategyDirection::Long)
                         ? (currentPrice - eff)
                         : (eff - currentPrice);
            return diff * quantity;
        }

        float UnrealizedPnLPercent(float currentPrice) const
        {
            float eff = EffectiveEntryPrice();
            if (eff <= 0.f) return 0.f;
            float diff = (direction == StrategyDirection::Long)
                         ? (currentPrice - eff)
                         : (eff - currentPrice);
            return (diff / eff) * 100.f;
        }
    };

    inline const char* StatusToString(StrategyStatus s)
    {
        switch (s)
        {
        case StrategyStatus::Active:    return "Active";
        case StrategyStatus::TPHit:     return "TP Hit";
        case StrategyStatus::SLHit:     return "SL Hit";
        case StrategyStatus::Cancelled: return "Cancelled";
        }
        return "Unknown";
    }

    inline const char* DirectionToString(StrategyDirection d)
    {
        return d == StrategyDirection::Long ? "Long" : "Short";
    }

    inline const char* StrategyTypeToString(StrategyType t)
    {
        switch (t)
        {
        case StrategyType::TPSL:     return "TP/SL";
        case StrategyType::Position: return "Position";
        case StrategyType::AI:       return "AI";
        }
        return "Unknown";
    }

    inline const char* BrokerSourceToString(BrokerSource b)
    {
        switch (b)
        {
        case BrokerSource::Auto:       return "Auto";
        case BrokerSource::Binance:    return "Binance";
        case BrokerSource::GBM:        return "GBM+";
        case BrokerSource::MetaTrader: return "MT5";
        }
        return "Unknown";
    }

    // Map a data source name (from SymbolMatch.source) to a BrokerSource.
    inline BrokerSource BrokerSourceFromName(const std::string& sourceName)
    {
        if (sourceName == "Binance")       return BrokerSource::Binance;
        if (sourceName == "GBM+")          return BrokerSource::GBM;
        if (sourceName == "MT5")           return BrokerSource::MetaTrader;
        return BrokerSource::Auto;
    }

    inline constexpr int kBrokerSourceCount = 4;

} // namespace stnks
