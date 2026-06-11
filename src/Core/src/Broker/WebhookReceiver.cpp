#include <Broker/WebhookReceiver.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <openssl/hmac.h>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace stnks
{
    struct WebhookReceiver::Impl
    {
        httplib::Server server;
    };

    WebhookReceiver::WebhookReceiver()
        : impl_(std::make_unique<Impl>())
    {
    }

    WebhookReceiver::~WebhookReceiver()
    {
        Stop();
    }

    void WebhookReceiver::Start(int port)
    {
        if (running_.load()) return;

        port_ = port;

        // Register the webhook endpoint: POST /webhook/{source}
        impl_->server.Post(R"(/webhook/(\w+))", [this](const httplib::Request& req, httplib::Response& res)
        {
            receivedCount_.fetch_add(1, std::memory_order_relaxed);

            std::string source = req.matches[1].str();
            std::string signature = req.get_header_value("X-Signature");
            std::string remoteIP = req.remote_addr;

            // Validate IP whitelist
            if (!ValidateIP(source, remoteIP))
            {
                rejectedCount_.fetch_add(1, std::memory_order_relaxed);
                spdlog::warn("[Webhook] Rejected from IP {} for source '{}'", remoteIP, source);
                res.status = 403;
                res.set_content(R"({"error":"forbidden"})", "application/json");
                return;
            }

            // Validate HMAC signature
            if (!ValidateSignature(source, req.body, signature))
            {
                rejectedCount_.fetch_add(1, std::memory_order_relaxed);
                spdlog::warn("[Webhook] Invalid signature for source '{}'", source);
                res.status = 401;
                res.set_content(R"({"error":"invalid signature"})", "application/json");
                return;
            }

            // Parse event type from body
            std::string eventType = "unknown";
            try
            {
                auto j = nlohmann::json::parse(req.body);
                if (j.contains("event"))
                    eventType = j["event"].get<std::string>();
                else if (j.contains("type"))
                    eventType = j["type"].get<std::string>();
                else if (j.contains("action"))
                    eventType = j["action"].get<std::string>();
            }
            catch (...) { /* Non-JSON body — still dispatch */ }

            WebhookEvent event;
            event.source     = source;
            event.eventType  = eventType;
            event.body       = req.body;
            event.signature  = signature;
            event.receivedAt = std::time(nullptr);

            spdlog::info("[Webhook] Received {}/{} from {}", source, eventType, remoteIP);

            DispatchEvent(event);

            res.status = 200;
            res.set_content(R"({"ok":true})", "application/json");
        });

        // Health check
        impl_->server.Get("/webhook/health", [](const httplib::Request&, httplib::Response& res)
        {
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        // Start server in background thread
        running_.store(true);
        serverThread_ = std::make_unique<std::thread>([this, port]()
        {
            spdlog::info("[Webhook] Listening on port {}", port);
            impl_->server.listen("0.0.0.0", port);
            running_.store(false);
        });
    }

    void WebhookReceiver::Stop()
    {
        if (!running_.load()) return;

        impl_->server.stop();
        if (serverThread_ && serverThread_->joinable())
            serverThread_->join();

        running_.store(false);
        spdlog::info("[Webhook] Stopped");
    }

    void WebhookReceiver::SetSecret(const std::string& source, const std::string& secret)
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        sources_[source].secret = secret;
    }

    void WebhookReceiver::RegisterHandler(const std::string& source,
                                           const std::string& eventType,
                                           WebhookHandler handler)
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        sources_[source].handlers[eventType] = std::move(handler);
    }

    void WebhookReceiver::SetAllowedIPs(const std::string& source,
                                          const std::vector<std::string>& ips)
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        sources_[source].allowedIPs = ips;
    }

    bool WebhookReceiver::ValidateSignature(const std::string& source,
                                             const std::string& body,
                                             const std::string& signature) const
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = sources_.find(source);
        if (it == sources_.end() || it->second.secret.empty())
            return true; // No secret configured — skip validation

        if (signature.empty())
            return false;

        // Expected format: "sha256=<hex>"
        std::string prefix = "sha256=";
        std::string providedHex = signature;
        if (providedHex.substr(0, prefix.size()) == prefix)
            providedHex = providedHex.substr(prefix.size());

        // Compute HMAC-SHA256
        const auto& secret = it->second.secret;
        unsigned char digest[32];
        unsigned int digestLen = 0;

        HMAC(EVP_sha256(),
             secret.data(), (int)secret.size(),
             (const unsigned char*)body.data(), body.size(),
             digest, &digestLen);

        // Convert to hex
        std::ostringstream oss;
        for (unsigned int i = 0; i < digestLen; ++i)
            oss << std::hex << std::setfill('0') << std::setw(2) << (int)digest[i];

        return oss.str() == providedHex;
    }

    bool WebhookReceiver::ValidateIP(const std::string& source, const std::string& remoteIP) const
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = sources_.find(source);
        if (it == sources_.end() || it->second.allowedIPs.empty())
            return true; // No whitelist — allow all

        for (auto& ip : it->second.allowedIPs)
            if (ip == remoteIP) return true;

        return false;
    }

    void WebhookReceiver::DispatchEvent(const WebhookEvent& event)
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = sources_.find(event.source);
        if (it == sources_.end()) return;

        auto& handlers = it->second.handlers;

        // Try exact match first
        auto h = handlers.find(event.eventType);
        if (h != handlers.end())
        {
            h->second(event);
            return;
        }

        // Try wildcard handler
        h = handlers.find("*");
        if (h != handlers.end())
            h->second(event);
    }

} // namespace stnks
