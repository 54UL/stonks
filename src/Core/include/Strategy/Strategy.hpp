#pragma once

#include <EnumTraits.hpp>
#include <string>
#include <cstdint>

namespace stnks
{
    enum class StrategyStatus : int { Active = 0, TPHit = 1, SLHit = 2, Cancelled = 3 };
    enum class StrategyDirection : int { Long = 0, Short = 1 };
    enum class StrategyType : int { TPSL = 0, Position = 1, AI = 2 };
    enum class BrokerSource : int { Auto = 0, Binance = 1, MetaTrader = 3 };

    template<> struct EnumTraits<StrategyStatus> {
        static constexpr std::pair<StrategyStatus, const char*> values[] = {
            {StrategyStatus::Active, "Active"}, {StrategyStatus::TPHit, "TP Hit"},
            {StrategyStatus::SLHit, "SL Hit"}, {StrategyStatus::Cancelled, "Cancelled"}
        };
    };

    template<> struct EnumTraits<StrategyDirection> {
        static constexpr std::pair<StrategyDirection, const char*> values[] = {
            {StrategyDirection::Long, "Long"}, {StrategyDirection::Short, "Short"}
        };
    };

    template<> struct EnumTraits<StrategyType> {
        static constexpr std::pair<StrategyType, const char*> values[] = {
            {StrategyType::TPSL, "TP/SL"}, {StrategyType::Position, "Position"},
            {StrategyType::AI, "AI"}
        };
    };

    template<> struct EnumTraits<BrokerSource> {
        static constexpr std::pair<BrokerSource, const char*> values[] = {
            {BrokerSource::Auto, "Auto"}, {BrokerSource::Binance, "Binance"},
            {BrokerSource::MetaTrader, "MT5"}
        };
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
        int64_t            createdAt   = 0;
        int64_t            triggeredAt = 0;
        std::string        notes;
        int64_t            parentId    = 0;
        int                priority    = 0;
        float              quantity    = 0.f;
        int64_t            entryDate   = 0;
        float              exitPrice      = 0.f;
        float              closedPnlPct   = 0.f;
        float              entryFee       = 0.f;
        float              exitFee        = 0.f;
        bool               enabled        = true;
        BrokerSource       broker         = BrokerSource::Auto;

        bool IsActive() const { return status == StrategyStatus::Active; }
        bool IsEnabled() const { return enabled; }
        bool IsPosition() const { return type == StrategyType::Position; }
        bool IsTPSL() const { return type == StrategyType::TPSL; }
        bool IsAI() const { return type == StrategyType::AI; }

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

        float RiskReward() const
        {
            float eff   = EffectiveEntryPrice();
            float risk   = std::abs(eff - stopLoss);
            float reward = std::abs(takeProfit - eff);
            if (risk <= 0.f) return 0.f;
            return reward / risk;
        }

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

    inline const char* StatusToString(StrategyStatus s) { return EnumToString(s); }
    inline const char* DirectionToString(StrategyDirection d) { return EnumToString(d); }
    inline const char* StrategyTypeToString(StrategyType t) { return EnumToString(t); }
    inline const char* BrokerSourceToString(BrokerSource b) { return EnumToString(b); }
    inline BrokerSource BrokerSourceFromName(const std::string& name) { return EnumFromString<BrokerSource>(name); }
    inline constexpr int kBrokerSourceCount = static_cast<int>(EnumCount<BrokerSource>());

} // namespace stnks
