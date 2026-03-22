# Snora

GPU-powered adaptive sleep audio service. Generates personalized white noise and nature soundscapes in real-time based on physiological signals (heart rate, HRV, respiration, stress level, mood) and streams audio to clients via Agora Server Gateway SDK.

```
┌─────────────────────────────────────────────────────────┐
│  Docker Container (nvidia/cuda)                         │
│                                                         │
│  ┌──────────────┐     ┌───────────┐     ┌────────────┐ │
│  │  Node.js API │────▶│   Redis   │◀────│  Worker    │ │
│  │  (Fastify)   │     │ (jobs +   │     │  Manager   │ │
│  │              │     │  pub/sub) │     │            │ │
│  └──────────────┘     └───────────┘     └─────┬──────┘ │
│                                               │ spawn  │
│                                  Unix Socket  │        │
│                                               ▼        │
│  ┌─────────────────────────────────────────────────┐   │
│  │  CUDA Audio Engine (C++ process, per session)   │   │
│  │                                                  │   │
│  │  Noise → Spectral Tilt → AM → Binaural         │   │
│  │  Nature WAV → Procedural → Mixer → Agora SDK   │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## Audio Pipeline

Each engine process runs a 10ms frame loop (48kHz stereo 16-bit):

- **Noise generator** — white noise with Paul Kellett pinking filter
- **Spectral tilt** — IIR shelving filter, slope adapts to stress level
- **Amplitude modulation** — respiration entrainment, guides breathing toward 5.5 bpm
- **Binaural beats** — L/R frequency differential based on mood (alpha/theta/delta)
- **Nature player** — WAV loops loaded from manifest (rain, ocean, forest)
- **Procedural textures** — algorithmic rain, wind, ocean sounds
- **Mixer** — multi-layer with smoothed gains, clipping prevention

All parameters smooth over time using one-pole exponential filters. CPU fallback mode (`SNORA_CPU_MODE`) replaces CUDA kernels with plain C++ for CI/testing.

## Quick Start

```bash
# Prerequisites: Node.js 20+, Redis, CMake, libsndfile
cp .env.example .env  # edit with your values

# Node.js services
npm install
npm run dev:api      # API server on :8080
npm run dev:worker   # Worker manager

# C++ engine (CPU mode for dev)
cd engine
cmake -B build -DSNORA_CPU_MODE=ON
cmake --build build
```

Or with Docker Compose:

```bash
docker compose up
```

## Environment Variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `SNORA_API_KEY` | yes | — | API authentication key |
| `REDIS_URL` | yes | — | Redis connection URL |
| `AGORA_APP_ID` | yes | — | Agora SDK app identifier |
| `ASSETS_PATH` | no | `/assets/sounds` | Path to WAV sound assets |
| `MAX_CONCURRENT_SESSIONS` | no | `4` | Max sessions per worker |
| `GPU_DEVICE_ID` | no | `0` | CUDA device index |
| `PORT` | no | `8080` | API server port |
| `LOG_LEVEL` | no | `info` | pino log level |

## API

All endpoints require `X-API-Key` header (except `/health` and `/metrics`).

```
POST   /sessions          Create a new sleep session
GET    /sessions/:id      Get session status
PUT    /sessions/:id/state   Update physiological state
PUT    /sessions/:id/token   Renew Agora token
DELETE /sessions/:id       Stop a session
GET    /health             Health check (Redis, GPU, session count)
GET    /metrics            Prometheus metrics
```

### Create Session

```bash
curl -X POST http://localhost:8080/sessions \
  -H "X-API-Key: your-key" \
  -H "Content-Type: application/json" \
  -d '{
    "user_id": "user-123",
    "agora": { "token": "agora-token", "channel": "sleep-ch" },
    "initial_state": {
      "mood": "anxious",
      "heart_rate": 82,
      "hrv": 35,
      "respiration_rate": 18,
      "stress_level": 0.7
    },
    "preferences": {
      "soundscape": "rain",
      "binaural_beats": true,
      "volume": 0.7
    }
  }'
# => {"job_id": "uuid", "status": "pending"}
```

### Update State

```bash
curl -X PUT http://localhost:8080/sessions/<job_id>/state \
  -H "X-API-Key: your-key" \
  -H "Content-Type: application/json" \
  -d '{"mood": "calm", "heart_rate": 65, "hrv": 55, "respiration_rate": 12, "stress_level": 0.2}'
```

## Testing

```bash
# Node.js tests (59 tests, requires Redis)
npm test

# C++ engine tests (88 tests, including E2E)
cd engine/build && ctest --output-on-failure

# Lint
npm run lint

# Type check
npx tsc --noEmit

# Helm
helm lint charts/snora/ --set secrets.apiKey=test --set config.agoraAppId=test
```

## Project Structure

```
src/
  api/          Fastify HTTP server, auth, routes
  worker/       Worker manager, engine bridge, heartbeat, session monitor
  shared/       Config, types, Redis, IPC protocol, job repository
engine/
  src/audio/    Noise gen, spectral tilt, AM, binaural, nature player, mixer, pipeline
  src/ipc/      Unix socket server, message framing
  src/state/    Session state, physio-to-audio parameter mapper
  src/agora/    Agora SDK wrapper (stub + real)
  tests/        88 GoogleTest tests (unit + integration + E2E)
charts/snora/   Helm chart (deployment, HPA, network policy, PDB)
docker/         Dockerfile (multi-stage CUDA + Node.js), entrypoint
```

## Tech Stack

- **Node.js 20**, TypeScript, Fastify, ioredis, pino, prom-client
- **C++17**, CUDA 12.x, CMake, nlohmann/json, libsndfile, GoogleTest
- **Agora Server Gateway SDK 4.x** for audio streaming
- **Redis 7** for job storage and pub/sub
- **Docker** (nvidia/cuda base), **Helm**, **GitHub Actions** CI/CD

## License

Proprietary. All rights reserved.
