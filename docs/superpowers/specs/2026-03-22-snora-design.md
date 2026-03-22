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
│                                               │        │
│                                  Unix Socket  │ spawn  │
│                                               ▼        │
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
- **Redis for job store + pub/sub**: Persistent job state survives process crashes. Pub/sub enables real-time state update delivery. Redis should be configured with AOF persistence (`appendonly yes`) to survive Redis restarts.

### IPC Protocol (Unix Socket)

The Worker Manager communicates with each engine process via a Unix domain socket at `/tmp/snora-engine-{job_id}.sock`.

**Message framing**: Length-prefixed JSON. Each message is a 4-byte big-endian uint32 (payload length) followed by a UTF-8 JSON payload.

**Message types**:

| Type | Direction | Description |
|------|-----------|-------------|
| `init` | Manager → Engine | Initial session config (agora app_id/token/channel, preferences, initial state) |
| `state_update` | Manager → Engine | New physiological state from client |
| `shutdown` | Manager → Engine | Graceful stop request |
| `ack` | Engine → Manager | Acknowledgement of received message |
| `token_update` | Manager → Engine | New Agora token; engine calls `renewToken()` on the Agora connection |
| `status` | Engine → Manager | Engine status report (running, error, no_subscribers, subscriber_joined, token_expiring, agora_disconnected) |

**Example message**:
```json
{"type": "state_update", "data": {"heart_rate": 68, "mood": "calm", "stress_level": 0.3, ...}}
```

**Timeout**: If the engine does not send an `ack` within 5 seconds, the Worker Manager considers it unresponsive and marks the job as `failed`.

**Max message size**: 64KB. Messages exceeding this limit are dropped and logged as a warning. Malformed JSON payloads or unknown message types are logged and dropped (not fatal).

**Ack timeout**: The 5-second timeout is intentional — a real-time audio engine that cannot respond within 5 seconds is effectively hung. No retry; the job is marked `failed` immediately so the client can create a new session.

## Component 1: Node.js HTTP API

**Framework**: Fastify (chosen for built-in JSON schema validation, superior performance, and native TypeScript support)

**Authentication**: API key via `X-API-Key` header, validated as middleware against `SNORA_API_KEY` environment variable.

### Endpoints

| Method | Path | Description | Response |
|--------|------|-------------|----------|
| `POST` | `/sessions` | Create a new sleep session job | `202 { job_id, status: "pending" }` |
| `GET` | `/sessions/:id` | Get job status and metadata | `200 { job_id, status, ... }` |
| `PUT` | `/sessions/:id/state` | Push physiological state update | `200 { ok: true }` |
| `PUT` | `/sessions/:id/token` | Renew Agora token for a running session | `200 { ok: true }` |
| `DELETE` | `/sessions/:id` | Stop a session gracefully | `200 { status: "stopping" }` |
| `GET` | `/health` | Health check | `200 { ok: true }` |

### POST /sessions Request Body

The Agora App ID is shared across all sessions via the `AGORA_APP_ID` environment variable and cannot be overridden per session.

```json
{
  "user_id": "12345",
  "agora": {
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
  "timestamp": 1710000000,
  "mood": "anxious",
  "heart_rate": 82,
  "hrv": 35,
  "respiration_rate": 18,
  "stress_level": 0.7
}
```

### Input Validation Rules

| Field | Type | Range | Required |
|-------|------|-------|----------|
| `user_id` | string | 1-128 chars, alphanumeric + hyphens + underscores | Yes (in POST /sessions) |
| `mood` | string enum | `"anxious"`, `"stressed"`, `"neutral"`, `"calm"`, `"relaxed"`, `"sleepy"` | Yes |
| `heart_rate` | integer | 30-220 bpm | Yes |
| `hrv` | integer | 5-300 ms (RMSSD) | Yes |
| `respiration_rate` | float | 4-40 breaths/min | Yes |
| `stress_level` | float | 0.0-1.0 | Yes |
| `timestamp` | integer | Unix epoch seconds | Optional (server uses current time if omitted) |
| `soundscape` | string | Must match a key in manifest.json | Yes (in preferences) |
| `binaural_beats` | boolean | `true` or `false` | No (default: `true`, in preferences) |
| `volume` | float | 0.0-1.0 | Yes (in preferences) |

State updates are throttled to a maximum of 1 per second per session. Excess updates are silently dropped (latest wins) — the API still returns `200 { ok: true }` to avoid client retry storms.

## Component 2: Job System (Redis)

### Job Lifecycle

```
pending → starting → running → stopping ─┬→ stopped
                       │                  └→ client_disconnected
                       └→ failed (engine crash / timeout)
```

See Component 9 for `client_disconnected` details.

### Redis Key Schema

| Key | Type | Description |
|-----|------|-------------|
| `snora:job:{id}` | Hash | Job metadata: status, user_id, config, worker_pid, created_at, error, stop_reason, token_status. Job ID is a UUID v4 generated by the API on session creation. |
| `snora:job:{id}:state` | Hash | Latest physiological state for the session |
| `snora:worker:{pid}` | String + TTL | Worker heartbeat, expires in 10 seconds |
| `snora:channel:state:{id}` | Pub/Sub | Channel for real-time state update delivery |

### Crash Recovery

The Worker Manager polls for jobs in `running` status whose worker heartbeat key (`snora:worker:{pid}`) has expired. These jobs are marked `failed` with an error reason. The API reports failure to the caller via `GET /sessions/:id`.

**Orphaned job recovery**: If the Worker Manager itself crashes, its heartbeat key expires. On startup, a new Worker Manager instance scans Redis for all jobs in `running` or `starting` status whose worker heartbeat has expired, and marks them as `failed` with reason `worker_crashed`. This ensures orphaned jobs don't remain in `running` state indefinitely.

## Component 3: Worker Manager (Node.js)

Runs as a **separate Node.js process** from the API. This ensures the API stays responsive even if the Worker Manager is busy spawning/monitoring engines, and a crash in either process does not affect the other. Responsibilities:

- **Watch Redis** for jobs with `pending` status
- **Spawn** a C++ CUDA engine process per job, passing config via command-line args or Unix socket handshake
- **Bridge state updates**: subscribe to Redis `snora:channel:state:{id}`, forward to the engine process via Unix socket
- **Monitor child processes**: detect crashes via exit event, update job status to `failed`
- **Heartbeat**: set `snora:worker:{pid}` with 10s TTL in Redis every 5 seconds
- **Enforce limits**: cap concurrent sessions per container based on GPU memory (`MAX_CONCURRENT_SESSIONS` env var). Estimated GPU memory per session: ~50MB (curand state, audio buffers, nature loop PCM data, filter state). With a 4GB GPU, `MAX_CONCURRENT_SESSIONS=4` is a conservative default leaving headroom for CUDA overhead.
- **Session limits**: max session duration of 12 hours (auto-stop). Idle timeout of 30 minutes (no state updates received → auto-stop). Both configurable via env vars `MAX_SESSION_DURATION_HOURS` and `IDLE_TIMEOUT_MINUTES`.

### Graceful Shutdown

When the container receives SIGTERM:

1. Stop accepting new sessions (Worker Manager stops polling for `pending` jobs)
2. Send `shutdown` message to all active engine processes via Unix socket
3. Wait up to 30 seconds for engines to leave Agora channels and exit
4. Update all remaining jobs to `stopped` status in Redis
5. Exit process

## Component 4: CUDA Audio Engine (C++ Process)

One process per active session. Receives initial config on startup and state updates via Unix socket. Outputs audio frames to Agora.

### Audio Pipeline (GPU, per frame)

```
┌─────────────────────────────────────────────────┐
│  Per-frame (10ms @ 48kHz stereo)                │
│  480 samples/channel x 2 = 960 samples = 1920B  │
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
- Frame size: 10ms = 480 samples/channel x 2 channels = 960 samples total = 1920 bytes

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
  Input: stress_level (0.0-1.0)
  Output: -6.0 dB/oct (deep brown) to -2.0 dB/oct (near pink)
  Higher stress → deeper (more negative) slope for calming effect
  Formula: slope = lerp(-6.0, -2.0, 1.0 - stress_level)

Respiration entrainment:
  Input: respiration_rate (current breaths/min)
  Output: amplitude modulation frequency
  Target: gradually guide toward 5.5 bpm (optimal for sleep HRV)
  session_progress: clamp(elapsed_minutes / 20.0, 0.0, 1.0)
    — ramps from 0 to 1 over the first 20 minutes of the session
    — at 0: AM frequency matches user's current respiration rate
    — at 1: AM frequency reaches target 5.5 bpm
  Formula: am_freq = lerp(current_respiration, 5.5, session_progress) / 60.0

Binaural beat frequency:
  Input: stress_level, mood
  Output: frequency difference between L/R channels
  Mood determines target range:
    anxious/stressed → alpha (8-12Hz) for relaxation
    neutral → theta (4-8Hz) transitional
    calm/relaxed/sleepy → delta (0.5-4Hz) for deep sleep
  Within each range, stress_level interpolates:
    For alpha: binaural_hz = lerp(8.0, 12.0, stress_level)
    For theta: binaural_hz = lerp(4.0, 8.0, stress_level)
    For delta: binaural_hz = lerp(0.5, 4.0, stress_level)
  Note: mood transitions can cause target jumps (e.g., 12Hz → 4Hz).
  The 10-second smoothing constant ensures the audio transition is gradual.
  Carrier frequency: 200Hz

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

- If Agora connection drops: engine sends `status` message with `{"reason": "agora_disconnected"}` to Worker Manager. No automatic reconnect in v1 — the job is marked `failed` with reason `agora_connection_lost`. Reconnection logic may be added in a future version.
- Token expiry: engine receives `onTokenPrivilegeWillExpire` callback, sends `status` message with `{"reason": "token_expiring"}` to Worker Manager. Worker Manager sets a `token_status: expiring` field on the job hash in Redis (the job `status` remains `running`). The client is expected to poll `GET /sessions/:id` and provide a new token via `PUT /sessions/:id/token` before expiry. If no new token is received within 60 seconds, the job is marked `failed` with reason `token_expired`.

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

- Format: WAV, 48kHz, mono or stereo, 16-bit PCM (mono files are upmixed to stereo on the CPU during WAV decode, before streaming to GPU)
- Seamless loop points baked into the files (start and end samples match)
- Recommended loop length: 30-120 seconds

### Extensibility

Add new soundscapes by dropping WAV files into a new folder and updating `manifest.json`. No code changes or container rebuild required. The engine reads and validates the manifest on session start — if `manifest.json` is missing, malformed, or references WAV files that don't exist, the engine fails fast with a clear error and the job is marked `failed`.

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
- Entrypoint: shell script (`start.sh`) that starts the Worker Manager first, then the API, with `tini` as PID 1 for proper signal handling. The API health check (`GET /health`) verifies the Worker Manager is running before reporting healthy.
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
| `MAX_SESSION_DURATION_HOURS` | Max session duration before auto-stop | `12` |
| `IDLE_TIMEOUT_MINUTES` | Auto-stop after no state updates | `30` |
| `CLIENT_DISCONNECT_GRACE_MINUTES` | Grace period after last subscriber leaves | `5` |
| `LOG_LEVEL` | Logging verbosity | `info` |

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

## Component 8: Observability

### Structured Logging

- **Node.js** (API + Worker Manager): `pino` with JSON output
- **C++ engine**: writes JSON log lines to stderr, captured by Worker Manager and enriched with `job_id`
- Every log line includes: `job_id` (when in session context), `component` (`api`, `worker`, `engine`), `timestamp`
- Log levels: `error` (crashes, failed jobs), `warn` (token expiring, throttled updates), `info` (session lifecycle events), `debug` (state updates, audio params)

### Prometheus Metrics

Exposed at `GET /metrics` on the API process. This endpoint does **not** require `X-API-Key` authentication (Prometheus scrapers expect unauthenticated access). In K8s, `/metrics` is excluded from the public Ingress and only accessible within the cluster network via the Service's ClusterIP.

| Metric | Type | Description |
|--------|------|-------------|
| `snora_sessions_active` | Gauge | Currently running sessions |
| `snora_sessions_total` | Counter | Total sessions created (labels: `status=stopped\|failed\|client_disconnected`) |
| `snora_engine_spawn_duration_seconds` | Histogram | Time from job pending to engine running |
| `snora_audio_frame_latency_ms` | Histogram | Time to generate one 10ms audio frame |
| `snora_gpu_memory_used_bytes` | Gauge | GPU memory usage (via NVML) |
| `snora_state_updates_total` | Counter | State updates received (labels: `result=accepted\|throttled`) |
| `snora_agora_connection_status` | Gauge | 1=connected, 0=disconnected per session |
| `snora_engine_crashes_total` | Counter | Engine process crashes |

### Enhanced Health Endpoint

`GET /health` response:

```json
{
  "ok": true,
  "redis": "connected",
  "gpu": { "available": true, "memory_free_mb": 6144 },
  "active_sessions": 3,
  "max_sessions": 4
}
```

## Component 9: Client Disconnect Handling

### Detection Methods (Layered)

1. **Agora channel occupancy** (primary): The C++ engine registers for Agora's `onUserOffline` callback. When the last subscriber leaves the channel, engine sends a `status` message to Worker Manager: `{"reason": "no_subscribers"}`. Worker Manager starts a grace timer.

2. **Idle timeout** (secondary): If no state updates received for 30 minutes, auto-stop (defined in Component 3).

### Grace Period Logic (Worker Manager)

- On `no_subscribers` event: start 5-minute timer
- If a subscriber rejoins within 5 minutes (Agora `onUserJoined`): cancel timer, continue session
- If timer expires: graceful stop (send `shutdown` to engine, transition through `stopping` → `client_disconnected`)
- Configurable via `CLIENT_DISCONNECT_GRACE_MINUTES` env var (default: 5)

### Job Status

New terminal status value `client_disconnected` distinguishes intentional stops from client drops, useful for analytics.

Updated lifecycle:
```
pending → starting → running → stopping ─┬→ stopped
                       │                  └→ client_disconnected
                       └→ failed (engine crash / timeout)
```

`client_disconnected` is a distinct terminal status that transitions through `stopping` (engine shutdown is still graceful). The `stop_reason` field on the job hash distinguishes the cause.

## Component 10: Security

### Transport Security

- TLS termination at K8s Ingress (cert-manager with Let's Encrypt or internal CA)
- Internal pod-to-Redis communication within cluster network, optionally Redis AUTH password

### Rate Limiting

Applied at API middleware level. Rate-limited requests receive `429 Too Many Requests` with a `Retry-After` header (seconds until the limit resets).

| Scope | Limit | Notes |
|-------|-------|-------|
| Global per API key | 100 req/sec | All endpoints |
| Session creation | 10/min per API key | Prevent runaway spawning |
| State updates | 1/sec per session | Already in spec (throttle, latest wins) |

### Secrets Management

- `SNORA_API_KEY` and Redis password stored as K8s Secrets. `AGORA_APP_ID` is a public identifier and can be stored in a ConfigMap; however, if an Agora App Certificate is used for token signing, that certificate must be stored as a Secret.
- Mounted as environment variables via `secretKeyRef` in Deployment
- Never logged, never included in error responses

### Network Policies

| Source | Destination | Allowed |
|--------|-------------|---------|
| Ingress | Snora API (port 8080) | Yes |
| Snora pods | Redis (TCP 6379) | Yes |
| Snora pods | Agora (external UDP/TCP) | Yes |
| Snora pods | Other pods | Denied |

### Data Handling

- Physiological data (HR, HRV, respiration, mood) is **transient** — exists only in Redis during active session
- Redis keys for completed/failed jobs: 24-hour TTL is applied when the job enters a terminal state (`stopped`, `failed`, `client_disconnected`), not at creation time
- No persistent storage of health data beyond the session
- API responses never echo back physiological data
- Note: if deployed in healthcare context, HIPAA/GDPR compliance requires additional review (encryption at rest, audit logging, BAA with cloud provider)

### Input Sanitization

- All inputs validated against the validation rules table in Component 1
- Job IDs validated as UUID format before Redis lookup
- Reject payloads > 4KB

## Component 11: Testing Strategy

### Node.js Tests (Vitest)

- **API unit tests**: request validation, auth middleware, endpoint handlers with mocked Redis
- **Worker Manager unit tests**: job polling logic, session limits, idle timeout, graceful shutdown sequence
- **IPC protocol tests**: message framing (length-prefix encoding/decoding), message type handling
- **Integration tests**: full API → Redis → Worker Manager flow using real Redis (via testcontainers or Docker Compose)

### CUDA Engine Tests (GoogleTest)

- **CPU fallback build**: compile engine with `SNORA_CPU_MODE` flag that replaces CUDA kernels with plain C++ equivalents. Same audio pipeline logic, no GPU required. Used in CI.
- **Unit tests**:
  - Noise generator: verify statistical properties (mean ~0, correct spectral slope)
  - Parameter smoother: verify convergence time matches spec
  - Mixer: verify gain application and no clipping
  - IPC message parsing
- **Reference audio tests**: generate a 1-second clip with known input state, compare output against a committed reference WAV file (tolerance: SNR > 60dB). Catches regressions in the audio pipeline.

### Integration Tests (Docker Compose)

```yaml
services:
  redis:
    image: redis:7-alpine
  snora:
    build: .
    depends_on: [redis]
  test-runner:
    build: ./tests/integration
    depends_on: [snora]
```

- Full lifecycle: create session → push state updates → verify job status transitions → stop session
- Mock Agora SDK: link against a stub library that records `sendAudioFrame()` calls to a file instead of streaming

### E2E Smoke Test

- Single script exercising the full API (create, update, poll status, stop)
- Runs against a real container with GPU (for GPU-equipped CI runners)
- Validates Agora connection via channel occupancy API

## Component 12: CI/CD Pipeline (GitHub Actions)

### Workflow 1: `ci.yml` (on PR and push to main)

```
Lint (parallel) ──┬── Node.js: eslint + prettier
                  └── C++: clang-format check

Test (parallel) ──┬── Node.js: vitest (unit + integration via testcontainers)
                  └── C++ CPU mode: cmake build + googletest

Docker Build ────── Multi-stage build (verify it builds, no push)
```

### Workflow 2: `release.yml` (on tag push `v*`)

```
Build & Push Docker Image ──── ghcr.io/<org>/snora:<tag>
                               ghcr.io/<org>/snora:latest

Helm Chart Package & Push ──── oci://ghcr.io/<org>/charts/snora
                               Version from Chart.yaml
```

### Workflow 3: `gpu-tests.yml` (manual dispatch)

- Runs on self-hosted GPU runner
- Full Docker build with GPU, runs E2E smoke test
- Triggered manually or on release candidates

### Container Registry

GHCR (GitHub Container Registry) for both Docker images and Helm charts (OCI).

### Versioning

- Docker image tag = git tag (e.g., `v1.0.0`)
- Helm chart version = `Chart.yaml` version (kept in sync with git tags)
- `latest` tag: updated by `release.yml` to point to the most recent release tag (not on every main branch push)

## Component 13: Kubernetes & Helm

### Architecture

- Each pod runs one Snora container (API + Worker Manager + engine processes)
- All pods share a single Redis instance (deployed separately or via Bitnami Helm chart)
- GPU scheduling via `nvidia.com/gpu: 1` resource request (NVIDIA device plugin)
- **API layer is stateless**: all state lives in Redis, so any pod can serve any HTTP request (create, state update, stop). No session affinity is required at the Ingress/Service level. Standard round-robin routing works.
- **Worker Manager routing is implicit**: state updates flow through Redis pub/sub, which automatically delivers to the Worker Manager that owns the engine process for that session. No explicit session routing needed.

### Helm Chart Structure

Located at `charts/snora/` in the repository root.

```
charts/snora/
  Chart.yaml
  values.yaml
  templates/
    deployment.yaml
    service.yaml
    configmap.yaml
    secret.yaml
    hpa.yaml
    ingress.yaml
    networkpolicy.yaml
    _helpers.tpl
```

### Key values.yaml

```yaml
replicaCount: 2

image:
  repository: ghcr.io/<org>/snora
  tag: latest

resources:
  limits:
    nvidia.com/gpu: 1
    memory: 8Gi
  requests:
    memory: 4Gi

config:
  maxConcurrentSessions: 4
  port: 8080
  assetsPath: /assets/sounds

redis:
  url: redis://redis:6379

autoscaling:
  enabled: true
  minReplicas: 1
  maxReplicas: 10
  targetSessionsPerPod: 3

ingress:
  enabled: true
  tls: true
```

### Autoscaling

HPA uses custom metric `snora_sessions_active` via Prometheus Adapter. Scale up when average sessions per pod exceeds `targetSessionsPerPod`.

**Scale-down safety**: Set `terminationGracePeriodSeconds: 45` on the pod (aligned with the 30-second graceful shutdown window + buffer). Use a `PodDisruptionBudget` with `minAvailable: 1` to prevent all pods from being terminated simultaneously. The graceful shutdown flow (Component 3) ensures active sessions are properly stopped before the pod exits.

**Missing metric at startup**: Set `behavior.scaleDown.stabilizationWindowSeconds: 300` to prevent thrashing. If `snora_sessions_active` is not yet available (Prometheus hasn't scraped), the HPA retains the current replica count (default K8s behavior).

### Chart Publishing

GitHub Actions runs `helm package` and `helm push` to `oci://ghcr.io/<org>/charts/snora` on tagged releases (handled by `release.yml` workflow).

## Component 14: State Update Flow (End to End)

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
