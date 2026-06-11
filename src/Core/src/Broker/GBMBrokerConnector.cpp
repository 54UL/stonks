#include <Broker/GBMBrokerConnector.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace stnks
{
    GBMBrokerConnector::GBMBrokerConnector(HttpClient& http)
        : http_(http)
    {
    }

    GBMBrokerConnector::~GBMBrokerConnector()
    {
        Disconnect();
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────────

    void GBMBrokerConnector::Initialize(const BrokerConfig& config)
    {
        config_ = config;

        // Setup webhook receiver if configured
        if (config.webhookPort > 0)
        {
            webhookReceiver_ = std::make_unique<WebhookReceiver>();

            if (!config.webhookSecret.empty())
                webhookReceiver_->SetSecret("gbm", config.webhookSecret);

            webhookReceiver_->RegisterHandler("gbm", "*",
                [this](const WebhookEvent& e) { HandleWebhookEvent(e); });
        }

        spdlog::info("[GBM] Initialized: REST={} WS={} sandbox={}",
            config_.restBaseUrl, config_.wsUrl, config_.credentials.sandbox);
    }

    void GBMBrokerConnector::Connect()
    {
        // Step 1: Authenticate (OAuth2 token refresh)
        if (!RefreshAccessToken())
        {
            spdlog::error("[GBM] Authentication failed");
            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onError_) onError_("Authentication failed", 401);
            return;
        }

        // Step 2: Connect WebSocket for price streaming
        WsConfig wsConfig;
        wsConfig.url = config_.wsUrl;
        wsConfig.headers = {
            {"Authorization", GetAuthHeader()}
        };
        wsConfig.pingIntervalSec = 30;
        wsConfig.autoReconnect = true;

        ws_.SetOnMessage([this](const std::string& data, WsMessageType)
        {
            HandleWsMessage(data);
        });

        ws_.SetOnStateChange([this](WsState state)
        {
            spdlog::info("[GBM] WebSocket state: {}", WsStateToString(state));
            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onConnection_) onConnection_(state);
        });

        ws_.SetOnError([this](const std::string& reason, int code)
        {
            spdlog::error("[GBM] WebSocket error: {} ({})", reason, code);
            std::lock_guard<std::mutex> lock(cbMutex_);
            if (onError_) onError_(reason, code);
        });

        ws_.Connect(wsConfig);

        // Step 3: Start webhook receiver
        if (webhookReceiver_)
            webhookReceiver_->Start(config_.webhookPort);

        spdlog::info("[GBM] Connected");
    }

    void GBMBrokerConnector::Disconnect()
    {
        ws_.Disconnect();
        if (webhookReceiver_)
            webhookReceiver_->Stop();

        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.clear();
        }

        spdlog::info("[GBM] Disconnected");
    }

    WsState GBMBrokerConnector::GetConnectionState() const
    {
        return ws_.GetState();
    }

    // ── Market data ──────────────────────────────────────────────────────────────

    void GBMBrokerConnector::SubscribePrice(const std::string& symbol)
    {
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.push_back(symbol);
        }

        // Send subscription message over WebSocket
        nlohmann::json msg = {
            {"action", "subscribe"},
            {"symbols", {symbol}}
        };
        ws_.SendText(msg.dump());
        spdlog::info("[GBM] Subscribed to {}", symbol);
    }

    void GBMBrokerConnector::UnsubscribePrice(const std::string& symbol)
    {
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            subscriptions_.erase(
                std::remove(subscriptions_.begin(), subscriptions_.end(), symbol),
                subscriptions_.end());
        }

        nlohmann::json msg = {
            {"action", "unsubscribe"},
            {"symbols", {symbol}}
        };
        ws_.SendText(msg.dump());
    }

    std::vector<std::string> GBMBrokerConnector::GetSubscribedSymbols() const
    {
        std::lock_guard<std::mutex> lock(subMutex_);
        return subscriptions_;
    }

    BrokerTick GBMBrokerConnector::GetLastTick(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(tickMutex_);
        auto it = tickCache_.find(symbol);
        if (it != tickCache_.end()) return it->second;
        return {};
    }

    // ── IBrokerDataSource ───────��────────────────────────────────────────────────

    float GBMBrokerConnector::FetchCurrentPrice(const std::string& symbol)
    {
        auto tick = GetLastTick(symbol);
        if (tick.last > 0.f) return tick.last;

        // Fallback: REST quote
        std::string url = MakeUrl("/market/quotes/" + symbol);
        auto resp = http_.Get(url);
        if (!resp.Ok()) return 0.f;

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            return j.value("last", 0.f);
        }
        catch (...) { return 0.f; }
    }

    void GBMBrokerConnector::Subscribe(const std::string& symbol, TickCallback cb)
    {
        SubscribePrice(symbol);
        // Wire tick callback through the OnTick system
        SetOnTick([cb, symbol](const BrokerTick& tick) {
            if (tick.symbol == symbol)
                cb(tick.symbol, tick.last, tick.timestamp);
        });
    }

    void GBMBrokerConnector::Unsubscribe(const std::string& symbol)
    {
        UnsubscribePrice(symbol);
    }

    StockQuote GBMBrokerConnector::FetchQuote(const std::string& symbol,
                                               const std::string& interval,
                                               const std::string& range)
    {
        // GBM doesn't provide historical candles via API — return empty.
        // The MarketService will fallback to Yahoo for candle history.
        StockQuote quote;
        quote.symbol = symbol;
        quote.source = GetName();
        quote.fetchedAt = std::time(nullptr);
        return quote;
    }

    std::vector<SymbolMatch> GBMBrokerConnector::SearchSymbols(const std::string& query)
    {
        std::string url = MakeUrl("/market/search?q=" + query);
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<SymbolMatch> results;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j["results"])
            {
                SymbolMatch m;
                m.symbol   = item.value("symbol", "");
                m.name     = item.value("name", "");
                m.exchange = item.value("exchange", "BMV");
                m.type     = item.value("type", "EQUITY");
                m.source   = "GBM+";
                results.push_back(std::move(m));
            }
        }
        catch (...) {}
        return results;
    }

    // ── Order execution ──────────────────────────────────────────────────────────

    BrokerOrder GBMBrokerConnector::PlaceOrder(const std::string& symbol,
                                                OrderSide side, OrderType type,
                                                float quantity, float price,
                                                float stopPrice, OrderTimeInForce tif)
    {
        nlohmann::json body = {
            {"symbol",       symbol},
            {"side",         OrderSideStr(side)},
            {"type",         OrderTypeStr(type)},
            {"quantity",     quantity},
            {"account_id",   config_.credentials.accountId},
            {"time_in_force", tif == OrderTimeInForce::GTC ? "GTC" : "DAY"}
        };

        if (type == OrderType::Limit || type == OrderType::StopLimit)
            body["price"] = price;
        if (type == OrderType::Stop || type == OrderType::StopLimit)
            body["stop_price"] = stopPrice;

        std::string url = MakeUrl("/accounts/" + config_.credentials.accountId + "/orders");
        auto resp = http_.Post(url, body.dump(), {
            {"Authorization", GetAuthHeader()},
            {"Content-Type", "application/json"}
        });

        BrokerOrder order;
        order.symbol = symbol;
        order.side = side;
        order.type = type;
        order.quantity = quantity;
        order.price = price;
        order.stopPrice = stopPrice;
        order.createdAt = std::time(nullptr);

        if (!resp.Ok())
        {
            order.status = OrderStatus::Rejected;
            order.errorMsg = resp.error.empty() ? resp.body : resp.error;
            spdlog::error("[GBM] Order rejected: {}", order.errorMsg);
            return order;
        }

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            order.orderId = j.value("order_id", "");
            order.status = OrderStatus::Pending;
        }
        catch (...)
        {
            order.status = OrderStatus::Rejected;
            order.errorMsg = "Failed to parse response";
        }

        spdlog::info("[GBM] Order placed: {} {} {} {} @ {}",
            order.orderId, OrderSideStr(side), quantity, symbol, price);
        return order;
    }

    bool GBMBrokerConnector::CancelOrder(const std::string& orderId)
    {
        std::string url = MakeUrl("/accounts/" + config_.credentials.accountId + "/orders/" + orderId);
        auto resp = http_.Delete(url);
        return resp.Ok();
    }

    BrokerOrder GBMBrokerConnector::GetOrder(const std::string& orderId)
    {
        std::string url = MakeUrl("/accounts/" + config_.credentials.accountId + "/orders/" + orderId);
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};
        return ParseOrderJson(resp.body);
    }

    std::vector<BrokerOrder> GBMBrokerConnector::GetOpenOrders()
    {
        std::string url = MakeUrl("/accounts/" + config_.credentials.accountId + "/orders?status=open");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<BrokerOrder> orders;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j["orders"])
                orders.push_back(ParseOrderJson(item.dump()));
        }
        catch (...) {}
        return orders;
    }

    // ── Account ──────────────────────────────────────────────────────────────────

    BrokerBalance GBMBrokerConnector::GetBalance()
    {
        std::string url = MakeUrl("/accounts/" + config_.credentials.accountId + "/balance");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        BrokerBalance bal;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            bal.cash = j.value("cash", 0.f);
            bal.equity = j.value("equity", 0.f);
            bal.buyingPower = j.value("buying_power", 0.f);
            bal.currency = j.value("currency", "MXN");
        }
        catch (...) {}
        return bal;
    }

    std::vector<BrokerPosition> GBMBrokerConnector::GetPositions()
    {
        std::string url = MakeUrl("/accounts/" + config_.credentials.accountId + "/positions");
        auto resp = http_.Get(url);
        if (!resp.Ok()) return {};

        std::vector<BrokerPosition> positions;
        try
        {
            auto j = nlohmann::json::parse(resp.body);
            for (auto& item : j["positions"])
            {
                BrokerPosition pos;
                pos.symbol = item.value("symbol", "");
                pos.quantity = item.value("quantity", 0.f);
                pos.avgCost = item.value("avg_cost", 0.f);
                pos.marketValue = item.value("market_value", 0.f);
                pos.unrealizedPnL = item.value("unrealized_pnl", 0.f);
                positions.push_back(pos);
            }
        }
        catch (...) {}
        return positions;
    }

    // ── Event callbacks ──────────────────────────────────────────────────────────

    void GBMBrokerConnector::SetOnTick(OnTickCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onTick_ = std::move(cb);
    }

    void GBMBrokerConnector::SetOnOrder(OnOrderCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onOrder_ = std::move(cb);
    }

    void GBMBrokerConnector::SetOnPosition(OnPositionCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onPosition_ = std::move(cb);
    }

    void GBMBrokerConnector::SetOnError(OnErrorCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onError_ = std::move(cb);
    }

    void GBMBrokerConnector::SetOnConnection(OnConnectionCallback cb)
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        onConnection_ = std::move(cb);
    }

    float GBMBrokerConnector::GetLatencyMs() const
    {
        return ws_.GetLatencyMs();
    }

    uint64_t GBMBrokerConnector::GetTicksReceived() const
    {
        return ticksReceived_.load(std::memory_order_relaxed);
    }

    // ── Private helpers ─────���────────────────────────────────────────────────────

    bool GBMBrokerConnector::RefreshAccessToken()
    {
        nlohmann::json body = {
            {"grant_type", "refresh_token"},
            {"client_id", config_.credentials.clientId},
            {"client_secret", config_.credentials.clientSecret},
            {"refresh_token", config_.credentials.refreshToken}
        };

        std::string url = config_.restBaseUrl + "/auth/token";
        auto resp = http_.Post(url, body.dump(), {
            {"Content-Type", "application/json"}
        });

        if (!resp.Ok())
        {
            spdlog::error("[GBM] Token refresh failed: {} {}", resp.statusCode, resp.error);
            return false;
        }

        try
        {
            auto j = nlohmann::json::parse(resp.body);
            config_.credentials.accessToken = j["access_token"].get<std::string>();
            config_.credentials.tokenExpiry = std::time(nullptr) + j.value("expires_in", 3600);
            spdlog::info("[GBM] Token refreshed, expires in {}s", j.value("expires_in", 3600));
            return true;
        }
        catch (const std::exception& e)
        {
            spdlog::error("[GBM] Token parse error: {}", e.what());
            return false;
        }
    }

    std::string GBMBrokerConnector::GetAuthHeader() const
    {
        return "Bearer " + config_.credentials.accessToken;
    }

    std::string GBMBrokerConnector::MakeUrl(const std::string& path) const
    {
        return config_.restBaseUrl + path;
    }

    void GBMBrokerConnector::HandleWsMessage(const std::string& data)
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
                tick.last      = j.value("last", 0.f);
                tick.volume    = j.value("vol", 0.f);
                tick.timestamp = j.value("ts", (int64_t)0);

                // Update cache
                {
                    std::lock_guard<std::mutex> lock(tickMutex_);
                    tickCache_[tick.symbol] = tick;
                }

                ticksReceived_.fetch_add(1, std::memory_order_relaxed);

                // Notify callback
                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onTick_) onTick_(tick);
            }
            else if (type == "heartbeat")
            {
                // Server is alive
            }
            else if (type == "error")
            {
                std::string msg = j.value("message", "Unknown error");
                spdlog::warn("[GBM] WS error: {}", msg);
                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onError_) onError_(msg, 0);
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("[GBM] WS parse error: {}", e.what());
        }
    }

    void GBMBrokerConnector::HandleWebhookEvent(const WebhookEvent& event)
    {
        try
        {
            auto j = nlohmann::json::parse(event.body);
            std::string eventType = j.value("event", "");

            if (eventType == "order.filled" || eventType == "order.cancelled" || eventType == "order.rejected")
            {
                BrokerOrder order = ParseOrderJson(event.body);
                std::lock_guard<std::mutex> lock(cbMutex_);
                if (onOrder_) onOrder_(order);
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("[GBM] Webhook parse error: {}", e.what());
        }
    }

    BrokerOrder GBMBrokerConnector::ParseOrderJson(const std::string& json) const
    {
        BrokerOrder order;
        try
        {
            auto j = nlohmann::json::parse(json);
            order.orderId     = j.value("order_id", "");
            order.symbol      = j.value("symbol", "");
            order.filledQty   = j.value("filled_qty", 0.f);
            order.avgFillPrice = j.value("fill_price", j.value("avg_fill_price", 0.f));
            order.quantity    = j.value("quantity", j.value("qty", 0.f));

            std::string status = j.value("status", j.value("event", ""));
            if (status == "filled" || status == "order.filled")
                order.status = OrderStatus::Filled;
            else if (status == "cancelled" || status == "order.cancelled")
                order.status = OrderStatus::Cancelled;
            else if (status == "rejected" || status == "order.rejected")
                order.status = OrderStatus::Rejected;
            else if (status == "partial")
                order.status = OrderStatus::PartialFill;
            else
                order.status = OrderStatus::Pending;

            order.errorMsg = j.value("reason", "");
        }
        catch (...) {}
        return order;
    }

    BrokerTick GBMBrokerConnector::ParseTickJson(const std::string& json) const
    {
        BrokerTick tick;
        try
        {
            auto j = nlohmann::json::parse(json);
            tick.symbol    = j.value("symbol", "");
            tick.bid       = j.value("bid", 0.f);
            tick.ask       = j.value("ask", 0.f);
            tick.last      = j.value("last", 0.f);
            tick.volume    = j.value("vol", j.value("volume", 0.f));
            tick.timestamp = j.value("ts", j.value("timestamp", (int64_t)0));
        }
        catch (...) {}
        return tick;
    }

} // namespace stnks
