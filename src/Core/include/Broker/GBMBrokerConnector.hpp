#pragma once

#include <Broker/IBrokerConnector.hpp>
#include <Broker/WebhookReceiver.hpp>
#include <Net/WebSocketClient.hpp>
#include <Http/HttpClient.hpp>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace stnks
{
    // GBM+ (Grupo Bursátil Mexicano) broker connector.
    //
    // Protocol:
    //   - REST API for orders, positions, balance (OAuth2 auth)
    //   - WebSocket for real-time price streaming
    //   - Webhooks for order fill/cancel notifications
    //
    // GBM+ API (documented at https://developers.gbm.com):
    //   REST Base:   https://api.gbm.com/v1  (production)
    //                https://sandbox.gbm.com/v1  (sandbox)
    //   WebSocket:   wss://stream.gbm.com/v1/prices
    //   Auth:        OAuth2 Bearer token (refresh flow)
    //
    // Endpoints:
    //   POST   /auth/token              - OAuth2 token refresh
    //   GET    /accounts/{id}/balance   - Account balance
    //   GET    /accounts/{id}/positions - Open positions
    //   POST   /accounts/{id}/orders    - Place order
    //   DELETE /accounts/{id}/orders/{orderId} - Cancel order
    //   GET    /accounts/{id}/orders/{orderId} - Order status
    //   GET    /market/search?q=        - Symbol search
    //   GET    /market/quotes/{symbol}  - Latest quote
    //
    // WebSocket messages (JSON):
    //   → {"action":"subscribe","symbols":["AMXL.MX","BIMBOA.MX"]}
    //   ← {"type":"tick","symbol":"AMXL.MX","bid":18.50,"ask":18.52,"last":18.51,"vol":123456,"ts":1700000000}
    //   ← {"type":"heartbeat","ts":1700000000}
    //
    // Webhook events (POST /webhook/gbm):
    //   {"event":"order.filled","order_id":"...","symbol":"...","fill_price":18.50,"qty":100}
    //   {"event":"order.cancelled","order_id":"...","reason":"..."}
    //   {"event":"order.rejected","order_id":"...","reason":"insufficient_funds"}
    //
    // Symbol format: "AMXL.MX" (BMV tickers with .MX suffix)
    class GBMBrokerConnector : public IBrokerConnector
    {
    public:
        GBMBrokerConnector(HttpClient& http);
        ~GBMBrokerConnector() override;

        // ── IBrokerConnector ─────────────────────────────────────────────────────

        void Initialize(const BrokerConfig& config) override;
        void Connect() override;
        void Disconnect() override;
        WsState GetConnectionState() const override;

        void SubscribePrice(const std::string& symbol) override;
        void UnsubscribePrice(const std::string& symbol) override;
        std::vector<std::string> GetSubscribedSymbols() const override;
        BrokerTick GetLastTick(const std::string& symbol) const override;

        BrokerOrder PlaceOrder(const std::string& symbol, OrderSide side, OrderType type,
                               float quantity, float price, float stopPrice,
                               OrderTimeInForce tif) override;
        bool CancelOrder(const std::string& orderId) override;
        BrokerOrder GetOrder(const std::string& orderId) override;
        std::vector<BrokerOrder> GetOpenOrders() override;

        BrokerBalance GetBalance() override;
        std::vector<BrokerPosition> GetPositions() override;

        void SetOnTick(OnTickCallback cb) override;
        void SetOnOrder(OnOrderCallback cb) override;
        void SetOnPosition(OnPositionCallback cb) override;
        void SetOnError(OnErrorCallback cb) override;
        void SetOnConnection(OnConnectionCallback cb) override;

        float GetLatencyMs() const override;
        uint64_t GetTicksReceived() const override;
        const BrokerConfig& GetConfig() const override { return config_; }

        // ── IBrokerDataSource / IMarketSource ──────���─────────────────────────────

        const char* GetName() const override { return "GBM+"; }
        bool IsRealtime() const override { return true; }
        int GetDelaySeconds() const override { return 0; }
        float FetchCurrentPrice(const std::string& symbol) override;
        bool SupportsStreaming() const override { return true; }
        void Subscribe(const std::string& symbol, TickCallback cb) override;
        void Unsubscribe(const std::string& symbol) override;

        StockQuote FetchQuote(const std::string& symbol,
                              const std::string& interval,
                              const std::string& range) override;
        std::vector<SymbolMatch> SearchSymbols(const std::string& query) override;

    private:
        HttpClient&      http_;
        BrokerConfig     config_;
        WebSocketClient  ws_;
        std::unique_ptr<WebhookReceiver> webhookReceiver_;

        // OAuth2 token management
        bool RefreshAccessToken();
        std::string GetAuthHeader() const;
        std::string MakeUrl(const std::string& path) const;

        // Tick cache (symbol → last tick)
        mutable std::mutex tickMutex_;
        std::unordered_map<std::string, BrokerTick> tickCache_;

        // Subscriptions
        mutable std::mutex subMutex_;
        std::vector<std::string> subscriptions_;

        // Callbacks
        std::mutex cbMutex_;
        OnTickCallback       onTick_;
        OnOrderCallback      onOrder_;
        OnPositionCallback   onPosition_;
        OnErrorCallback      onError_;
        OnConnectionCallback onConnection_;

        // Diagnostics
        std::atomic<uint64_t> ticksReceived_{0};

        // WebSocket message parsing
        void HandleWsMessage(const std::string& data);
        void HandleWebhookEvent(const WebhookEvent& event);

        // Parse broker-specific JSON into unified types
        BrokerOrder ParseOrderJson(const std::string& json) const;
        BrokerTick  ParseTickJson(const std::string& json) const;
    };

} // namespace stnks
