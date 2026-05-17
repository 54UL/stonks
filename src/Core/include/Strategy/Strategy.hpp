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

        // Enable/disable: disabled strategies are not monitored but still shown
        bool               enabled        = true;

        bool IsActive() const { return status == StrategyStatus::Active; }
        bool IsEnabled() const { return enabled; }
        bool IsPosition() const { return type == StrategyType::Position; }
        bool IsTPSL() const { return type == StrategyType::TPSL; }
        bool IsAI() const { return type == StrategyType::AI; }

        // Risk/reward ratio (TPSL only)
        float RiskReward() const
        {
            float risk   = std::abs(entryPrice - stopLoss);
            float reward = std::abs(takeProfit - entryPrice);
            if (risk <= 0.f) return 0.f;
            return reward / risk;
        }

        // Profit/loss percentages (TPSL)
        float TPPercent() const
        {
            if (entryPrice <= 0.f) return 0.f;
            return ((takeProfit - entryPrice) / entryPrice) * 100.f;
        }
        float SLPercent() const
        {
            if (entryPrice <= 0.f) return 0.f;
            return ((stopLoss - entryPrice) / entryPrice) * 100.f;
        }

        // P&L for position tracking (given current market price)
        float UnrealizedPnL(float currentPrice) const
        {
            float diff = (direction == StrategyDirection::Long)
                         ? (currentPrice - entryPrice)
                         : (entryPrice - currentPrice);
            return diff * quantity;
        }

        float UnrealizedPnLPercent(float currentPrice) const
        {
            if (entryPrice <= 0.f) return 0.f;
            float diff = (direction == StrategyDirection::Long)
                         ? (currentPrice - entryPrice)
                         : (entryPrice - currentPrice);
            return (diff / entryPrice) * 100.f;
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

} // namespace stnks
