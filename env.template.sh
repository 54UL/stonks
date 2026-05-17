#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# STNKS Environment Variables Template
# ═══════════════════════════════════════════════════════════════════════════════
# Copy this file and fill in your keys:
#   cp env.template.sh .env.sh
#   source .env.sh
#
# NEVER commit .env.sh to version control!
# ═══════════════════════════════════════════════════════════════════════════════

# ─── AI: Claude API (Anthropic) ───────────────────────────────────────────────
# Used by: ClaudeAnalyzer (sentiment analysis, trade recommendations)
# Get key: https://console.anthropic.com/settings/keys
# Required for: AI-powered market analysis, recommendations, warnings, operations
# Without: Falls back to dry-run mode (rule-based analysis from headlines)
export CLAUDE_API_KEY=""

# ─── News: GNews API ─────────────────────────────────────────────────────────
# Used by: NewsService (fetches financial news for AI analysis)
# Get key: https://gnews.io/register
# Required for: Live news feed that powers AI analysis
# Without: No news data, AI analysis produces placeholder results
export GNEWS_API_KEY=""

# ─── Client-Server Mode ──────────────────────────────────────────────────────
# Used by: UI to determine monolith vs remote mode
# Format: "http://host:port" (e.g., "http://localhost:8099")
# Set: Remote mode — UI connects to standalone stnks-server via HTTP + ENet
# Unset: Monolith mode — embedded server runs in background thread
# Default: "http://localhost:8099" when set without value
export STNKS_SERVER_URL=""

# ─── Asset Path (optional) ───────────────────────────────────────────────────
# Used by: StrategyStore, config loaders
# Default: "app/" relative to working directory
# Override to specify absolute path for DB and config files
# export ASSETS_STNKS="/path/to/stnks/data"

# ═══════════════════════════════════════════════════════════════════════════════
# Server CLI overrides (alternative to env vars for stnks-server):
#   --gnews-key <key>         Override GNEWS_API_KEY
#   --claude-key <key>        Override CLAUDE_API_KEY
#   --poll-interval <sec>     Market poll interval (default: 60)
#   --sentiment-interval <s>  AI analysis interval (default: 600)
#   --host <addr>             HTTP bind address (default: 0.0.0.0)
#   --port <port>             HTTP port (default: 8099)
#   --db <name>               Database filename (default: strategies.db)
# ═══════════════════════════════════════════════════════════════════════════════
