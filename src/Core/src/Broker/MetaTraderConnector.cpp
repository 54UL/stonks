#include <Broker/MetaTraderConnector.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace stnks
{
    MetaTraderConnector::MetaTraderConnector(HttpClient& http)
        : http_(http)
    {
    }

    MetaTraderConnector::~MetaTraderConnector()
    {
        Disconnect();
    }


    void MetaTraderConnector::Initialize(const BrokerConfig& config)
    {
        config_ = config;
        spdlog::info("[MT5] Initialized: REST={} WS={} server={}",
            config_.restBaseUrl, config_.wsUrl, config_.credentials.server);
    }

    void MetaTraderConnector::Connect()
    {
        WsConfig wsConfig;
        wsConfig.url = config_.wsUrl;
        wsConfig.headers = {
            {"auth-token", config_.credentials.apiKey}
        };
        wsConfig.pingIntervalSec = 10;  // MT5 bridges need faster heartbeat
        wsConfig.autoReconnect = true;
        wsConfig.reconnectDelayMs = 1000;

        ws_.SetOnMessage([this](const std::string& data, WsMessageType)
        {
            HandleWsMessage(data);
        });

        ws_.SetOnStateChange([this](WsState state)
        {
            spdlog::info("[MT5] WebSocket state: {}", WsStateToString(state));

            // Re-subscribe on reconnect
            if (state == WsState::Connected)
            {
                std::lock_guard<std::mutex> lock(subMutex_);
                for (auto& sym : subscriptions_)
                {
                    nlohmann::json msg = {{"type", "subscribe"}, {"symbol", sym}};
                    ws_.SendText(msg.dump());
                }
            }

            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onConnection_) onConnection_(state);
        });

        ws_.SetOnError([this](const std::string& reason, int code)
        {
            spdlog::error("[MT5] WebSocket error: {} ({})", reason, code);
            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onError_) onError_(reason, code);
        });

        ws_.Connect(wsConfig);
        spdlog::info("[MT5] Connecting...");
    }

    void MetaTraderConnector::Disconnect()
    {
        ws_.Disconnect();
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.clear();
        }
        spdlog::info("[MT5] Disconnected");
    }

    WsState MetaTraderConnector::GetConnectionState() const
    {
        return ws_.GetState();
    }


    void MetaTraderConnector::SubscribePrice(const std::string& symbol)
    {
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.push_back(symbol);
        }

        nlohmann::json msg = {{"type", "subscribe"}, {"symbol", symbol}};
        ws_.SendText(msg.dump());
        spdlog::info("[MT5] Subscribed to {}", symbol);
    }

    void MetaTraderConnector::UnsubscribePrice(const std::string& symbol)
    {
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.erase(
                std::remove(subscriptions_.begin(), subscriptions_.end(), symbol),
                subscriptions_.end());
        }

        nlohmann::json msg = {{"type", "unsubscribe"}, {"symbol", symbol}};
        ws_.SendText(msg.dump());
    }

    std::vector<std::string> MetaTraderConnector::GetSubscribedSymbols() const
    {
        std::lock_guard<std::mutex> lock(subMutex_);
        return subscriptions_;
    }

    BrokerTick MetaTraderConnector::GetLastTick(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(tickMutex_);
        auto it = tickCache_.find(symbol);
        if (it != tickCache_.end()) return it->second;
        return {};
    }


    float MetaTraderConnector::FetchCurrentPrice(const std::string& symbol)
    {
        auto tick = GetLastTick(symbol);
        if (tick.last > 0.f) return tick.last;
        if (tick.bid > 0.f) return tick.Mid();
        return 0.f;
    }

    void MetaTraderConnector::Subscribe(const std::string& symbol, TickCallback cb)
    {
        SubscribePrice(symbol);
        SetOnTick([cb, symbol](const BrokerTick& tick) {
            if (tick.symbol == symbol)
                cb(tick.symbol, tick.last > 0.f ? tick.last : tick.Mid(), tick.timestamp);
        });
    }

    void MetaTraderConnector::Unsubscribe(const std::string& symbol)
    {
        UnsubscribePrice(symbol);
    }

    StockQuote MetaTraderConnector::FetchQuote(const std::string& symbol,
                                                const std::string& interval,
                                                const std::string& range)
    {
        // MT5 bridge can provide candles via REST
        std::string url = MakeUrl("/users/current/accounts/" +
            config_.credentials.accountId + "/historical-market-data/symbols/" +
            symbol + "/timeframes/" + interval + "/candles?limit=500");

        auto resp = http_.Get(url);

        StockQuote quote;
        quote.symbol = symbol;
        quote.source = GetName();
        quote.fetchedAt = std::time(nullptr);
        quote.interval = interval;

        if (!resp.Ok()) return quote;

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j)
            {
                Candle c;
                c.open      = item.value("open", 0.f);
                c.high      = item.value("high", 0.f);
                c.low       = item.value("low", 0.f);
                c.close     = item.value("close", 0.f);
                c.volume    = item.value("tickVolume", item.value("volume", 0.f));

                // Parse ISO timestamp or unix
                if (item.contains("time") && item["time"].is_string())
                {
                    // ISO 8601 → unix (simplified)
                    c.timestamp = std::time(nullptr); // TODO: proper ISO parse
                }
                else
                {
                    c.timestamp = item.value("time", (int64_t)0);
                }

                quote.candles.push_back(c);
            }
        }
        catch (...) {}

        return quote;
    }

    std::vector<SymbolMatch> MetaTraderConnector::SearchSymbols(const std::string& query)
    {
        std::string url = MakeUrl("/users/current/accounts/" +
            config_.credentials.accountId + "/symbols");

        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<SymbolMatch> results;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j)
            {
                std::string sym = item.value("symbol", "");
                std::string desc = item.value("description", "");

                // Simple substring filter
                if (sym.find(query) != std::string::npos ||
                    desc.find(query) != std::string::npos)
                {
                    SymbolMatch m;
                    m.symbol   = sym;
                    m.name     = desc;
                    m.exchange = item.value("path", "MT5");
                    m.type     = item.value("type", "FOREX");
                    results.push_back(std::move(m));

                    if (results.size() >= 20) break;
                }
            }
        }
        catch (...) {}
        return results;
    }


    const char* MetaTraderConnector::MT5ActionString(OrderSide side, OrderType type)
    {
        if (type == OrderType::Market)
            return side == OrderSide::Buy ? "ORDER_TYPE_BUY" : "ORDER_TYPE_SELL";
        if (type == OrderType::Limit)
            return side == OrderSide::Buy ? "ORDER_TYPE_BUY_LIMIT" : "ORDER_TYPE_SELL_LIMIT";
        if (type == OrderType::Stop)
            return side == OrderSide::Buy ? "ORDER_TYPE_BUY_STOP" : "ORDER_TYPE_SELL_STOP";
        if (type == OrderType::StopLimit)
            return side == OrderSide::Buy ? "ORDER_TYPE_BUY_STOP_LIMIT" : "ORDER_TYPE_SELL_STOP_LIMIT";
        return "ORDER_TYPE_BUY";
    }

    BrokerOrder MetaTraderConnector::PlaceOrder(const std::string& symbol,
                                                 OrderSide side, OrderType type,
                                                 float quantity, float price,
                                                 float stopPrice, OrderTimeInForce tif)
    {
        nlohmann::json body = {
            {"actionType", MT5ActionString(side, type)},
            {"symbol",     symbol},
            {"volume",     quantity}  // MT5 uses "volume" (lots)
        };

        if (price > 0.f) body["openPrice"] = price;
        if (stopPrice > 0.f) body["stopLoss"] = stopPrice;

        // Can also send via WebSocket for lower latency
        nlohmann::json wsMsg = {
            {"type", "trade"},
            {"accountId", config_.credentials.accountId},
            {"application", "MetaApi"}
        };
        wsMsg.merge_patch(body);

        ws_.SendText(wsMsg.dump());

        BrokerOrder order;
        order.symbol = symbol;
        order.side = side;
        order.type = type;
        order.quantity = quantity;
        order.price = price;
        order.stopPrice = stopPrice;
        order.status = OrderStatus::Pending;
        order.createdAt = std::time(nullptr);

        spdlog::info("[MT5] Order sent: {} {} {} {}",
            MT5ActionString(side, type), quantity, symbol, price);
        return order;
    }

    bool MetaTraderConnector::CancelOrder(const std::string& orderId)
    {
        nlohmann::json body = {
            {"type", "trade"},
            {"actionType", "ORDER_CANCEL"},
            {"orderId", orderId}
        };
        ws_.SendText(body.dump());
        return true; // Async — result comes via callback
    }

    BrokerOrder MetaTraderConnector::GetOrder(const std::string& orderId)
    {
        std::string url = MakeUrl("/users/current/accounts/" +
            config_.credentials.accountId + "/orders/" + orderId);
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        BrokerOrder order;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            order.orderId = j.value("id", orderId);
            order.symbol = j.value("symbol", "");
            order.quantity = j.value("volume", 0.f);
            order.price = j.value("openPrice", 0.f);

            std::string state = j.value("state", "");
            if (state == "ORDER_STATE_FILLED") order.status = OrderStatus::Filled;
            else if (state == "ORDER_STATE_CANCELED") order.status = OrderStatus::Cancelled;
            else order.status = OrderStatus::Pending;
        }
        catch (...) {}
        return order;
    }

    std::vector<BrokerOrder> MetaTraderConnector::GetOpenOrders()
    {
        std::string url = MakeUrl("/users/current/accounts/" +
            config_.credentials.accountId + "/orders");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<BrokerOrder> orders;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j)
            {
                BrokerOrder order;
                order.orderId = item.value("id", "");
                order.symbol = item.value("symbol", "");
                order.quantity = item.value("volume", 0.f);
                order.price = item.value("openPrice", 0.f);
                order.status = OrderStatus::Pending;
                orders.push_back(order);
            }
        }
        catch (...) {}
        return orders;
    }


    BrokerBalance MetaTraderConnector::GetBalance()
    {
        std::string url = MakeUrl("/users/current/accounts/" +
            config_.credentials.accountId + "/account-information");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        BrokerBalance bal;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            bal.cash = j.value("balance", 0.f);
            bal.equity = j.value("equity", 0.f);
            bal.buyingPower = j.value("freeMargin", j.value("margin_free", 0.f));
            bal.currency = j.value("currency", "USD");
        }
        catch (...) {}
        return bal;
    }

    std::vector<BrokerPosition> MetaTraderConnector::GetPositions()
    {
        std::string url = MakeUrl("/users/current/accounts/" +
            config_.credentials.accountId + "/positions");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<BrokerPosition> positions;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j)
            {
                BrokerPosition pos;
                pos.symbol = item.value("symbol", "");
                float vol = item.value("volume", 0.f);
                std::string type = item.value("type", "");
                pos.quantity = (type == "POSITION_TYPE_SELL") ? -vol : vol;
                pos.avgCost = item.value("openPrice", 0.f);
                pos.unrealizedPnL = item.value("unrealizedProfit", item.value("profit", 0.f));
                pos.marketValue = item.value("currentPrice", 0.f) * std::abs(pos.quantity);
                positions.push_back(pos);
            }
        }
        catch (...) {}
        return positions;
    }


    void MetaTraderConnector::SetOnTick(OnTickCallback cb) { std::lock_guard<std::mutex> lock(cbMutex_); onTick_ = std::move(cb); }
    void MetaTraderConnector::SetOnOrder(OnOrderCallback cb) { std::lock_guard<std::mutex> lock(cbMutex_); onOrder_ = std::move(cb); }
    void MetaTraderConnector::SetOnPosition(OnPositionCallback cb) { std::lock_guard<std::mutex> lock(cbMutex_); onPosition_ = std::move(cb); }
    void MetaTraderConnector::SetOnError(OnErrorCallback cb) { std::lock_guard<std::mutex> lock(cbMutex_); onError_ = std::move(cb); }
    void MetaTraderConnector::SetOnConnection(OnConnectionCallback cb) { std::lock_guard<std::mutex> lock(cbMutex_); onConnection_ = std::move(cb); }

    float MetaTraderConnector::GetLatencyMs() const { return ws_.GetLatencyMs(); }
    uint64_t MetaTraderConnector::GetTicksReceived() const { return ticksReceived_.load(std::memory_order_relaxed); }


    std::string MetaTraderConnector::GetAuthHeader() const
    {
        return "auth-token: " + config_.credentials.apiKey;
    }

    std::string MetaTraderConnector::MakeUrl(const std::string& path) const
    {
        return config_.restBaseUrl + path;
    }

    void MetaTraderConnector::HandleWsMessage(const std::string& data)
    {
        try
        {
            auto j = nlohmann::json::parse(data);
            std::string type = j.value("type", "");

            if (type == "tick")
            {
                BrokerTick tick;
                tick.symbol    = j.value("symbol", "");
                tick.bid       = j.value("bid", 0.f);
                tick.ask       = j.value("ask", 0.f);
                tick.last      = j.value("last", tick.Mid()); // MT5 often only has bid/ask
                tick.volume    = j.value("volume", 0.f);
                tick.timestamp = std::time(nullptr);

                {
                    std::lock_guard<std::mutex> lock(tickMutex_);
                    tickCache_[tick.symbol] = tick;
                }

                ticksReceived_.fetch_add(1, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onTick_) onTick_(tick);
            }
            else if (type == "tradeResult" || type == "orderUpdate")
            {
                BrokerOrder order;
                order.orderId = std::to_string(j.value("orderId", 0));
                order.symbol = j.value("symbol", "");
                order.avgFillPrice = j.value("price", 0.f);
                order.quantity = j.value("volume", 0.f);

                std::string status = j.value("status", j.value("state", ""));
                if (status == "filled" || status == "ORDER_STATE_FILLED")
                    order.status = OrderStatus::Filled;
                else if (status == "cancelled" || status == "ORDER_STATE_CANCELED")
                    order.status = OrderStatus::Cancelled;
                else if (status == "rejected")
                    order.status = OrderStatus::Rejected;
                else
                    order.status = OrderStatus::Pending;

                order.errorMsg = j.value("message", j.value("error", ""));

                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onOrder_) onOrder_(order);
            }
            else if (type == "error")
            {
                std::string msg = j.value("message", j.value("error", "Unknown"));
                spdlog::warn("[MT5] Error: {}", msg);
                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onError_) onError_(msg, j.value("numericCode", 0));
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("[MT5] WS parse error: {}", e.what());
        }
    }

} // namespace stnks
