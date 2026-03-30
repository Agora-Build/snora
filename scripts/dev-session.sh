#!/bin/bash
# Full demo: start services, create a session via REST API, cycle through
# bio phases, then stop. Requires: Redis running, engine built, atem CLI.
#
# Usage:
#   ./scripts/dev-session.sh [soundscape]
#   Soundscapes: ocean (default), rain, wind

set -e

SOUNDSCAPE="${1:-ocean}"
API_PORT=8080
API_KEY="demo-api-key"
CHANNEL="snora-dev-$$"
ENGINE_BUILD="engine/build"
AGORA_LIB="engine/third_party/agora_rtc_sdk/agora_sdk"

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  [ -n "$API_PID" ] && kill $API_PID 2>/dev/null && echo "Stopped API server"
  [ -n "$WORKER_PID" ] && kill $WORKER_PID 2>/dev/null && echo "Stopped Worker Manager"
  exit 0
}
trap cleanup EXIT INT TERM

# ── Preflight checks ────────────────────────────────────────────────────

if ! command -v atem >/dev/null 2>&1; then
  echo "Error: 'atem' CLI not found."
  echo ""
  echo "Install atem:"
  echo "  npm install -g @aspect/atem"
  echo ""
  echo "Then authenticate and select a project:"
  echo "  atem login"
  echo "  atem list project"
  echo "  atem project use <number>"
  exit 1
fi

AGORA_APP_ID="$(atem config 2>/dev/null | grep -i 'app.*id' | head -1 | awk '{print $NF}' || echo '')"

if [ -z "$AGORA_APP_ID" ]; then
  echo "Error: Could not detect Agora App ID. Run 'atem project use <n>' first."
  exit 1
fi

if [ ! -f "$ENGINE_BUILD/snora-engine" ]; then
  echo "Error: Engine not built. Run: cd engine && cmake -B build -DSNORA_CPU_MODE=ON && cmake --build build"
  exit 1
fi

redis-cli ping >/dev/null 2>&1 || docker exec snora-test-redis redis-cli ping >/dev/null 2>&1 || {
  echo "Error: Redis not reachable. Start it with: docker run -d -p 6379:6379 redis:7-alpine"
  exit 1
}

echo "=== Snora Demo Session ==="
echo "Soundscape: $SOUNDSCAPE"
echo "Channel:    $CHANNEL"
echo "App ID:     ${AGORA_APP_ID:0:8}..."
echo ""

# ── Generate Agora token ────────────────────────────────────────────────

echo "Generating Agora token..."
TOKEN=$(atem token rtc create --channel "$CHANNEL" --uid 0 --role publisher --expire 3600 2>&1 | grep "^007" | head -1)
if [ -z "$TOKEN" ]; then
  echo "Error: Failed to generate Agora token"
  exit 1
fi
echo "Token OK (${#TOKEN} chars)"

# ── Start Node.js services ─────────────────────────────────────────────

echo ""
echo "Starting API server and Worker Manager..."

export SNORA_API_KEY="$API_KEY"
export REDIS_URL="redis://localhost:6379"
export AGORA_APP_ID="$AGORA_APP_ID"
export LOG_LEVEL="warn"
export PORT="$API_PORT"
export PATH="$(pwd)/$ENGINE_BUILD:$PATH"
export LD_LIBRARY_PATH="$(pwd)/$AGORA_LIB:${LD_LIBRARY_PATH:-}"

npx tsx src/worker/main.ts &
WORKER_PID=$!
sleep 1

npx tsx src/api/main.ts &
API_PID=$!
sleep 2

# Check API is up
if ! curl -sf "http://localhost:$API_PORT/health" >/dev/null 2>&1; then
  echo "Error: API server failed to start"
  exit 1
fi
echo "API server running on :$API_PORT"
echo "Worker Manager running (PID $WORKER_PID)"

# ── Create session via REST API ─────────────────────────────────────────

echo ""
echo "Creating session..."
CREATE_RESPONSE=$(curl -sf -X POST "http://localhost:$API_PORT/sessions" \
  -H "X-API-Key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d "{
    \"user_id\": \"demo-user\",
    \"agora\": { \"token\": \"$TOKEN\", \"channel\": \"$CHANNEL\" },
    \"initial_state\": {
      \"mood\": \"anxious\",
      \"heart_rate\": 90,
      \"hrv\": 25,
      \"respiration_rate\": 20,
      \"stress_level\": 0.8
    },
    \"preferences\": {
      \"soundscape\": \"$SOUNDSCAPE\",
      \"binaural_beats\": true,
      \"volume\": 0.8
    }
  }")

JOB_ID=$(echo "$CREATE_RESPONSE" | jq -r '.job_id')
STATUS=$(echo "$CREATE_RESPONSE" | jq -r '.status')
echo "Session created: $JOB_ID ($STATUS)"

# Wait for session to start running
echo "Waiting for engine to start..."
for i in $(seq 1 30); do
  SESSION_STATUS=$(curl -sf "http://localhost:$API_PORT/sessions/$JOB_ID" \
    -H "X-API-Key: $API_KEY" | jq -r '.status')
  if [ "$SESSION_STATUS" = "running" ]; then
    break
  fi
  sleep 1
done
echo "Session status: $SESSION_STATUS"

if [ "$SESSION_STATUS" != "running" ]; then
  echo "Warning: Session did not reach 'running' state (got: $SESSION_STATUS)"
  echo "Audio may still work — the engine streams even if Agora connection is slow"
fi

echo ""
echo "============================================"
echo "  STREAMING on channel: $CHANNEL"
echo "  Join with App ID: ${AGORA_APP_ID:0:8}..."
echo "  Soundscape: $SOUNDSCAPE"
echo "============================================"
echo ""

# ── Cycle through bio phases via REST API ───────────────────────────────

PHASES=(
  "ANXIOUS|anxious|0.9|95|20|22"
  "STRESSED|stressed|0.7|85|30|18"
  "NEUTRAL|neutral|0.5|75|40|15"
  "CALM|calm|0.2|62|55|10"
  "SLEEPY|sleepy|0.05|55|65|7"
)

PHASE_DURATION=15

echo "Cycling through bio phases (${PHASE_DURATION}s each)..."
echo "Press Ctrl+C to stop."
echo ""

cycle=1
while true; do
  for phase_data in "${PHASES[@]}"; do
    IFS='|' read -r label mood stress hr hrv resp <<< "$phase_data"

    ts=$(date +%H:%M:%S)
    echo "[$ts] Phase: $label (stress=$stress, mood=$mood, hr=$hr, resp=$resp)"

    curl -sf -X PUT "http://localhost:$API_PORT/sessions/$JOB_ID/state" \
      -H "X-API-Key: $API_KEY" \
      -H "Content-Type: application/json" \
      -d "{
        \"mood\": \"$mood\",
        \"heart_rate\": $hr,
        \"hrv\": $hrv,
        \"respiration_rate\": $resp,
        \"stress_level\": $stress
      }" >/dev/null 2>&1

    sleep $PHASE_DURATION
  done
  cycle=$((cycle + 1))
  echo ""
  echo "--- Cycle $cycle ---"
  echo ""
done
