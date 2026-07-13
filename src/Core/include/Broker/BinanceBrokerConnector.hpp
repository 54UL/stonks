#pragma once

#include <Broker/IBrokerConnector.hpp>
#include <Net/WebSocketClient.hpp>
#include <Http/HttpClient.hpp>
#include <nlohmann/json_fwd.hpp>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <string>

namespace stnks
{
    // Binance Spot broker connector.
    //
    // Protocol:
    //   - REST API for orders, positions, balance (HMAC-SHA256 signed requests)
    //   - WebSocket for real-time price streaming (combined streams)
    //   - User Data Stream for order/account updates
    //
    // Binance API:
    //   REST Base:   https://api.binance.com  (production)
    //                https://testnet.binance.vision  (testnet)
    //   WebSocket:   wss://stream.binance.com:9443/ws
    //                wss://testnet.binance.vision/ws  (testnet)
    //   Auth:        HMAC-SHA256 signature (API key + secret)
    //
    // Endpoints:
    //   GET    /api/v3/account         - Account info (balances)
    //   POST   /api/v3/order           - Place order
    //   DELETE /api/v3/order           - Cancel order
    //   GET    /api/v3/order           - Query order
    //   GET    /api/v3/openOrders      - All open orders
    //   GET    /api/v3/myTrades        - Trade history
    //   GET    /api/v3/ticker/price    - Current price
    //   GET    /api/v3/klines          - Candlestick data
    //   GET    /api/v3/exchangeInfo    - Symbol info (filters, lot sizes)
    //   POST   /api/v3/userDataStream  - Start user data stream (listenKey)
    //
    // WebSocket streams (JSON):
    //   Combined: wss://stream.binance.com:9443/stream?streams=btcusdt@trade/ethusdt@trade
    //   Trade:    {"e":"trade","s":"BTCUSDT","p":"50000.00","q":"0.001","T":1700000000000}
    //   Ticker:   {"e":"24hrTicker","s":"BTCUSDT","c":"50000.00","b":"49999.00","a":"50001.00"}
    //
    // User Data Stream (order updates):
    //   {"e":"executionReport","s":"BTCUSDT","S":"BUY","o":"LIMIT","X":"FILLED","p":"50000","q":"0.1",...}
    //
    // Symbol format: "BTCUSDT", "ETHUSDT" (no separator, quote asset suffix)
    class BinanceBrokerConnector : public IBrokerConnector
    {
    public:
        BinanceBrokerConnector(HttpClient& http);
        ~BinanceBrokerConnector() override;


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


        const char* GetName() const override { return "Binance"; }
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
        WebSocketClient  marketWs_;    // Price streams
        WebSocketClient  userWs_;      // User data stream (order updates)
        std::string      listenKey_;   // User data stream listen key

        // HMAC-SHA256 signature for REST requests
        std::string SignQuery(const std::string& queryString) const;
        std::string MakeSignedUrl(const std::string& endpoint, const std::string& params) const;
        std::string GetTimestamp() const;

        // REST helpers
        std::string MakeUrl(const std::string& path) const;
        std::vector<std::pair<std::string,std::string>> GetAuthHeaders() const;

        // User Data Stream lifecycle
        bool StartUserDataStream();
        void KeepAliveUserDataStream();

        // Tick cache (symbol → last tick)
        mutable std::mutex tickMutex_;
        std::unordered_map<std::string, BrokerTick> tickCache_;

        // Subscriptions
        mutable std::mutex subMutex_;
        std::vector<std::string> subscriptions_;

        // For cancel order we need to track symbol → orderId mapping
        mutable std::mutex orderSymbolMutex_;
        std::unordered_map<std::string, std::string> orderIdToSymbol_;

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
        void HandleMarketWsMessage(const std::string& data);
        void HandleUserWsMessage(const std::string& data);

        // Parse broker-specific JSON into unified types
        BrokerOrder ParseOrderJson(const nlohmann::json& j) const;
        BrokerTick  ParseTradeJson(const nlohmann::json& j) const;

        // Binance-specific helpers
        static std::string ToLower(const std::string& s);
        static std::string BinanceOrderType(OrderType type);
        static std::string BinanceTIF(OrderTimeInForce tif);
        static OrderStatus ParseBinanceStatus(const std::string& status);

        // Interval conversion (our format → Binance format)
        static std::string ToBinanceInterval(const std::string& interval);
        static std::string ToBinanceRange(const std::string& range);
    };

} // namespace stnks
