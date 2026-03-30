# Snora

GPU-powered adaptive sleep audio service. Generates personalized soundscapes (ocean waves, rain, wind) in real-time based on physiological signals (heart rate, HRV, respiration, stress level, mood) and streams audio to clients via Agora Server Gateway SDK.

```
┌─────────────────────────────────────────────────────────┐
│  Docker Container                                       │
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
│  │  Audio Engine (C++ process, per session)         │   │
│  │                                                  │   │
│  │  Ocean/Rain/Wind → Noise Bed → Binaural         │   │
│  │       ↓                                          │   │
│  │  Mixer → Spectral Tilt → AM → Agora SDK         │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## How It Works

### Audio Pipeline

Each engine process runs a 10ms frame loop (48kHz stereo 16-bit). Layers are mixed, then bio-adaptive effects are applied to the full mix:

**Layers:**
- **Procedural textures** — algorithmic ocean waves, rain, wind (dominant layer)
- **White/pink/brown noise** — Paul Kellett pinking filter blends white→pink→brown based on stress level; soft bed underneath the soundscape
- **Binaural beats** — L/R frequency differential based on mood (subtle)
- **Nature player** — WAV loops from asset files (optional)

**Bio-adaptive effects (applied to full mix):**
- **Spectral tilt** — IIR shelving filter shifts the tone: high stress = darker/warmer, low stress = brighter
- **Amplitude modulation** — entire soundscape pulses gently at the user's breathing rate, gradually guiding respiration toward 5.5 bpm over 20 minutes

### How Bio Data Drives Audio

| Bio Signal | Audio Effect | Range |
|---|---|---|
| **Stress level** (0-1) | Spectral tilt slope | High stress = -6 dB/oct (dark). Low stress = -2 dB/oct (bright) |
| **Mood** (anxious/neutral/calm/sleepy) | Binaural beat frequency | Anxious = alpha 8-12 Hz. Neutral = theta 4-8 Hz. Calm/sleepy = delta 0.5-4 Hz |
| **Respiration rate** (bpm) | AM frequency | Follows breath rate, guides toward 5.5 bpm over 20 min |
| **Stress level** | Nature gain | High stress = more nature (0.6). Low stress = less (0.2) |
| **Volume** (0-1) | Master volume | User preference, smoothed transitions |

All parameters smooth over time (3-15 second time constants) to prevent abrupt audio changes.

## Quick Start

```bash
# Prerequisites: Node.js 20+, Redis, CMake, libsndfile
cp .env.example .env  # edit with your values

# Node.js services
npm install
npm run dev:api      # API server on :8080
npm run dev:worker   # Worker manager

# C++ engine (auto-downloads Agora SDK)
cd engine
cmake -B build -DSNORA_CPU_MODE=ON
cmake --build build
```

Or with Docker Compose:

```bash
docker compose up
```

## App Integration Guide

### Session Lifecycle

```
Mobile App                     Snora API                    Audio Engine
    │                              │                              │
    │  1. POST /sessions           │                              │
    │  (agora token + bio state)   │                              │
    │─────────────────────────────▶│                              │
    │  { job_id, status: pending } │                              │
    │◀─────────────────────────────│  spawn engine process        │
    │                              │─────────────────────────────▶│
    │                              │  init (token, channel, prefs)│
    │                              │─────────────────────────────▶│
    │                              │         ack + running        │
    │                              │◀─────────────────────────────│
    │                              │                              │
    │  2. Join Agora channel       │                    ┌─────────┤
    │  (RTC SDK subscriber)        │                    │ audio   │
    │◀─────────────────────────────┼────────────────────┤ stream  │
    │  🔊 ocean waves playing      │                    └─────────┤
    │                              │                              │
    │  3. PUT /sessions/:id/state  │                              │
    │  (updated bio readings)      │  state_update via pub/sub    │
    │─────────────────────────────▶│─────────────────────────────▶│
    │                              │      audio adapts to mood    │
    │                              │                              │
    │  4. DELETE /sessions/:id     │                              │
    │─────────────────────────────▶│  shutdown                    │
    │                              │─────────────────────────────▶│
    │  🔇 audio stops              │                              │
```

### Step 1: Create a Session

Generate an Agora RTC token for your app, then create a session:

```bash
curl -X POST https://your-snora-host/sessions \
  -H "X-API-Key: your-key" \
  -H "Content-Type: application/json" \
  -d '{
    "user_id": "user-123",
    "agora": {
      "token": "007eJxT...",
      "channel": "sleep-user-123"
    },
    "initial_state": {
      "mood": "anxious",
      "heart_rate": 82,
      "hrv": 35,
      "respiration_rate": 18,
      "stress_level": 0.7
    },
    "preferences": {
      "soundscape": "ocean",
      "binaural_beats": true,
      "volume": 0.7
    }
  }'
```

Response:
```json
{"job_id": "a1b2c3d4-...", "status": "pending"}
```

Available soundscapes: `ocean`, `rain`, `wind`

### Step 2: Join the Agora Channel (Client Side)

Use the Agora RTC SDK in your mobile/web app to subscribe to audio on the same channel:

**Swift (iOS):**
```swift
let config = AgoraRtcEngineConfig()
config.appId = "your-agora-app-id"
let engine = AgoraRtcEngineKit.sharedEngine(with: config, delegate: self)

engine.joinChannel(byToken: token, channelId: "sleep-user-123", uid: 0)
// Audio from Snora engine will play automatically
```

**Kotlin (Android):**
```kotlin
val config = RtcEngineConfig()
config.mAppId = "your-agora-app-id"
val engine = RtcEngine.create(config)

engine.joinChannel(token, "sleep-user-123", 0)
// Audio from Snora engine will play through the device speaker
```

**Web (JavaScript):**
```javascript
import AgoraRTC from "agora-rtc-sdk-ng";

const client = AgoraRTC.createClient({ mode: "rtc", codec: "vp8" });
await client.join("your-agora-app-id", "sleep-user-123", token);
client.on("user-published", async (user, mediaType) => {
  if (mediaType === "audio") {
    await client.subscribe(user, mediaType);
    user.audioTrack.play();  // 🔊 ocean waves start playing
  }
});
```

### Step 3: Send Bio Data Updates

As the user's wearable/sensors provide new readings, push them to Snora:

```bash
# Every 1-5 seconds, send the latest biometric readings
curl -X PUT https://your-snora-host/sessions/a1b2c3d4-.../state \
  -H "X-API-Key: your-key" \
  -H "Content-Type: application/json" \
  -d '{
    "mood": "calm",
    "heart_rate": 65,
    "hrv": 55,
    "respiration_rate": 12,
    "stress_level": 0.2
  }'
```

The audio will smoothly adapt:
- Stress 0.7 → 0.2: soundscape gets brighter, nature sounds quieter
- Mood anxious → calm: binaural shifts from alpha (8-12 Hz) to delta (0.5-4 Hz)
- Respiration 18 → 12: AM envelope slows to match breathing, guiding toward 5.5 bpm

### Step 4: Renew Token (Before Expiry)

Snora sends a `token_expiring` status when the Agora token is about to expire. Renew it:

```bash
curl -X PUT https://your-snora-host/sessions/a1b2c3d4-.../token \
  -H "X-API-Key: your-key" \
  -H "Content-Type: application/json" \
  -d '{"token": "007eJxT...new-token..."}'
```

### Step 5: Stop the Session

```bash
curl -X DELETE https://your-snora-host/sessions/a1b2c3d4-... \
  -H "X-API-Key: your-key"
```

### Check Session Status

```bash
curl https://your-snora-host/sessions/a1b2c3d4-... \
  -H "X-API-Key: your-key"
# => {"job_id": "...", "status": "running", "created_at": 1774172088905}
```

Possible statuses: `pending` → `starting` → `running` → `stopping` → `stopped`

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

## API Reference

All endpoints require `X-API-Key` header (except `/health` and `/metrics`).

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/sessions` | Create a new sleep session |
| `GET` | `/sessions/:id` | Get session status |
| `PUT` | `/sessions/:id/state` | Update physiological state |
| `PUT` | `/sessions/:id/token` | Renew Agora token |
| `DELETE` | `/sessions/:id` | Stop a session |
| `GET` | `/health` | Health check (Redis, active sessions) |
| `GET` | `/metrics` | Prometheus metrics |

## Dev Testing

Test the engine locally with a real Agora channel:

```bash
# 1. Build the engine
cd engine && cmake -B build -DSNORA_CPU_MODE=ON && cmake --build build

# 2. Start the engine process
LD_LIBRARY_PATH=third_party/agora_rtc_sdk/agora_sdk \
  build/snora-engine --socket /tmp/snora-test.sock --gpu 0 &

# 3. Generate an Agora token (using atem CLI)
atem project use 1  # select your Agora project
TOKEN=$(atem token rtc create --channel my-test --uid 0 | grep "^007")

# 4. Connect and stream (sends init + periodic state updates)
npx tsx scripts/stream-live.ts /tmp/snora-test.sock YOUR_APP_ID "$TOKEN" my-test

# 5. Join "my-test" channel from any Agora RTC client to hear the audio
```

## Testing

```bash
# Node.js tests (59 tests, requires Redis)
npm test

# C++ engine tests (106 tests, including E2E)
cd engine/build && LD_LIBRARY_PATH=../third_party/agora_rtc_sdk/agora_sdk \
  ctest --output-on-failure

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
  src/agora/    Agora Server Gateway SDK 4.4.32 integration
  tests/        106 GoogleTest tests (unit + integration + E2E + audio quality)
scripts/
  stream-live.ts   Dev test script — stream to Agora with live state updates
  dev-test.ts      Quick smoke test — init, update, shutdown
charts/snora/      Helm chart (deployment, HPA, network policy, PDB)
docker/            Dockerfile (multi-stage build), Dockerfile.cuda (GPU), entrypoint
```

## Tech Stack

- **Node.js 20**, TypeScript, Fastify, ioredis, pino, prom-client
- **C++17**, CMake, nlohmann/json, libsndfile, GoogleTest
- **Agora Server Gateway SDK 4.4.32** for real-time audio streaming
- **Redis 7** for job storage and pub/sub
- **Docker**, **Helm**, **GitHub Actions** CI/CD
- Optional: **CUDA 12.x** for GPU-accelerated audio processing

## License

Proprietary. All rights reserved.
