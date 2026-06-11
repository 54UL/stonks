#include <Server/BrokerAction.hpp>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace stnks
{
    BrokerAction::BrokerAction(HttpClient& http, const Config& config)
        : http_(http), config_(config)
    {
    }

    bool BrokerAction::Validate()
    {
        if (config_.apiKey.empty())
        {
            spdlog::warn("[BrokerAction] No API key configured — running in DRY-RUN mode");
            return true;  // Still valid, just won't make real calls
        }

        spdlog::info("[BrokerAction] Configured for {} (account: {})",
                     config_.baseUrl, config_.accountId);
        return true;
    }

    bool BrokerAction::Execute(const StrategyTriggerEvent& event)
    {
        bool isTakeProfit = (event.triggerType == StrategyStatus::TPHit);
        const char* action = isTakeProfit ? "TAKE_PROFIT" : "STOP_LOSS";

        spdlog::info("[BrokerAction] {} triggered for {} @ {:.2f} (strategy #{})",
                     action, event.symbol, event.currentPrice, event.strategy.id);

        // Dry-run mode when no API key
        if (config_.apiKey.empty())
        {
            spdlog::info("[BrokerAction] DRY-RUN: Would place {} order:", action);
            spdlog::info("  Symbol:    {}", event.symbol);
            spdlog::info("  Side:      {}",
                isTakeProfit
                    ? (event.strategy.direction == StrategyDirection::Long ? "SELL" : "BUY")
                    : (event.strategy.direction == StrategyDirection::Long ? "SELL" : "BUY"));
            spdlog::info("  Type:      MARKET");
            spdlog::info("  Trigger:   {} @ {:.2f}", action, event.currentPrice);
            spdlog::info("  Entry was: {:.2f}", event.strategy.entryPrice);

            float pnlPct = event.strategy.UnrealizedPnLPercent(event.currentPrice);

            spdlog::info("  P&L:       {}{:.1f}%", pnlPct >= 0.f ? "+" : "", pnlPct);
            return true;
        }

        // Real API call (when key is available)
        std::string payload = BuildOrderPayload(event);
        std::string url = config_.baseUrl + "/api/v1/orders";

        spdlog::info("[BrokerAction] POST {} (payload: {})", url, payload);

        // TODO: Use http_.Post() when implemented (currently only GET exists)
        // For now, log the intent
        // auto response = http_.Post(url, payload, {
        //     {"Authorization", "Bearer " + config_.apiKey},
        //     {"Content-Type", "application/json"}
        // });
        //
        // if (!response.Ok()) {
        //     spdlog::error("[BrokerAction] Order failed: {} {}", response.statusCode, response.error);
        //     return false;
        // }

        spdlog::warn("[BrokerAction] Real API calls not yet implemented — POST support needed in HttpClient");
        return true;
    }

    std::string BrokerAction::BuildOrderPayload(const StrategyTriggerEvent& event) const
    {
        bool isTakeProfit = (event.triggerType == StrategyStatus::TPHit);

        // When TP hits on a long position, we sell. When SL hits on a long, we also sell.
        // For shorts it's reversed.
        std::string side = (event.strategy.direction == StrategyDirection::Long) ? "sell" : "buy";

        nlohmann::json order = {
            {"symbol",      event.symbol},
            {"side",        side},
            {"type",        "market"},
            {"account_id",  config_.accountId},
            {"trigger",     isTakeProfit ? "take_profit" : "stop_loss"},
            {"strategy_id", event.strategy.id},
            {"metadata", {
                {"entry_price",   event.strategy.entryPrice},
                {"target_price",  isTakeProfit ? event.strategy.takeProfit : event.strategy.stopLoss},
                {"current_price", event.currentPrice},
                {"direction",     DirectionToString(event.strategy.direction)}
            }}
        };

        return order.dump();
    }

} // namespace stnks
