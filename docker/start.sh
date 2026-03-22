#!/bin/bash
set -e

WORKER_PID=""
API_PID=""

shutdown() {
    echo "[snora] Received shutdown signal, stopping processes..."
    [ -n "$WORKER_PID" ] && kill -TERM $WORKER_PID 2>/dev/null || true
    [ -n "$API_PID" ] && kill -TERM $API_PID 2>/dev/null || true
    [ -n "$WORKER_PID" ] && wait $WORKER_PID 2>/dev/null || true
    [ -n "$API_PID" ] && wait $API_PID 2>/dev/null || true
    echo "[snora] All processes stopped"
    exit 0
}

trap shutdown SIGTERM SIGINT

echo "[snora] Starting Worker Manager..."
node dist/src/worker/main.js &
WORKER_PID=$!

sleep 1

if ! kill -0 $WORKER_PID 2>/dev/null; then
    echo "[snora] ERROR: Worker Manager failed to start"
    exit 1
fi

echo "[snora] Starting API server..."
node dist/src/api/main.js &
API_PID=$!

echo "[snora] Both processes started (worker=$WORKER_PID, api=$API_PID)"

wait -n $WORKER_PID $API_PID
EXIT_CODE=$?

echo "[snora] A process exited with code $EXIT_CODE, shutting down..."
shutdown
