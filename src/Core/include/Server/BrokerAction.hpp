#pragma once

#include <Server/IStrategyAction.hpp>
#include <Http/HttpClient.hpp>
#include <string>

namespace stnks
{
    // Dummy GBM broker REST API action.
    //
    // Simulates placing orders through a broker's HTTP API.
    // When a real API key is available, replace the dummy endpoints
    // with actual GBM+ (or any broker) API calls.
    //
    // Expected GBM-style endpoints:
    //   POST /api/v1/orders          - place a market order
    //   GET  /api/v1/orders/{id}     - check order status
    //   DELETE /api/v1/orders/{id}   - cancel an order
    //   GET  /api/v1/account/balance - check account balance
    class BrokerAction : public IStrategyAction
    {
    public:
        struct Config
        {
            std::string baseUrl = "https://api.gbm.com";  // Placeholder
            std::string apiKey;                             // Empty = dry-run mode
            std::string accountId;
        };

        BrokerAction(HttpClient& http, const Config& config);

        std::string Name() const override { return "BrokerAction (GBM)"; }
        bool Execute(const StrategyTriggerEvent& event) override;
        bool Validate() override;

    private:
        HttpClient& http_;
        Config      config_;

        // Build the JSON body for an order request
        std::string BuildOrderPayload(const StrategyTriggerEvent& event) const;
    };

} // namespace stnks
