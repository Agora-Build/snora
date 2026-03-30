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

Each engine process runs a 10ms frame loop (48kHz stereo 16-bit). Layers are mixed, then bio-adaptive effects shape the entire output:

**Layers:**
- **Procedural textures** — algorithmic ocean waves, rain, wind (dominant layer, intensity adapts to stress)
- **Noise bed** — white/pink/brown noise via Paul Kellett pinking filter, amplitude scales with stress level
- **Binaural beats** — L/R frequency differential based on mood (subtle)
- **Nature player** — WAV loops from asset files (optional)

**Bio-adaptive effects (applied to full mix):**
- **Spectral tilt** — 3-pole cascaded IIR filter shifts the entire soundscape tone: stressed = dark/warm, calm = bright/clear
- **Amplitude modulation** — entire soundscape pulses gently at the user's breathing rate (depth 0.4-1.0), gradually guiding respiration toward 5.5 bpm over 20 minutes

### How Bio Data Drives Audio

| Bio Signal | Audio Effect | Range |
|---|---|---|
| **Stress level** (0-1) | Spectral tilt slope | High stress = steep (-6 dB/oct, dark/muffled). Low stress = flat (-2 dB/oct, bright/clear) |
| **Stress level** | Texture intensity | Calm = louder soundscape (1.0). Stressed = quieter (0.5) |
| **Stress level** | Noise bed amplitude | Calm = soft (0.08). Stressed = louder (0.33) |
| **Mood** (anxious/neutral/calm/sleepy) | Binaural beat frequency | Anxious = alpha 8-12 Hz. Neutral = theta 4-8 Hz. Calm/sleepy = delta 0.5-4 Hz |
| **Respiration rate** (bpm) | AM frequency | Follows breath rate, guides toward 5.5 bpm over 20 min |
| **Stress level** | Nature gain | High stress = more nature (0.6). Low stress = less (0.2) |
| **Soundscape** (ocean/rain/wind) | Procedural texture | Switchable mid-session via state update |
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
    │  (bio data + soundscape)     │  state_update via pub/sub    │
    │─────────────────────────────▶│─────────────────────────────▶│
    │                              │  audio adapts: tone, rhythm, │
    │                              │  texture, soundscape switch   │
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
    user.audioTrack.play();  // ocean waves start playing
  }
});
```

### Step 3: Send Bio Data Updates

As the user's wearable/sensors provide new readings, push them to Snora. You can also switch the soundscape mid-session:

```bash
# Send bio readings (every 1-5 seconds)
curl -X PUT https://your-snora-host/sessions/a1b2c3d4-.../state \
  -H "X-API-Key: your-key" \
  -H "Content-Type: application/json" \
  -d '{
    "mood": "calm",
    "heart_rate": 65,
    "hrv": 55,
    "respiration_rate": 12,
    "stress_level": 0.2,
    "soundscape": "ocean"
  }'
```

The `soundscape` field is optional — include it to switch textures mid-session.

What the listener hears when bio data changes:
- **Stress 0.8 → 0.2**: tone shifts from dark/muffled to bright/clear, ocean waves get louder, noise fades
- **Mood anxious → calm**: binaural shifts from alpha (8-12 Hz) to delta (0.5-4 Hz)
- **Respiration 20 → 10**: breathing pulse slows, guides toward 5.5 bpm over 20 minutes
- **Soundscape rain → ocean**: texture crossfades from rain crackle to rolling waves

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
| `PUT` | `/sessions/:id/state` | Update bio state and/or soundscape |
| `PUT` | `/sessions/:id/token` | Renew Agora token |
| `DELETE` | `/sessions/:id` | Stop a session |
| `GET` | `/health` | Health check (Redis, active sessions) |
| `GET` | `/metrics` | Prometheus metrics |

## Dev Testing

### Full stack demo (API + Worker + Engine)

```bash
# Prerequisites: atem CLI (npm install -g @agora-build/atem), Redis
# Select your Agora project first:
atem login
atem list project
atem project use 1

# Run the full demo — starts everything, cycles through bio phases
./scripts/dev-session.sh          # default: ocean
./scripts/dev-session.sh rain     # or: rain, wind

# Join the printed channel name from any Agora RTC client to hear audio.
# Phases cycle every 15 seconds:
#   1. ANXIOUS  (rain, dark tone, fast breathing pulse)
#   2. STRESSED (rain, slightly darker)
#   3. NEUTRAL  (wind, balanced tone)
#   4. CALM     (ocean, bright tone, slow pulse)
#   5. SLEEPY   (ocean, brightest, deep slow pulse)
```

### Engine-only test (IPC direct)

```bash
# Build the engine
cd engine && cmake -B build -DSNORA_CPU_MODE=ON && cmake --build build

# Start the engine process
LD_LIBRARY_PATH=third_party/agora_rtc_sdk/agora_sdk \
  build/snora-engine --socket /tmp/snora-test.sock --gpu 0 &

# Generate token and stream with phase cycling
TOKEN=$(atem token rtc create --channel my-test --uid 0 | grep "^007")
npx tsx scripts/stream-live.ts /tmp/snora-test.sock YOUR_APP_ID "$TOKEN" my-test ocean
```

### Quick smoke test

```bash
# Verify engine IPC works (no need to join Agora channel)
npx tsx scripts/dev-test.ts /tmp/snora-test.sock YOUR_APP_ID "$TOKEN" my-test
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
  dev-session.sh   Full stack demo — API + Worker + Engine + bio phase cycling
  stream-live.ts   Engine-only streaming with live state updates
  dev-test.ts      Quick IPC smoke test
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

MIT
