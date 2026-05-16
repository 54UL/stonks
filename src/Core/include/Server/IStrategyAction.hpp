#pragma once

#include <Strategy/Strategy.hpp>
#include <string>

namespace stnks
{
    // Trigger context passed to actions when a strategy fires
    struct StrategyTriggerEvent
    {
        const Strategy& strategy;
        StrategyStatus  triggerType;   // TPHit or SLHit
        float           currentPrice;  // Price that caused the trigger
        std::string     symbol;
        int64_t         timestamp;     // When the trigger occurred
    };

    // Interface for actions executed when a strategy's TP or SL is hit.
    //
    // Implementations can perform any side effect:
    //   - Place orders via a broker API (BrokerAction)
    //   - Send push/email notifications (NotificationAction)
    //   - Log to file or analytics (LogAction)
    //   - Chain multiple actions together
    //
    // Actions are registered with StrategyServer and called in order.
    class IStrategyAction
    {
    public:
        virtual ~IStrategyAction() = default;

        // Human-readable name for logging
        virtual std::string Name() const = 0;

        // Called when a strategy triggers. Return true if the action succeeded.
        virtual bool Execute(const StrategyTriggerEvent& event) = 0;

        // Optional: called once at startup to validate configuration (API keys, etc.)
        virtual bool Validate() { return true; }
    };

} // namespace stnks
