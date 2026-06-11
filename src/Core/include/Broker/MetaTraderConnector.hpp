#pragma once

#include <Broker/IBrokerConnector.hpp>
#include <Net/WebSocketClient.hpp>
#include <Http/HttpClient.hpp>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace stnks
{
    // MetaTrader 5 broker connector via MT5 Web API bridge.
    //
    // MetaTrader doesn't expose a native WebSocket/REST API. This connector
    // works through a bridge service (e.g., MetaApi.cloud, or a custom
    // Expert Advisor that exposes a WebSocket server).
    //
    // Supported bridge protocols:
    //
    // 1. MetaApi.cloud (hosted MT5 bridge):
    //    REST:  https://mt-client-api-v1.agiliumtrade.agiliumtrade.ai
    //    WS:    wss://mt-client-api-v1.agiliumtrade.agiliumtrade.ai/ws
    //    Auth:  Bearer token (MetaApi auth-token)
    //    Docs:  https://metaapi.cloud/docs/client/
    //
    // 2. Custom EA Bridge (self-hosted):
    //    WS:    ws://localhost:5555  (EA runs WS server on VPS)
    //    Auth:  Simple API key header
    //
    // Protocol (MetaApi-style JSON over WebSocket):
    //   → {"type":"subscribe","symbol":"EURUSD"}
    //   ← {"type":"tick","symbol":"EURUSD","bid":1.0850,"ask":1.0852,"time":"2024-01-15T10:30:00Z"}
    //   → {"type":"trade","action":"ORDER_BUY","symbol":"EURUSD","volume":0.1}
    //   ← {"type":"tradeResult","orderId":123456,"status":"filled","price":1.0851}
    //
    // REST endpoints (MetaApi):
    //   GET    /users/current/accounts/{id}/symbols         - Available symbols
    //   GET    /users/current/accounts/{id}/positions       - Open positions
    //   GET    /users/current/accounts/{id}/orders          - Pending orders
    //   POST   /users/current/accounts/{id}/trade           - Execute trade
    //   GET    /users/current/accounts/{id}/history-deals   - Trade history
    //   GET    /users/current/accounts/{id}/account-information - Balance/equity
    //
    // Symbol format: "EURUSD", "XAUUSD", "US500", "BTCUSD" (MT5 standard)
    //
    // Futures specifics:
    //   - Lot sizes and contract specifications vary per symbol
    //   - Margin requirements checked before order
    //   - Swap/rollover handling
    //   - Expiration dates for futures contracts
    class MetaTraderConnector : public IBrokerConnector
    {
    public:
        MetaTraderConnector(HttpClient& http);
        ~MetaTraderConnector() override;

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

        // ── IBrokerDataSource / IMarketSource ────────────────────────────────────

        const char* GetName() const override { return "MetaTrader 5"; }
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

        // Tick cache
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

        std::atomic<uint64_t> ticksReceived_{0};

        // Auth
        std::string GetAuthHeader() const;
        std::string MakeUrl(const std::string& path) const;

        // MetaTrader-specific trade action strings
        static const char* MT5ActionString(OrderSide side, OrderType type);

        // Message handling
        void HandleWsMessage(const std::string& data);
    };

} // namespace stnks
