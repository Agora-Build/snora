# Snora — Adaptive Sleep Audio Service Design Spec

## Overview

Snora is a GPU-powered adaptive sleep audio service. It generates personalized white noise and nature soundscapes in real-time based on physiological signals (heart rate, HRV, respiration rate, stress level, mood), and streams the audio to clients via Agora Server Gateway SDK.

The system targets users with sleep difficulties, generating comfortable audio that adapts to the user's internal state to promote relaxation and deeper sleep.

## System Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Docker Container (nvidia/cuda multi-stage)             │
│                                                         │
│  ┌──────────────┐     ┌───────────┐     ┌────────────┐ │
│  │  Node.js API │────▶│   Redis   │◀────│  Worker    │ │
│  │  (HTTP)      │     │ (job store│     │  Manager   │ │
│  │              │     │  + pubsub)│     │  (Node.js) │ │
│  └──────────────┘     └───────────┘     └─────┬──────┘ │
│        │                                      │        │
│        │ Unix Socket (state updates)          │ spawn  │
│        ▼                                      ▼        │
│  ┌─────────────────────────────────────────────────┐   │
│  │  CUDA Audio Engine (C++ process, per session)   │   │
│  │                                                  │   │
│  │  ┌──────────┐ ┌───────────┐ ┌────────────────┐  │   │
│  │  │ Noise Gen│ │Nature Loop│ │ Agora Gateway  │  │   │
│  │  │ (CUDA)   │ │ Player    │ │ SDK 4.x        │  │   │
│  │  └────┬─────┘ └─────┬─────┘ └───────┬────────┘  │   │
│  │       │              │               │           │   │
│  │       └──────┬───────┘               │           │   │
│  │              ▼                       │           │   │
│  │        ┌──────────┐                  │           │   │
│  │        │  Mixer   │──────────────────┘           │   │
│  │        └──────────┘                              │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  /assets (mounted volume — rain, ocean, forest loops)   │
└─────────────────────────────────────────────────────────┘
```

### Key Design Decisions

- **Multi-session per container**: One container handles multiple concurrent users for efficient GPU utilization
- **Job-based architecture**: HTTP API accepts requests and creates jobs in Redis; worker processes execute them independently. The system remains stable even if an audio engine process crashes.
- **Node.js + C++ separation**: Node.js handles HTTP API and job orchestration via IPC (Unix socket). C++ CUDA engine handles audio generation and Agora streaming as a separate process. Process isolation means a crash in one doesn't take down the other.
- **Redis for job store + pub/sub**: Persistent job state survives process crashes. Pub/sub enables real-time state update delivery.

## Component 1: Node.js HTTP API

**Framework**: Express or Fastify

**Authentication**: API key via `X-API-Key` header, validated as middleware against `SNORA_API_KEY` environment variable.

### Endpoints

| Method | Path | Description | Response |
|--------|------|-------------|----------|
| `POST` | `/sessions` | Create a new sleep session job | `202 { job_id, status: "pending" }` |
| `GET` | `/sessions/:id` | Get job status and metadata | `200 { job_id, status, ... }` |
| `PUT` | `/sessions/:id/state` | Push physiological state update | `200 { ok: true }` |
| `DELETE` | `/sessions/:id` | Stop a session gracefully | `200 { status: "stopping" }` |
| `GET` | `/health` | Health check | `200 { ok: true }` |

### POST /sessions Request Body

```json
{
  "user_id": "12345",
  "agora": {
    "app_id": "xxx",
    "token": "xxx",
    "channel": "sleep_12345"
  },
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
}
```

### PUT /sessions/:id/state Request Body

```json
{
  "user_id": "12345",
  "timestamp": 1710000000,
  "mood": "anxious",
  "heart_rate": 82,
  "hrv": 35,
  "respiration_rate": 18,
  "stress_level": 0.7
}
```

## Component 2: Job System (Redis)

### Job Lifecycle

```
pending → starting → running → stopping → stopped
                       ↓
                     failed (engine crash / timeout)
```

### Redis Key Schema

| Key | Type | Description |
|-----|------|-------------|
| `snora:job:{id}` | Hash | Job metadata: status, user_id, config, worker_pid, created_at, error |
| `snora:job:{id}:state` | Hash | Latest physiological state for the session |
| `snora:worker:{pid}` | String + TTL | Worker heartbeat, expires in 10 seconds |
| `snora:channel:state:{id}` | Pub/Sub | Channel for real-time state update delivery |

### Crash Recovery

The Worker Manager polls for jobs in `running` status whose worker heartbeat key (`snora:worker:{pid}`) has expired. These jobs are marked `failed` with an error reason. The API reports failure to the caller via `GET /sessions/:id`.

## Component 3: Worker Manager (Node.js)

Runs as a separate module alongside the API (same Node.js process or separate). Responsibilities:

- **Watch Redis** for jobs with `pending` status
- **Spawn** a C++ CUDA engine process per job, passing config via command-line args or Unix socket handshake
- **Bridge state updates**: subscribe to Redis `snora:channel:state:{id}`, forward to the engine process via Unix socket
- **Monitor child processes**: detect crashes via exit event, update job status to `failed`
- **Heartbeat**: periodically set `snora:worker:{pid}` with TTL in Redis
- **Enforce limits**: cap concurrent sessions per container based on GPU memory (`MAX_CONCURRENT_SESSIONS` env var)

## Component 4: CUDA Audio Engine (C++ Process)

One process per active session. Receives initial config on startup and state updates via Unix socket. Outputs audio frames to Agora.

### Audio Pipeline (GPU, per frame)

```
┌─────────────────────────────────────────────────┐
│  Per-frame (10ms = 480 samples @ 48kHz stereo)  │
│                                                  │
│  1. curand white noise generation                │
│  2. Spectral tilt filter (slope from HR/HRV)     │
│  3. Amplitude modulation (respiration entrainment)│
│  4. Binaural beat oscillators (L/R, delta/alpha) │
│  5. Nature loop playback (from mounted assets)   │
│  6. Procedural texture layer (rain grains, wind) │
│  7. Mixer (all layers, smooth gain per layer)    │
│  8. Output buffer → Agora sendAudioFrame()       │
└─────────────────────────────────────────────────┘
```

### Audio Format

- Sample rate: 48kHz
- Channels: 2 (stereo, required for binaural beats)
- Bits per sample: 16
- Frame size: 10ms = 960 samples (480 per channel)

### Algorithm Details

| Component | Algorithm | Execution |
|-----------|-----------|-----------|
| Noise base | curand (per-thread RNG) + Paul Kellett IIR pinking filter | GPU |
| Spectral tilt | Continuous slope parameter (0 to -6 dB/oct), based on Julius O. Smith III (Stanford CCRMA) | GPU |
| Amplitude modulation | Asymmetric raised cosine envelope at target breath rate (1:2 inhale:exhale) | GPU |
| Binaural beats | Dual sine oscillators per ear, frequency difference = target entrainment Hz | GPU |
| Nature loops | WAV decode on CPU, stream PCM samples to GPU mix buffer | CPU decode, GPU mix |
| Rain texture | Stochastic grain triggers, filtered noise bursts (2-8ms per drop) | GPU |
| Wind texture | Swept bandpass on noise, Perlin-modulated center frequency and bandwidth | GPU |
| Ocean texture | AM brown noise at wave rhythm rate (0.05-0.15Hz) | GPU |
| Mixer | Per-layer gain with one-pole exponential smoothing | GPU |
| Parameter smoother | One-pole lowpass on all control signals, stored in GPU constant memory | GPU |

### Physiological Signal → Audio Parameter Mapping

```
Spectral tilt slope:
  Input: stress_level (0.0-1.0), heart_rate, hrv
  Output: -6.0 dB/oct (deep brown) to -2.0 dB/oct (near pink)
  Higher stress / higher HR / lower HRV → deeper (more negative) slope
  Formula: slope = lerp(-6.0, -2.0, 1.0 - stress_level)

Respiration entrainment:
  Input: respiration_rate (current breaths/min)
  Output: amplitude modulation frequency
  Target: gradually guide toward 5.5 bpm (optimal for sleep HRV)
  Formula: am_freq = lerp(current_respiration, 5.5, session_progress) / 60.0

Binaural beat frequency:
  Input: stress_level, mood
  Output: frequency difference between L/R channels
  Stressed/anxious: alpha range (8-12Hz) for relaxation
  Calm/sleepy: delta range (0.5-4Hz) for deep sleep promotion
  Carrier frequency: ~200Hz

Nature layer gain:
  Input: stress_level
  Output: nature sound volume (0.0-1.0)
  Higher stress → more nature sounds (proven stress relief at ~50dBA)
  Formula: nature_gain = lerp(0.2, 0.6, stress_level)
```

### Smoothing Time Constants

| Parameter | Smoothing Duration | Rationale |
|-----------|--------------------|-----------|
| Spectral tilt | 3 seconds | Gradual color shift, imperceptible |
| Amplitude changes | 50ms minimum | Prevent clicks and pops |
| Nature layer crossfade | 15 seconds | Smooth soundscape blend |
| Sleep state transitions | 30-60 seconds | Avoid waking the user |
| Binaural beat frequency | 10 seconds | Gradual entrainment shift |

## Component 5: Agora Integration

**SDK**: Agora Server Gateway SDK for Linux C++ 4.x

### Connection Flow

1. Engine process initializes Agora service with `app_id`
2. Creates connection, joins channel with provided token
3. Creates custom audio track (PCM: 48kHz, stereo, 16-bit)
4. Main loop: every 10ms, pushes audio frame via `sendAudioFrame()`
5. On session stop: stops track, leaves channel, destroys connection

### Error Handling

- If Agora connection drops: engine continues generating audio, discards output buffers. Worker Manager detects via heartbeat and can attempt reconnect or mark job as `failed`.
- Token expiry: engine receives callback, Worker Manager can request a fresh token from the API caller.

## Component 6: Nature Sound Assets

### Mount Path

`/assets/sounds/` — mounted as a Docker volume.

### Directory Structure

```
/assets/sounds/
  manifest.json
  rain/
    light.wav
    heavy.wav
  ocean/
    waves.wav
    shore.wav
  forest/
    birds.wav
    crickets.wav
```

### manifest.json

```json
{
  "soundscapes": {
    "rain": {
      "layers": [
        { "file": "rain/light.wav", "default_gain": 0.5, "loop": true },
        { "file": "rain/heavy.wav", "default_gain": 0.0, "loop": true }
      ],
      "tags": ["calming", "masking"]
    },
    "ocean": {
      "layers": [
        { "file": "ocean/waves.wav", "default_gain": 0.4, "loop": true },
        { "file": "ocean/shore.wav", "default_gain": 0.3, "loop": true }
      ],
      "tags": ["rhythmic", "calming"]
    },
    "forest": {
      "layers": [
        { "file": "forest/birds.wav", "default_gain": 0.3, "loop": true },
        { "file": "forest/crickets.wav", "default_gain": 0.4, "loop": true }
      ],
      "tags": ["natural", "ambient"]
    }
  }
}
```

### Audio File Requirements

- Format: WAV, 48kHz, stereo, 16-bit PCM
- Seamless loop points baked into the files (start and end samples match)
- Recommended loop length: 30-120 seconds

### Extensibility

Add new soundscapes by dropping WAV files into a new folder and updating `manifest.json`. No code changes or container rebuild required. The engine reads the manifest on session start.

## Component 7: Docker Setup

### Multi-Stage Build

**Stage 1 — Build**:
- Base: `nvidia/cuda:12.x-devel-ubuntu22.04`
- Install: CMake, build-essential, Agora SDK headers/libs
- Compile: CUDA audio engine → `snora-engine` binary

**Stage 2 — Runtime**:
- Base: `nvidia/cuda:12.x-runtime-ubuntu22.04`
- Copy: `snora-engine` binary from build stage
- Install: Node.js 20 LTS
- Copy: Node.js application and `node_modules`
- Expose: port 8080
- Volume: `/assets/sounds`
- Requires: `--gpus all`, Redis connection via environment variable

### Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `SNORA_API_KEY` | API authentication key | `sk-xxx` |
| `REDIS_URL` | Redis connection string | `redis://redis:6379` |
| `AGORA_APP_ID` | Agora application ID | `agoraAppId123` |
| `ASSETS_PATH` | Path to mounted sound assets | `/assets/sounds` |
| `MAX_CONCURRENT_SESSIONS` | Max simultaneous sessions per container | `4` |
| `GPU_DEVICE_ID` | CUDA device index | `0` |
| `PORT` | HTTP API port | `8080` |

### Docker Run Example

```bash
docker run --gpus all \
  -p 8080:8080 \
  -v /path/to/sounds:/assets/sounds \
  -e SNORA_API_KEY=sk-xxx \
  -e REDIS_URL=redis://redis:6379 \
  -e AGORA_APP_ID=xxx \
  snora:latest
```

## Component 8: State Update Flow (End to End)

```
1. Client app detects state change (e.g., mood "anxious" → "calm", HR 82 → 68)

2. Client sends: PUT /sessions/:id/state
   { "mood": "calm", "heart_rate": 68, "hrv": 48, "stress_level": 0.3, ... }

3. Node.js API validates request → updates snora:job:{id}:state in Redis
   → publishes to snora:channel:state:{id} via Redis pub/sub

4. Worker Manager receives pub/sub message
   → forwards state update to engine process via Unix socket

5. Engine updates target parameters (all smoothed):
   - spectral_slope: -5.0 → -3.0 dB/oct (3s transition)
   - nature_gain: 0.6 → 0.3 (15s crossfade)
   - binaural_diff: 8Hz → 2Hz (10s smooth)
   - am_frequency: unchanged

6. Audio output morphs smoothly over 3-15 seconds
   User hears a gradual, comfortable transition — no clicks or jumps
```

## Research References

- Julius O. Smith III, "Spectral Tilt Filters", Stanford CCRMA — continuous spectral slope control
- Paul Kellett, IIR Pinking Filter — efficient 1/f noise from white noise
- Ngo et al. (2013), pink noise bursts timed to slow oscillations increase slow-wave sleep
- Scientific Reports (2024), 0.25Hz binaural beats shorten latency to slow-wave sleep
- Verron & Drettakis (2012), particle-based procedural audio for rain/wind
- Nature sound stress recovery studies — nature sounds at ~50dBA promote faster stress recovery than silence
