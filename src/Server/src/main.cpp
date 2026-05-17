#include <Server/StrategyServer.hpp>
#include <Server/HttpApiServer.hpp>
#include <Server/BrokerAction.hpp>
#include <Server/SentimentAction.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <csignal>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>

static stnks::StrategyServer* g_server = nullptr;

void SignalHandler(int sig)
{
    spdlog::info("[Server] Received signal {}, shutting down...", sig);
    if (g_server)
        g_server->Stop();
}

// void SetupLogger(const std::string& logFile)
// {
//     auto console = std::make_shared<spdlog::sinks::bas>();
//     auto file    = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, false); // append mode
//
//     console->set_level(spdlog::level::info);
//     file->set_level(spdlog::level::info);
//
//     std::vector<spdlog::sink_ptr> sinks{console, file};
//     auto logger = std::make_shared<spdlog::logger>("stnks", sinks.begin(), sinks.end());
//     logger->set_level(spdlog::level::info);
//     logger->flush_on(spdlog::level::info); // flush every message so crashes don't lose logs
//
//     spdlog::set_default_logger(logger);
// }

int main(int argc, char* argv[])
{
    // Pre-scan for --log before setting up logger
    std::string logFile = "app/stnks-server.log";
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--log" && i + 1 < argc)
            logFile = argv[++i];

    // SetupLogger(logFile);
    spdlog::info("=== STNKS Strategy Server ===");

    // Parse args
    stnks::StrategyServer::Config   serverConfig;
    stnks::HttpApiServer::Config    apiConfig;
    stnks::BrokerAction::Config     brokerConfig;
    stnks::SentimentAction::Config  sentimentConfig;

    // Read API keys from environment (can be overridden by args)
    const char* envGnews  = std::getenv("GNEWS_API_KEY");
    const char* envClaude = std::getenv("CLAUDE_API_KEY");
    if (envGnews)  sentimentConfig.gnewsApiKey  = envGnews;
    if (envClaude) sentimentConfig.claudeApiKey = envClaude;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--interval" && i + 1 < argc)
            serverConfig.pollIntervalSec = std::atoi(argv[++i]);
        else if (arg == "--sentiment-interval" && i + 1 < argc)
            serverConfig.sentimentIntervalSec = std::atoi(argv[++i]);
        else if (arg == "--broker-url" && i + 1 < argc)
            brokerConfig.baseUrl = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc)
            brokerConfig.apiKey = argv[++i];
        else if (arg == "--account" && i + 1 < argc)
            brokerConfig.accountId = argv[++i];
        else if (arg == "--gnews-key" && i + 1 < argc)
            sentimentConfig.gnewsApiKey = argv[++i];
        else if (arg == "--claude-key" && i + 1 < argc)
            sentimentConfig.claudeApiKey = argv[++i];
        else if (arg == "--db" && i + 1 < argc)
            serverConfig.dbName = argv[++i];
        else if (arg == "--log" && i + 1 < argc)
            ++i; // Already handled in pre-scan
        else if (arg == "--port" && i + 1 < argc)
            apiConfig.port = std::atoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc)
            apiConfig.host = argv[++i];
        else if (arg == "--help" || arg == "-h")
        {
            spdlog::info("Usage: stnks-server [options]");
            spdlog::info("  --interval <sec>          Price poll interval (default: 60)");
            spdlog::info("  --sentiment-interval <sec> Sentiment analysis interval (default: 600)");
            spdlog::info("  --port <port>              HTTP API port (default: 8099)");
            spdlog::info("  --host <host>              HTTP API bind address (default: 0.0.0.0)");
            spdlog::info("  --broker-url <url>         Broker API base URL");
            spdlog::info("  --api-key <key>            Broker API key (omit for dry-run)");
            spdlog::info("  --account <id>             Broker account ID");
            spdlog::info("  --gnews-key <key>          GNews API key (or set GNEWS_API_KEY env)");
            spdlog::info("  --claude-key <key>         Claude API key (or set CLAUDE_API_KEY env)");
            spdlog::info("  --db <name>                Database filename (default: strategies.db)");
            spdlog::info("  --log <path>               Log file path (default: app/stnks-server.log)");
            return 0;
        }
    }

    // Register signal handlers for clean shutdown
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Create server
    stnks::StrategyServer server(serverConfig);
    g_server = &server;

    // Create a temporary ThreadRegistry for the broker's HttpClient
    stnks::ThreadRegistry brokerThreads(1);
    stnks::HttpClient brokerHttp(brokerThreads);

    // Register actions
    server.AddAction(std::make_unique<stnks::BrokerAction>(brokerHttp, brokerConfig));
    server.AddAction(std::make_unique<stnks::SentimentAction>(brokerHttp, brokerThreads, sentimentConfig));

    // Start HTTP API server (includes ENet market feed server)
    stnks::HttpApiServer apiServer(server.GetStore(), server.GetMarket(), &server, apiConfig);
    apiServer.Start();

    // Wire up tick broadcast: when StrategyServer fetches market data, broadcast via ENet
    server.SetTickCallback([&apiServer](const std::string& symbol, const stnks::StockQuote& quote) {
        if (quote.candles.empty()) return;
        auto& last = quote.candles.back();
        apiServer.BroadcastTick(symbol, last.close, last.open, last.high, last.low, last.volume, last.timestamp);
    });

    // Poll ENet during idle periods to keep client connections alive
    server.SetIdleCallback([&apiServer]() {
        apiServer.GetFeedServer().Poll();
    });

    // Run monitoring loop (blocks until Ctrl+C or Stop())
    spdlog::info("[Server] Starting with {}s poll / {}s sentiment interval / API on port {}...",
                 serverConfig.pollIntervalSec, serverConfig.sentimentIntervalSec, apiConfig.port);
    server.Run();

    apiServer.Stop();
    g_server = nullptr;
    spdlog::info("[Server] Clean exit");
    return 0;
}
