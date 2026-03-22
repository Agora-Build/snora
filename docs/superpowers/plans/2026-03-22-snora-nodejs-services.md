# Snora Node.js Services Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Node.js API server and Worker Manager that orchestrate sleep audio sessions, managing jobs in Redis and communicating with CUDA engine processes via Unix socket IPC.

**Architecture:** Fastify HTTP API accepts session requests, stores jobs in Redis, and publishes state updates via pub/sub. A separate Worker Manager process watches Redis for pending jobs, spawns engine processes, bridges state updates via length-prefixed JSON over Unix sockets, and monitors engine health. Both processes share a common config, logger, and Redis client library.

**Tech Stack:** Node.js 20 LTS, TypeScript, Fastify, ioredis, pino, prom-client, vitest, uuid

**Spec:** `docs/superpowers/specs/2026-03-22-snora-design.md`

**Scope:** This plan covers Node.js services only (Components 1-3, 8-10 from spec). The CUDA engine (Component 4) and infrastructure (Components 7, 12, 13) are separate plans. The engine is mocked in tests using a stub process.

---

## File Structure

```
snora/
  package.json                          # Project root, scripts, dependencies
  tsconfig.json                         # TypeScript config
  vitest.config.ts                      # Vitest config
  src/
    shared/
      config.ts                         # Environment variable config with defaults
      logger.ts                         # Pino logger factory
      redis.ts                          # ioredis client factory
      types.ts                          # Shared TypeScript types (job, state, IPC messages)
      ipc-protocol.ts                   # Length-prefixed JSON framing (encode/decode)
      job-repository.ts                 # Redis CRUD for jobs (create, get, update, list)
      validation.ts                     # Shared validation schemas (Fastify JSON Schema format)
    api/
      server.ts                         # Fastify app factory (plugins, routes, hooks)
      main.ts                           # API entry point
      plugins/
        auth.ts                         # X-API-Key authentication hook
        rate-limit.ts                   # Rate limiting plugin
      routes/
        sessions.ts                     # POST/GET/PUT/DELETE /sessions routes
        health.ts                       # GET /health route
        metrics.ts                      # GET /metrics route (prom-client)
    worker/
      main.ts                           # Worker Manager entry point
      manager.ts                        # Core manager: poll jobs, spawn engines, monitor
      engine-bridge.ts                  # Unix socket IPC connection to one engine process
      heartbeat.ts                      # Redis heartbeat (set TTL key every 5s)
      session-monitor.ts                # Idle timeout, max duration, disconnect grace timers
  tests/
    unit/
      shared/
        ipc-protocol.test.ts            # Frame encode/decode, edge cases
        job-repository.test.ts          # Job CRUD with mocked Redis
        validation.test.ts              # Schema validation
        config.test.ts                  # Config parsing
      api/
        auth.test.ts                    # Auth plugin
        sessions.test.ts                # Session route handlers
        health.test.ts                  # Health endpoint
        rate-limit.test.ts              # Rate limiting behavior
      worker/
        manager.test.ts                 # Job polling, spawn, limits
        engine-bridge.test.ts           # IPC communication
        heartbeat.test.ts               # Heartbeat timing
        session-monitor.test.ts         # Timeout logic
    integration/
      session-lifecycle.test.ts         # Full API + Redis + Worker flow
    fixtures/
      mock-engine.ts                    # Stub engine process for testing
```

---

## Task 1: Project Scaffold & Config

**Files:**
- Create: `package.json`
- Create: `tsconfig.json`
- Create: `vitest.config.ts`
- Create: `src/shared/config.ts`
- Create: `src/shared/types.ts`
- Test: `tests/unit/shared/config.test.ts`

- [ ] **Step 1: Initialize project**

```bash
cd /home/guohai/Dev/Aerojet/snora
npm init -y
```

Update `package.json`:
```json
{
  "name": "snora",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "build": "tsc",
    "dev:api": "tsx src/api/main.ts",
    "dev:worker": "tsx src/worker/main.ts",
    "test": "vitest run",
    "test:watch": "vitest",
    "lint": "eslint src tests",
    "format": "prettier --write 'src/**/*.ts' 'tests/**/*.ts'"
  },
  "engines": {
    "node": ">=20"
  }
}
```

- [ ] **Step 2: Install dependencies**

```bash
npm install fastify fastify-plugin ioredis pino prom-client uuid @fastify/rate-limit
npm install -D typescript tsx vitest ajv @types/node @types/uuid eslint prettier
```

- [ ] **Step 3: Create tsconfig.json**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "Node16",
    "moduleResolution": "Node16",
    "outDir": "dist",
    "rootDir": ".",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "declaration": true,
    "sourceMap": true,
    "resolveJsonModule": true
  },
  "include": ["src/**/*.ts"],
  "exclude": ["node_modules", "dist", "tests"]
}
```

- [ ] **Step 4: Create vitest.config.ts**

```typescript
import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    globals: true,
    environment: 'node',
    include: ['tests/**/*.test.ts'],
    coverage: {
      provider: 'v8',
      include: ['src/**/*.ts'],
    },
  },
});
```

- [ ] **Step 5: Create shared types**

Create `src/shared/types.ts`:
```typescript
export const MOODS = ['anxious', 'stressed', 'neutral', 'calm', 'relaxed', 'sleepy'] as const;
export type Mood = typeof MOODS[number];

export const JOB_STATUSES = ['pending', 'starting', 'running', 'stopping', 'stopped', 'failed', 'client_disconnected'] as const;
export type JobStatus = typeof JOB_STATUSES[number];

export const TERMINAL_STATUSES: readonly JobStatus[] = ['stopped', 'failed', 'client_disconnected'] as const;

export interface PhysiologicalState {
  mood: Mood;
  heart_rate: number;
  hrv: number;
  respiration_rate: number;
  stress_level: number;
  timestamp?: number;
}

export interface SessionPreferences {
  soundscape: string;
  binaural_beats?: boolean;
  volume: number;
}

export interface CreateSessionRequest {
  user_id: string;
  agora: {
    token: string;
    channel: string;
  };
  initial_state: PhysiologicalState;
  preferences: SessionPreferences;
}

export interface Job {
  id: string;
  status: JobStatus;
  user_id: string;
  config: {
    agora: { token: string; channel: string };
    preferences: SessionPreferences;
  };
  worker_pid?: number;
  created_at: number;
  error?: string;
  stop_reason?: string;
  token_status?: string;
}

// IPC message types (Node.js ↔ C++ engine)
export type IpcMessageType = 'init' | 'state_update' | 'shutdown' | 'ack' | 'token_update' | 'status';

export interface IpcMessage {
  type: IpcMessageType;
  data?: Record<string, unknown>;
}
```

- [ ] **Step 6: Write failing config test**

Create `tests/unit/shared/config.test.ts`:
```typescript
import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { loadConfig } from '../../../src/shared/config.js';

describe('loadConfig', () => {
  const originalEnv = process.env;

  beforeEach(() => {
    process.env = { ...originalEnv };
  });

  afterEach(() => {
    process.env = originalEnv;
  });

  it('loads required env vars', () => {
    process.env.SNORA_API_KEY = 'test-key';
    process.env.REDIS_URL = 'redis://localhost:6379';
    process.env.AGORA_APP_ID = 'test-app-id';

    const config = loadConfig();
    expect(config.apiKey).toBe('test-key');
    expect(config.redisUrl).toBe('redis://localhost:6379');
    expect(config.agoraAppId).toBe('test-app-id');
  });

  it('uses defaults for optional vars', () => {
    process.env.SNORA_API_KEY = 'test-key';
    process.env.REDIS_URL = 'redis://localhost:6379';
    process.env.AGORA_APP_ID = 'test-app-id';

    const config = loadConfig();
    expect(config.port).toBe(8080);
    expect(config.maxConcurrentSessions).toBe(4);
    expect(config.maxSessionDurationHours).toBe(12);
    expect(config.idleTimeoutMinutes).toBe(30);
    expect(config.clientDisconnectGraceMinutes).toBe(5);
    expect(config.logLevel).toBe('info');
    expect(config.assetsPath).toBe('/assets/sounds');
  });

  it('throws if required vars are missing', () => {
    delete process.env.SNORA_API_KEY;
    expect(() => loadConfig()).toThrow('SNORA_API_KEY');
  });
});
```

- [ ] **Step 7: Run test to verify it fails**

```bash
npx vitest run tests/unit/shared/config.test.ts
```
Expected: FAIL — `loadConfig` not found.

- [ ] **Step 8: Implement config module**

Create `src/shared/config.ts`:
```typescript
export interface Config {
  apiKey: string;
  redisUrl: string;
  agoraAppId: string;
  assetsPath: string;
  maxConcurrentSessions: number;
  gpuDeviceId: number;
  port: number;
  maxSessionDurationHours: number;
  idleTimeoutMinutes: number;
  clientDisconnectGraceMinutes: number;
  logLevel: string;
}

function required(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

function optional(name: string, defaultValue: string): string {
  return process.env[name] ?? defaultValue;
}

export function loadConfig(): Config {
  return {
    apiKey: required('SNORA_API_KEY'),
    redisUrl: required('REDIS_URL'),
    agoraAppId: required('AGORA_APP_ID'),
    assetsPath: optional('ASSETS_PATH', '/assets/sounds'),
    maxConcurrentSessions: parseInt(optional('MAX_CONCURRENT_SESSIONS', '4'), 10),
    gpuDeviceId: parseInt(optional('GPU_DEVICE_ID', '0'), 10),
    port: parseInt(optional('PORT', '8080'), 10),
    maxSessionDurationHours: parseInt(optional('MAX_SESSION_DURATION_HOURS', '12'), 10),
    idleTimeoutMinutes: parseInt(optional('IDLE_TIMEOUT_MINUTES', '30'), 10),
    clientDisconnectGraceMinutes: parseInt(optional('CLIENT_DISCONNECT_GRACE_MINUTES', '5'), 10),
    logLevel: optional('LOG_LEVEL', 'info'),
  };
}
```

- [ ] **Step 9: Run test to verify it passes**

```bash
npx vitest run tests/unit/shared/config.test.ts
```
Expected: PASS

- [ ] **Step 10: Commit**

```bash
git add package.json tsconfig.json vitest.config.ts src/shared/config.ts src/shared/types.ts tests/unit/shared/config.test.ts
git commit -m "feat: project scaffold with config and shared types"
```

---

## Task 2: Logger & Redis Client

**Files:**
- Create: `src/shared/logger.ts`
- Create: `src/shared/redis.ts`

- [ ] **Step 1: Create logger module**

Create `src/shared/logger.ts`:
```typescript
import pino from 'pino';

export type ComponentName = 'api' | 'worker' | 'engine';

export function createLogger(component: ComponentName, level = 'info') {
  return pino({
    level,
    base: { component },
    timestamp: pino.stdTimeFunctions.isoTime,
    formatters: {
      level(label) {
        return { level: label };
      },
    },
  });
}
```

- [ ] **Step 2: Create Redis client factory**

Create `src/shared/redis.ts`:
```typescript
import Redis from 'ioredis';
import type { Logger } from 'pino';

export function createRedisClient(url: string, logger: Logger): Redis {
  const client = new Redis(url, {
    maxRetriesPerRequest: 3,
    retryStrategy(times) {
      const delay = Math.min(times * 200, 5000);
      return delay;
    },
  });

  client.on('error', (err) => {
    logger.error({ err }, 'Redis connection error');
  });

  client.on('connect', () => {
    logger.info('Redis connected');
  });

  return client;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/shared/logger.ts src/shared/redis.ts
git commit -m "feat: add pino logger and ioredis client factory"
```

---

## Task 3: IPC Protocol (Length-Prefixed JSON)

**Files:**
- Create: `src/shared/ipc-protocol.ts`
- Test: `tests/unit/shared/ipc-protocol.test.ts`

- [ ] **Step 1: Write failing IPC protocol tests**

Create `tests/unit/shared/ipc-protocol.test.ts`:
```typescript
import { describe, it, expect } from 'vitest';
import { encodeMessage, MessageDecoder } from '../../../src/shared/ipc-protocol.js';
import type { IpcMessage } from '../../../src/shared/types.js';

describe('encodeMessage', () => {
  it('produces 4-byte length prefix + JSON payload', () => {
    const msg: IpcMessage = { type: 'ack' };
    const buf = encodeMessage(msg);
    const payloadLen = buf.readUInt32BE(0);
    const payload = buf.subarray(4).toString('utf-8');
    expect(payloadLen).toBe(Buffer.byteLength(JSON.stringify(msg), 'utf-8'));
    expect(JSON.parse(payload)).toEqual(msg);
  });

  it('rejects messages exceeding 64KB', () => {
    const msg: IpcMessage = { type: 'state_update', data: { big: 'x'.repeat(70000) } };
    expect(() => encodeMessage(msg)).toThrow('exceeds max');
  });
});

describe('MessageDecoder', () => {
  it('decodes a complete message from a single buffer', () => {
    const decoder = new MessageDecoder();
    const msg: IpcMessage = { type: 'state_update', data: { heart_rate: 68 } };
    const encoded = encodeMessage(msg);

    const messages: IpcMessage[] = [];
    decoder.on('message', (m) => messages.push(m));
    decoder.feed(encoded);

    expect(messages).toHaveLength(1);
    expect(messages[0]).toEqual(msg);
  });

  it('handles fragmented data across multiple feeds', () => {
    const decoder = new MessageDecoder();
    const msg: IpcMessage = { type: 'ack' };
    const encoded = encodeMessage(msg);

    const messages: IpcMessage[] = [];
    decoder.on('message', (m) => messages.push(m));

    // Feed byte by byte
    for (let i = 0; i < encoded.length; i++) {
      decoder.feed(encoded.subarray(i, i + 1));
    }

    expect(messages).toHaveLength(1);
    expect(messages[0]).toEqual(msg);
  });

  it('handles multiple messages in one buffer', () => {
    const decoder = new MessageDecoder();
    const msg1: IpcMessage = { type: 'ack' };
    const msg2: IpcMessage = { type: 'status', data: { reason: 'running' } };
    const combined = Buffer.concat([encodeMessage(msg1), encodeMessage(msg2)]);

    const messages: IpcMessage[] = [];
    decoder.on('message', (m) => messages.push(m));
    decoder.feed(combined);

    expect(messages).toHaveLength(2);
    expect(messages[0]).toEqual(msg1);
    expect(messages[1]).toEqual(msg2);
  });

  it('emits error on malformed JSON', () => {
    const decoder = new MessageDecoder();
    const badPayload = Buffer.from('not json');
    const header = Buffer.alloc(4);
    header.writeUInt32BE(badPayload.length, 0);
    const frame = Buffer.concat([header, badPayload]);

    const errors: Error[] = [];
    decoder.on('error', (e) => errors.push(e));
    decoder.feed(frame);

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toContain('malformed');
  });

  it('emits error if payload exceeds 64KB', () => {
    const decoder = new MessageDecoder();
    const header = Buffer.alloc(4);
    header.writeUInt32BE(70000, 0); // claim 70KB

    const errors: Error[] = [];
    decoder.on('error', (e) => errors.push(e));
    decoder.feed(header);

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toContain('exceeds max');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/shared/ipc-protocol.test.ts
```
Expected: FAIL — modules not found.

- [ ] **Step 3: Implement IPC protocol**

Create `src/shared/ipc-protocol.ts`:
```typescript
import { EventEmitter } from 'node:events';
import type { IpcMessage } from './types.js';

const MAX_MESSAGE_SIZE = 64 * 1024; // 64KB
const HEADER_SIZE = 4;

export function encodeMessage(msg: IpcMessage): Buffer {
  const json = JSON.stringify(msg);
  const payload = Buffer.from(json, 'utf-8');
  if (payload.length > MAX_MESSAGE_SIZE) {
    throw new Error(`Message size ${payload.length} exceeds max ${MAX_MESSAGE_SIZE}`);
  }
  const header = Buffer.alloc(HEADER_SIZE);
  header.writeUInt32BE(payload.length, 0);
  return Buffer.concat([header, payload]);
}

export class MessageDecoder extends EventEmitter {
  private buffer = Buffer.alloc(0);
  private expectedLength: number | null = null;

  feed(data: Buffer): void {
    this.buffer = Buffer.concat([this.buffer, data]);
    this.drain();
  }

  private drain(): void {
    while (true) {
      // Read header
      if (this.expectedLength === null) {
        if (this.buffer.length < HEADER_SIZE) return;
        this.expectedLength = this.buffer.readUInt32BE(0);
        this.buffer = this.buffer.subarray(HEADER_SIZE);

        if (this.expectedLength > MAX_MESSAGE_SIZE) {
          this.emit('error', new Error(`Message size ${this.expectedLength} exceeds max ${MAX_MESSAGE_SIZE}`));
          this.expectedLength = null;
          this.buffer = Buffer.alloc(0);
          return;
        }
      }

      // Read payload
      if (this.buffer.length < this.expectedLength) return;

      const payload = this.buffer.subarray(0, this.expectedLength);
      this.buffer = this.buffer.subarray(this.expectedLength);
      this.expectedLength = null;

      try {
        const msg = JSON.parse(payload.toString('utf-8')) as IpcMessage;
        this.emit('message', msg);
      } catch {
        this.emit('error', new Error('IPC message malformed JSON'));
      }
    }
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/shared/ipc-protocol.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/shared/ipc-protocol.ts tests/unit/shared/ipc-protocol.test.ts
git commit -m "feat: IPC protocol with length-prefixed JSON framing"
```

---

## Task 4: Job Repository (Redis CRUD)

**Files:**
- Create: `src/shared/job-repository.ts`
- Test: `tests/unit/shared/job-repository.test.ts`

- [ ] **Step 1: Write failing job repository tests**

Create `tests/unit/shared/job-repository.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { JobRepository } from '../../../src/shared/job-repository.js';
import type { JobStatus } from '../../../src/shared/types.js';

// Mock ioredis
function createMockRedis() {
  const store = new Map<string, Map<string, string>>();
  return {
    hset: vi.fn(async (key: string, ...args: string[]) => {
      if (!store.has(key)) store.set(key, new Map());
      const hash = store.get(key)!;
      for (let i = 0; i < args.length; i += 2) {
        hash.set(args[i], args[i + 1]);
      }
      return args.length / 2;
    }),
    hgetall: vi.fn(async (key: string) => {
      const hash = store.get(key);
      if (!hash || hash.size === 0) return {};
      return Object.fromEntries(hash);
    }),
    expire: vi.fn(async () => 1),
    publish: vi.fn(async () => 1),
    _store: store,
  };
}

describe('JobRepository', () => {
  let redis: ReturnType<typeof createMockRedis>;
  let repo: JobRepository;

  beforeEach(() => {
    redis = createMockRedis();
    repo = new JobRepository(redis as any);
  });

  it('creates a job with pending status and UUID id', async () => {
    const job = await repo.create({
      user_id: 'user-1',
      config: {
        agora: { token: 'tok', channel: 'ch' },
        preferences: { soundscape: 'rain', volume: 0.7 },
      },
    });

    expect(job.id).toMatch(/^[0-9a-f-]{36}$/);
    expect(job.status).toBe('pending');
    expect(job.user_id).toBe('user-1');
    expect(redis.hset).toHaveBeenCalled();
  });

  it('gets a job by id', async () => {
    const created = await repo.create({
      user_id: 'user-1',
      config: {
        agora: { token: 'tok', channel: 'ch' },
        preferences: { soundscape: 'rain', volume: 0.7 },
      },
    });

    const fetched = await repo.get(created.id);
    expect(fetched).not.toBeNull();
    expect(fetched!.id).toBe(created.id);
    expect(fetched!.status).toBe('pending');
  });

  it('returns null for non-existent job', async () => {
    const result = await repo.get('non-existent-id');
    expect(result).toBeNull();
  });

  it('updates job status', async () => {
    const job = await repo.create({
      user_id: 'user-1',
      config: {
        agora: { token: 'tok', channel: 'ch' },
        preferences: { soundscape: 'rain', volume: 0.7 },
      },
    });

    await repo.updateStatus(job.id, 'running');
    const updated = await repo.get(job.id);
    expect(updated!.status).toBe('running');
  });

  it('sets 24h TTL on terminal status', async () => {
    const job = await repo.create({
      user_id: 'user-1',
      config: {
        agora: { token: 'tok', channel: 'ch' },
        preferences: { soundscape: 'rain', volume: 0.7 },
      },
    });

    await repo.updateStatus(job.id, 'stopped');
    expect(redis.expire).toHaveBeenCalledWith(`snora:job:${job.id}`, 86400);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/shared/job-repository.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement job repository**

Create `src/shared/job-repository.ts`:
```typescript
import { v4 as uuidv4 } from 'uuid';
import type Redis from 'ioredis';
import type { Job, JobStatus } from './types.js';

const TERMINAL_STATUSES: JobStatus[] = ['stopped', 'failed', 'client_disconnected'];
const JOB_TTL_SECONDS = 86400; // 24 hours

interface CreateJobInput {
  user_id: string;
  config: Job['config'];
}

export class JobRepository {
  constructor(private redis: Redis) {}

  async create(input: CreateJobInput): Promise<Job> {
    const id = uuidv4();
    const job: Job = {
      id,
      status: 'pending',
      user_id: input.user_id,
      config: input.config,
      created_at: Date.now(),
    };

    const key = `snora:job:${id}`;
    await this.redis.hset(key, 'id', id, 'status', 'pending', 'user_id', input.user_id,
      'config', JSON.stringify(input.config), 'created_at', String(job.created_at));

    return job;
  }

  async get(id: string): Promise<Job | null> {
    const key = `snora:job:${id}`;
    const data = await this.redis.hgetall(key);
    if (!data || !data.id) return null;

    return {
      id: data.id,
      status: data.status as JobStatus,
      user_id: data.user_id,
      config: JSON.parse(data.config),
      worker_pid: data.worker_pid ? parseInt(data.worker_pid, 10) : undefined,
      created_at: parseInt(data.created_at, 10),
      error: data.error || undefined,
      stop_reason: data.stop_reason || undefined,
      token_status: data.token_status || undefined,
    };
  }

  async updateStatus(id: string, status: JobStatus, extra?: Record<string, string>): Promise<void> {
    const key = `snora:job:${id}`;
    const fields = ['status', status];
    if (extra) {
      for (const [k, v] of Object.entries(extra)) {
        fields.push(k, v);
      }
    }
    await this.redis.hset(key, ...fields);

    if (TERMINAL_STATUSES.includes(status)) {
      await this.redis.expire(key, JOB_TTL_SECONDS);
      await this.redis.expire(`snora:job:${id}:state`, JOB_TTL_SECONDS);
    }
  }

  async updateState(id: string, state: Record<string, string>): Promise<void> {
    const key = `snora:job:${id}:state`;
    const fields: string[] = [];
    for (const [k, v] of Object.entries(state)) {
      fields.push(k, v);
    }
    await this.redis.hset(key, ...fields);
  }

  async publishStateUpdate(id: string, state: Record<string, unknown>): Promise<void> {
    const channel = `snora:channel:state:${id}`;
    await this.redis.publish(channel, JSON.stringify(state));
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/shared/job-repository.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/shared/job-repository.ts tests/unit/shared/job-repository.test.ts
git commit -m "feat: job repository with Redis CRUD and TTL on terminal states"
```

---

## Task 5: Validation Schemas

**Files:**
- Create: `src/shared/validation.ts`
- Test: `tests/unit/shared/validation.test.ts`

- [ ] **Step 1: Write failing validation tests**

Create `tests/unit/shared/validation.test.ts`:
```typescript
import { describe, it, expect } from 'vitest';
import Ajv from 'ajv';
import { createSessionSchema, stateUpdateSchema } from '../../../src/shared/validation.js';

const ajv = new Ajv();

describe('createSessionSchema', () => {
  const validate = ajv.compile(createSessionSchema);

  it('accepts valid request', () => {
    const valid = validate({
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(true);
  });

  it('rejects missing user_id', () => {
    const valid = validate({
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(false);
  });

  it('rejects invalid mood', () => {
    const valid = validate({
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'happy', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(false);
  });

  it('rejects heart_rate out of range', () => {
    const valid = validate({
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'calm', heart_rate: 300, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(false);
  });

  it('defaults binaural_beats to true', () => {
    const data = {
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'calm', heart_rate: 70, hrv: 50, respiration_rate: 14, stress_level: 0.3 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    };
    validate(data);
    expect((data as any).preferences.binaural_beats).toBe(true);
  });
});

describe('stateUpdateSchema', () => {
  const validate = ajv.compile(stateUpdateSchema);

  it('accepts valid state update', () => {
    const valid = validate({
      mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3,
    });
    expect(valid).toBe(true);
  });

  it('accepts optional timestamp', () => {
    const valid = validate({
      mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3,
      timestamp: 1710000000,
    });
    expect(valid).toBe(true);
  });

  it('rejects stress_level > 1.0', () => {
    const valid = validate({
      mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 1.5,
    });
    expect(valid).toBe(false);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npm install -D ajv
npx vitest run tests/unit/shared/validation.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement validation schemas**

Create `src/shared/validation.ts`:
```typescript
export const physiologicalStateProperties = {
  mood: { type: 'string', enum: ['anxious', 'stressed', 'neutral', 'calm', 'relaxed', 'sleepy'] },
  heart_rate: { type: 'integer', minimum: 30, maximum: 220 },
  hrv: { type: 'integer', minimum: 5, maximum: 300 },
  respiration_rate: { type: 'number', minimum: 4, maximum: 40 },
  stress_level: { type: 'number', minimum: 0, maximum: 1 },
} as const;

export const createSessionSchema = {
  type: 'object',
  required: ['user_id', 'agora', 'initial_state', 'preferences'],
  additionalProperties: false,
  properties: {
    user_id: { type: 'string', minLength: 1, maxLength: 128, pattern: '^[a-zA-Z0-9_-]+$' },
    agora: {
      type: 'object',
      required: ['token', 'channel'],
      additionalProperties: false,
      properties: {
        token: { type: 'string', minLength: 1 },
        channel: { type: 'string', minLength: 1 },
      },
    },
    initial_state: {
      type: 'object',
      required: ['mood', 'heart_rate', 'hrv', 'respiration_rate', 'stress_level'],
      additionalProperties: false,
      properties: {
        ...physiologicalStateProperties,
        timestamp: { type: 'integer' },
      },
    },
    preferences: {
      type: 'object',
      required: ['soundscape', 'volume'],
      additionalProperties: false,
      properties: {
        soundscape: { type: 'string', minLength: 1 },
        binaural_beats: { type: 'boolean', default: true },
        volume: { type: 'number', minimum: 0, maximum: 1 },
      },
    },
  },
} as const;

export const stateUpdateSchema = {
  type: 'object',
  required: ['mood', 'heart_rate', 'hrv', 'respiration_rate', 'stress_level'],
  additionalProperties: false,
  properties: {
    ...physiologicalStateProperties,
    timestamp: { type: 'integer' },
  },
} as const;

export const tokenUpdateSchema = {
  type: 'object',
  required: ['token'],
  additionalProperties: false,
  properties: {
    token: { type: 'string', minLength: 1 },
  },
} as const;
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/shared/validation.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/shared/validation.ts tests/unit/shared/validation.test.ts
git commit -m "feat: JSON validation schemas for session and state update requests"
```

---

## Task 6: API Auth Plugin

**Files:**
- Create: `src/api/plugins/auth.ts`
- Test: `tests/unit/api/auth.test.ts`

- [ ] **Step 1: Write failing auth tests**

Create `tests/unit/api/auth.test.ts`:
```typescript
import { describe, it, expect } from 'vitest';
import Fastify from 'fastify';
import { authPlugin } from '../../../src/api/plugins/auth.js';

async function buildApp(apiKey: string) {
  const app = Fastify();
  await app.register(authPlugin, { apiKey });
  app.get('/test', async () => ({ ok: true }));
  return app;
}

describe('authPlugin', () => {
  it('allows requests with valid API key', async () => {
    const app = await buildApp('test-key');
    const res = await app.inject({ method: 'GET', url: '/test', headers: { 'x-api-key': 'test-key' } });
    expect(res.statusCode).toBe(200);
  });

  it('rejects requests without API key', async () => {
    const app = await buildApp('test-key');
    const res = await app.inject({ method: 'GET', url: '/test' });
    expect(res.statusCode).toBe(401);
  });

  it('rejects requests with wrong API key', async () => {
    const app = await buildApp('test-key');
    const res = await app.inject({ method: 'GET', url: '/test', headers: { 'x-api-key': 'wrong' } });
    expect(res.statusCode).toBe(401);
  });

  it('skips auth for /health', async () => {
    const app = await buildApp('test-key');
    app.get('/health', async () => ({ ok: true }));
    const res = await app.inject({ method: 'GET', url: '/health' });
    expect(res.statusCode).toBe(200);
  });

  it('skips auth for /metrics', async () => {
    const app = await buildApp('test-key');
    app.get('/metrics', async () => 'metrics');
    const res = await app.inject({ method: 'GET', url: '/metrics' });
    expect(res.statusCode).toBe(200);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/api/auth.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement auth plugin**

Create `src/api/plugins/auth.ts`:
```typescript
import type { FastifyPluginAsync } from 'fastify';
import fp from 'fastify-plugin';

interface AuthPluginOptions {
  apiKey: string;
}

const SKIP_AUTH_PATHS = ['/health', '/metrics'];

const authPluginImpl: FastifyPluginAsync<AuthPluginOptions> = async (fastify, opts) => {
  fastify.addHook('onRequest', async (request, reply) => {
    if (SKIP_AUTH_PATHS.includes(request.url)) return;

    const key = request.headers['x-api-key'];
    if (!key || key !== opts.apiKey) {
      reply.code(401).send({ error: 'Unauthorized' });
    }
  });
};

export const authPlugin = fp(authPluginImpl, { name: 'auth' });
```

Install `fastify-plugin`:
```bash
npm install fastify-plugin
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/api/auth.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/api/plugins/auth.ts tests/unit/api/auth.test.ts
git commit -m "feat: API key authentication plugin with health/metrics bypass"
```

---

## Task 7: Session Routes

**Files:**
- Create: `src/api/routes/sessions.ts`
- Test: `tests/unit/api/sessions.test.ts`

- [ ] **Step 1: Write failing session route tests**

Create `tests/unit/api/sessions.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach } from 'vitest';
import Fastify from 'fastify';
import { sessionRoutes } from '../../../src/api/routes/sessions.js';
import type { JobRepository } from '../../../src/shared/job-repository.js';

function createMockRepo(): JobRepository {
  const jobs = new Map<string, any>();
  return {
    create: vi.fn(async (input) => {
      const job = { id: 'test-uuid', status: 'pending', ...input, created_at: Date.now() };
      jobs.set(job.id, job);
      return job;
    }),
    get: vi.fn(async (id) => jobs.get(id) ?? null),
    updateStatus: vi.fn(async (id, status, extra) => {
      const job = jobs.get(id);
      if (job) { job.status = status; Object.assign(job, extra); }
    }),
    updateState: vi.fn(async () => {}),
    publishStateUpdate: vi.fn(async () => {}),
  } as any;
}

async function buildApp(repo: JobRepository) {
  const app = Fastify();
  app.decorate('jobRepo', repo);
  app.decorate('throttle', new Map<string, number>());
  await app.register(sessionRoutes, { prefix: '/sessions' });
  return app;
}

const validBody = {
  user_id: 'user-1',
  agora: { token: 'tok', channel: 'ch' },
  initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
  preferences: { soundscape: 'rain', volume: 0.7 },
};

describe('POST /sessions', () => {
  let repo: JobRepository;

  beforeEach(() => { repo = createMockRepo(); });

  it('creates a session and returns 202', async () => {
    const app = await buildApp(repo);
    const res = await app.inject({ method: 'POST', url: '/sessions', payload: validBody });
    expect(res.statusCode).toBe(202);
    const body = JSON.parse(res.body);
    expect(body.job_id).toBe('test-uuid');
    expect(body.status).toBe('pending');
  });

  it('rejects invalid body with 400', async () => {
    const app = await buildApp(repo);
    const res = await app.inject({ method: 'POST', url: '/sessions', payload: { user_id: 'x' } });
    expect(res.statusCode).toBe(400);
  });
});

describe('GET /sessions/:id', () => {
  it('returns job by id', async () => {
    const repo = createMockRepo();
    const app = await buildApp(repo);
    await app.inject({ method: 'POST', url: '/sessions', payload: validBody });

    const res = await app.inject({ method: 'GET', url: '/sessions/test-uuid' });
    expect(res.statusCode).toBe(200);
    expect(JSON.parse(res.body).job_id).toBe('test-uuid');
  });

  it('returns 404 for non-existent job', async () => {
    const repo = createMockRepo();
    const app = await buildApp(repo);
    const res = await app.inject({ method: 'GET', url: '/sessions/nonexistent' });
    expect(res.statusCode).toBe(404);
  });

  it('rejects non-UUID id with 400', async () => {
    const repo = createMockRepo();
    const app = await buildApp(repo);
    const res = await app.inject({ method: 'GET', url: '/sessions/not-a-uuid!!!' });
    expect(res.statusCode).toBe(400);
  });
});

describe('PUT /sessions/:id/state', () => {
  it('accepts valid state update', async () => {
    const repo = createMockRepo();
    const app = await buildApp(repo);
    await app.inject({ method: 'POST', url: '/sessions', payload: validBody });

    const res = await app.inject({
      method: 'PUT', url: '/sessions/test-uuid/state',
      payload: { mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3 },
    });
    expect(res.statusCode).toBe(200);
    expect(repo.publishStateUpdate).toHaveBeenCalled();
  });
});

describe('DELETE /sessions/:id', () => {
  it('stops a running session', async () => {
    const repo = createMockRepo();
    const app = await buildApp(repo);
    await app.inject({ method: 'POST', url: '/sessions', payload: validBody });
    (repo as any).get = vi.fn(async () => ({ id: 'test-uuid', status: 'running' }));

    const res = await app.inject({ method: 'DELETE', url: '/sessions/test-uuid' });
    expect(res.statusCode).toBe(200);
    expect(repo.updateStatus).toHaveBeenCalledWith('test-uuid', 'stopping');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/api/sessions.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement session routes**

Create `src/api/routes/sessions.ts`:
```typescript
import type { FastifyPluginAsync } from 'fastify';
import { createSessionSchema, stateUpdateSchema, tokenUpdateSchema } from '../../shared/validation.js';
import type { JobRepository } from '../../shared/job-repository.js';

const UUID_REGEX = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function validateUuid(id: string): boolean {
  return UUID_REGEX.test(id);
}

export const sessionRoutes: FastifyPluginAsync = async (fastify) => {
  const repo = (fastify as any).jobRepo as JobRepository;
  const throttle = (fastify as any).throttle as Map<string, number>;

  // POST /sessions (stricter rate limit: 10/min per API key)
  fastify.post('/', {
    schema: { body: createSessionSchema },
    config: { rateLimit: { max: 10, timeWindow: '1 minute' } },
  }, async (request, reply) => {
    const body = request.body as any;
    const job = await repo.create({
      user_id: body.user_id,
      config: {
        agora: body.agora,
        preferences: body.preferences,
      },
    });

    // Store initial state
    await repo.updateState(job.id, {
      mood: body.initial_state.mood,
      heart_rate: String(body.initial_state.heart_rate),
      hrv: String(body.initial_state.hrv),
      respiration_rate: String(body.initial_state.respiration_rate),
      stress_level: String(body.initial_state.stress_level),
    });

    reply.code(202).send({ job_id: job.id, status: job.status });
  });

  // GET /sessions/:id
  fastify.get<{ Params: { id: string } }>('/:id', async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job) {
      reply.code(404).send({ error: 'Session not found' });
      return;
    }

    return {
      job_id: job.id,
      status: job.status,
      created_at: job.created_at,
      error: job.error,
      stop_reason: job.stop_reason,
      token_status: job.token_status,
    };
  });

  // PUT /sessions/:id/state
  fastify.put<{ Params: { id: string } }>('/:id/state', {
    schema: { body: stateUpdateSchema },
  }, async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job) {
      reply.code(404).send({ error: 'Session not found' });
      return;
    }

    // Throttle: 1 update per second per session
    const now = Date.now();
    const lastUpdate = throttle.get(id) ?? 0;
    if (now - lastUpdate < 1000) {
      // Silently accept but don't forward (latest wins on next valid update)
      return { ok: true };
    }
    throttle.set(id, now);

    const body = request.body as any;
    const state = {
      mood: body.mood,
      heart_rate: String(body.heart_rate),
      hrv: String(body.hrv),
      respiration_rate: String(body.respiration_rate),
      stress_level: String(body.stress_level),
    };

    await repo.updateState(id, state);
    await repo.publishStateUpdate(id, body);

    return { ok: true };
  });

  // PUT /sessions/:id/token
  fastify.put<{ Params: { id: string } }>('/:id/token', {
    schema: { body: tokenUpdateSchema },
  }, async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job || job.status !== 'running') {
      reply.code(404).send({ error: 'Running session not found' });
      return;
    }

    const body = request.body as any;
    await repo.publishStateUpdate(id, { type: 'token_update', token: body.token });
    await repo.updateStatus(id, job.status, { token_status: '' });

    return { ok: true };
  });

  // DELETE /sessions/:id
  fastify.delete<{ Params: { id: string } }>('/:id', async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job) {
      reply.code(404).send({ error: 'Session not found' });
      return;
    }

    await repo.updateStatus(id, 'stopping');
    await repo.publishStateUpdate(id, { type: 'shutdown' });

    return { status: 'stopping' };
  });
};
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/api/sessions.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/api/routes/sessions.ts tests/unit/api/sessions.test.ts
git commit -m "feat: session CRUD routes with validation, throttling, UUID checks"
```

---

## Task 8: Health & Metrics Routes

**Files:**
- Create: `src/api/routes/health.ts`
- Create: `src/api/routes/metrics.ts`
- Test: `tests/unit/api/health.test.ts`

- [ ] **Step 1: Write failing health test**

Create `tests/unit/api/health.test.ts`:
```typescript
import { describe, it, expect, vi } from 'vitest';
import Fastify from 'fastify';
import { healthRoutes } from '../../../src/api/routes/health.js';

describe('GET /health', () => {
  it('returns health status', async () => {
    const app = Fastify();
    const mockRedis = { ping: vi.fn(async () => 'PONG') };
    app.decorate('redis', mockRedis);
    app.decorate('config', { maxConcurrentSessions: 4 });
    app.decorate('activeSessionCount', () => 2);
    await app.register(healthRoutes);

    const res = await app.inject({ method: 'GET', url: '/health' });
    expect(res.statusCode).toBe(200);
    const body = JSON.parse(res.body);
    expect(body.ok).toBe(true);
    expect(body.redis).toBe('connected');
    expect(body.active_sessions).toBe(2);
    expect(body.max_sessions).toBe(4);
  });

  it('reports redis disconnected on error', async () => {
    const app = Fastify();
    const mockRedis = { ping: vi.fn(async () => { throw new Error('disconnected'); }) };
    app.decorate('redis', mockRedis);
    app.decorate('config', { maxConcurrentSessions: 4 });
    app.decorate('activeSessionCount', () => 0);
    await app.register(healthRoutes);

    const res = await app.inject({ method: 'GET', url: '/health' });
    expect(res.statusCode).toBe(200);
    const body = JSON.parse(res.body);
    expect(body.ok).toBe(false);
    expect(body.redis).toBe('disconnected');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/api/health.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement health and metrics routes**

Create `src/api/routes/health.ts`:
```typescript
import type { FastifyPluginAsync } from 'fastify';

export const healthRoutes: FastifyPluginAsync = async (fastify) => {
  fastify.get('/health', async () => {
    const redis = (fastify as any).redis;
    const config = (fastify as any).config;
    const activeSessionCount = (fastify as any).activeSessionCount;

    let redisStatus = 'connected';
    let ok = true;
    try {
      await redis.ping();
    } catch {
      redisStatus = 'disconnected';
      ok = false;
    }

    return {
      ok,
      redis: redisStatus,
      gpu: { available: true, memory_free_mb: null }, // Real values from NVML in CUDA engine plan
      active_sessions: activeSessionCount(),
      max_sessions: config.maxConcurrentSessions,
    };
  });
};
```

Create `src/api/routes/metrics.ts`:
```typescript
import type { FastifyPluginAsync } from 'fastify';
import { Registry, Counter, Gauge, Histogram, collectDefaultMetrics } from 'prom-client';

export const registry = new Registry();

export const sessionsActive = new Gauge({ name: 'snora_sessions_active', help: 'Currently running sessions', registers: [registry] });
export const sessionsTotal = new Counter({ name: 'snora_sessions_total', help: 'Total sessions', labelNames: ['status'], registers: [registry] });
export const engineSpawnDuration = new Histogram({ name: 'snora_engine_spawn_duration_seconds', help: 'Engine spawn time', registers: [registry] });
export const stateUpdatesTotal = new Counter({ name: 'snora_state_updates_total', help: 'State updates', labelNames: ['result'], registers: [registry] });
export const engineCrashesTotal = new Counter({ name: 'snora_engine_crashes_total', help: 'Engine crashes', registers: [registry] });

collectDefaultMetrics({ register: registry });

export const metricsRoutes: FastifyPluginAsync = async (fastify) => {
  fastify.get('/metrics', async (_request, reply) => {
    const metrics = await registry.metrics();
    reply.type(registry.contentType).send(metrics);
  });
};
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/api/health.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/api/routes/health.ts src/api/routes/metrics.ts tests/unit/api/health.test.ts
git commit -m "feat: health check and Prometheus metrics endpoints"
```

---

## Task 9: API Server Factory

**Files:**
- Create: `src/api/server.ts`
- Create: `src/api/main.ts`

- [ ] **Step 1: Create Fastify app factory**

Create `src/api/server.ts`:
```typescript
import Fastify from 'fastify';
import type { Config } from '../shared/config.js';
import type { Logger } from 'pino';
import type Redis from 'ioredis';
import type { JobRepository } from '../shared/job-repository.js';
import rateLimit from '@fastify/rate-limit';
import { authPlugin } from './plugins/auth.js';
import { sessionRoutes } from './routes/sessions.js';
import { healthRoutes } from './routes/health.js';
import { metricsRoutes } from './routes/metrics.js';

interface BuildServerOptions {
  config: Config;
  logger: Logger;
  redis: Redis;
  jobRepo: JobRepository;
  activeSessionCount: () => number;
}

export async function buildServer(opts: BuildServerOptions) {
  const app = Fastify({
    logger: opts.logger,
    bodyLimit: 4096, // 4KB max payload
  });

  // Decorators available to routes
  app.decorate('config', opts.config);
  app.decorate('redis', opts.redis);
  app.decorate('jobRepo', opts.jobRepo);
  app.decorate('throttle', new Map<string, number>());
  app.decorate('activeSessionCount', opts.activeSessionCount);

  // Plugins
  await app.register(rateLimit, {
    max: 100,
    timeWindow: '1 second',
    keyGenerator: (request) => request.headers['x-api-key'] as string ?? request.ip,
  });
  await app.register(authPlugin, { apiKey: opts.config.apiKey });

  // Routes (POST /sessions gets a stricter rate limit via route config)
  await app.register(sessionRoutes, { prefix: '/sessions' });
  await app.register(healthRoutes);
  await app.register(metricsRoutes);

  return app;
}
```

- [ ] **Step 2: Create API entry point**

Create `src/api/main.ts`:
```typescript
import { loadConfig } from '../shared/config.js';
import { createLogger } from '../shared/logger.js';
import { createRedisClient } from '../shared/redis.js';
import { JobRepository } from '../shared/job-repository.js';
import { buildServer } from './server.js';

async function main() {
  const config = loadConfig();
  const logger = createLogger('api', config.logLevel);
  const redis = createRedisClient(config.redisUrl, logger);
  const jobRepo = new JobRepository(redis);

  // Active session count will be populated by scanning Redis
  // For now, this is a simple counter maintained by the worker
  let activeCount = 0;

  const server = await buildServer({
    config, logger, redis, jobRepo,
    activeSessionCount: () => activeCount,
  });

  try {
    await server.listen({ port: config.port, host: '0.0.0.0' });
    logger.info({ port: config.port }, 'API server started');
  } catch (err) {
    logger.error({ err }, 'Failed to start server');
    process.exit(1);
  }

  // Graceful shutdown
  for (const signal of ['SIGTERM', 'SIGINT']) {
    process.on(signal, async () => {
      logger.info({ signal }, 'Shutting down API server');
      await server.close();
      redis.disconnect();
      process.exit(0);
    });
  }
}

main();
```

- [ ] **Step 3: Commit**

```bash
git add src/api/server.ts src/api/main.ts
git commit -m "feat: Fastify server factory and API entry point"
```

---

## Task 10: Engine Bridge (Unix Socket IPC Client)

**Files:**
- Create: `src/worker/engine-bridge.ts`
- Test: `tests/unit/worker/engine-bridge.test.ts`

- [ ] **Step 1: Write failing engine bridge tests**

Create `tests/unit/worker/engine-bridge.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomUUID } from 'node:crypto';
import { EngineBridge } from '../../../src/worker/engine-bridge.js';
import { encodeMessage } from '../../../src/shared/ipc-protocol.js';
import type { IpcMessage } from '../../../src/shared/types.js';

function createMockEngine(socketPath: string): Promise<ReturnType<typeof createServer>> {
  return new Promise((resolve) => {
    const server = createServer((conn) => {
      // Auto-ack every message
      const { MessageDecoder } = require('../../../src/shared/ipc-protocol.js');
      const decoder = new MessageDecoder();
      conn.on('data', (data: Buffer) => decoder.feed(data));
      decoder.on('message', () => {
        conn.write(encodeMessage({ type: 'ack' }));
      });
    });
    server.listen(socketPath, () => resolve(server));
  });
}

describe('EngineBridge', () => {
  let socketPath: string;
  let mockServer: ReturnType<typeof createServer>;

  beforeEach(async () => {
    socketPath = join(tmpdir(), `snora-test-${randomUUID()}.sock`);
    mockServer = await createMockEngine(socketPath);
  });

  afterEach(() => {
    mockServer.close();
  });

  it('connects to engine socket and sends init message', async () => {
    const bridge = new EngineBridge(socketPath);
    await bridge.connect();

    const acked = await bridge.send({ type: 'init', data: { app_id: 'test' } });
    expect(acked).toBe(true);

    bridge.close();
  });

  it('sends state_update and receives ack', async () => {
    const bridge = new EngineBridge(socketPath);
    await bridge.connect();

    const acked = await bridge.send({
      type: 'state_update',
      data: { heart_rate: 68, mood: 'calm', stress_level: 0.3 },
    });
    expect(acked).toBe(true);

    bridge.close();
  });

  it('times out if no ack received within timeout', async () => {
    // Create a server that never responds
    mockServer.close();
    const silentPath = join(tmpdir(), `snora-silent-${randomUUID()}.sock`);
    const silentServer = createServer(() => {}); // no response
    await new Promise<void>((r) => silentServer.listen(silentPath, r));

    const bridge = new EngineBridge(silentPath, 500); // 500ms timeout for test
    await bridge.connect();

    const acked = await bridge.send({ type: 'init', data: {} });
    expect(acked).toBe(false);

    bridge.close();
    silentServer.close();
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/worker/engine-bridge.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement engine bridge**

Create `src/worker/engine-bridge.ts`:
```typescript
import { connect, type Socket } from 'node:net';
import { EventEmitter } from 'node:events';
import { encodeMessage, MessageDecoder } from '../shared/ipc-protocol.js';
import type { IpcMessage } from '../shared/types.js';

const DEFAULT_ACK_TIMEOUT = 5000;

export class EngineBridge extends EventEmitter {
  private socket: Socket | null = null;
  private decoder = new MessageDecoder();
  private pendingAck: { resolve: (acked: boolean) => void } | null = null;

  constructor(
    private socketPath: string,
    private ackTimeout = DEFAULT_ACK_TIMEOUT,
  ) {
    super();
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket = connect(this.socketPath, () => resolve());
      this.socket.on('error', (err) => {
        this.emit('error', err);
        reject(err);
      });
      this.socket.on('close', () => this.emit('close'));
      this.socket.on('data', (data) => this.decoder.feed(data));

      this.decoder.on('message', (msg: IpcMessage) => {
        if (msg.type === 'ack' && this.pendingAck) {
          this.pendingAck.resolve(true);
          this.pendingAck = null;
        } else if (msg.type === 'status') {
          this.emit('status', msg.data);
        }
      });

      this.decoder.on('error', (err) => this.emit('error', err));
    });
  }

  send(msg: IpcMessage): Promise<boolean> {
    return new Promise((resolve) => {
      if (!this.socket) {
        resolve(false);
        return;
      }

      const timer = setTimeout(() => {
        this.pendingAck = null;
        resolve(false);
      }, this.ackTimeout);

      this.pendingAck = {
        resolve: (acked) => {
          clearTimeout(timer);
          resolve(acked);
        },
      };

      this.socket.write(encodeMessage(msg));
    });
  }

  close(): void {
    if (this.pendingAck) {
      this.pendingAck.resolve(false);
      this.pendingAck = null;
    }
    this.socket?.destroy();
    this.socket = null;
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/worker/engine-bridge.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/worker/engine-bridge.ts tests/unit/worker/engine-bridge.test.ts
git commit -m "feat: engine bridge for Unix socket IPC with ack timeout"
```

---

## Task 11: Heartbeat Module

**Files:**
- Create: `src/worker/heartbeat.ts`
- Test: `tests/unit/worker/heartbeat.test.ts`

- [ ] **Step 1: Write failing heartbeat tests**

Create `tests/unit/worker/heartbeat.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { Heartbeat } from '../../../src/worker/heartbeat.js';

describe('Heartbeat', () => {
  let mockRedis: any;
  let heartbeat: Heartbeat;

  beforeEach(() => {
    vi.useFakeTimers();
    mockRedis = {
      set: vi.fn(async () => 'OK'),
      del: vi.fn(async () => 1),
    };
  });

  afterEach(() => {
    vi.useRealTimers();
    heartbeat?.stop();
  });

  it('sets heartbeat key with TTL on start', async () => {
    heartbeat = new Heartbeat(mockRedis, 1234);
    await heartbeat.start();

    expect(mockRedis.set).toHaveBeenCalledWith(
      'snora:worker:1234', '1', 'EX', 10
    );
  });

  it('refreshes heartbeat every 5 seconds', async () => {
    heartbeat = new Heartbeat(mockRedis, 1234);
    await heartbeat.start();

    mockRedis.set.mockClear();
    vi.advanceTimersByTime(5000);
    // Need to flush promises
    await vi.runOnlyPendingTimersAsync();

    expect(mockRedis.set).toHaveBeenCalledTimes(1);
  });

  it('deletes heartbeat key on stop', async () => {
    heartbeat = new Heartbeat(mockRedis, 1234);
    await heartbeat.start();
    await heartbeat.stop();

    expect(mockRedis.del).toHaveBeenCalledWith('snora:worker:1234');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/worker/heartbeat.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement heartbeat**

Create `src/worker/heartbeat.ts`:
```typescript
import type Redis from 'ioredis';

const HEARTBEAT_INTERVAL = 5000; // 5 seconds
const HEARTBEAT_TTL = 10; // 10 seconds

export class Heartbeat {
  private interval: ReturnType<typeof setInterval> | null = null;

  constructor(
    private redis: Redis,
    private pid: number,
  ) {}

  private get key(): string {
    return `snora:worker:${this.pid}`;
  }

  async start(): Promise<void> {
    await this.beat();
    this.interval = setInterval(() => this.beat(), HEARTBEAT_INTERVAL);
  }

  async stop(): Promise<void> {
    if (this.interval) {
      clearInterval(this.interval);
      this.interval = null;
    }
    await this.redis.del(this.key);
  }

  private async beat(): Promise<void> {
    await this.redis.set(this.key, '1', 'EX', HEARTBEAT_TTL);
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/worker/heartbeat.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/worker/heartbeat.ts tests/unit/worker/heartbeat.test.ts
git commit -m "feat: worker heartbeat with 10s TTL refreshed every 5s"
```

---

## Task 12: Session Monitor (Timeouts)

**Files:**
- Create: `src/worker/session-monitor.ts`
- Test: `tests/unit/worker/session-monitor.test.ts`

- [ ] **Step 1: Write failing session monitor tests**

Create `tests/unit/worker/session-monitor.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { SessionMonitor } from '../../../src/worker/session-monitor.js';

describe('SessionMonitor', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('emits idle_timeout when no state updates received', () => {
    const onTimeout = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 1000,
      maxDurationMs: 60000,
      disconnectGraceMs: 500,
    });
    monitor.on('idle_timeout', onTimeout);
    monitor.start();

    vi.advanceTimersByTime(1000);
    expect(onTimeout).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('resets idle timer on state update', () => {
    const onTimeout = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 1000,
      maxDurationMs: 60000,
      disconnectGraceMs: 500,
    });
    monitor.on('idle_timeout', onTimeout);
    monitor.start();

    vi.advanceTimersByTime(800);
    monitor.recordStateUpdate();
    vi.advanceTimersByTime(800);
    expect(onTimeout).not.toHaveBeenCalled();

    vi.advanceTimersByTime(200);
    expect(onTimeout).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('emits max_duration when session exceeds limit', () => {
    const onMaxDuration = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 60000,
      maxDurationMs: 2000,
      disconnectGraceMs: 500,
    });
    monitor.on('max_duration', onMaxDuration);
    monitor.start();

    vi.advanceTimersByTime(2000);
    expect(onMaxDuration).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('handles disconnect grace period', () => {
    const onDisconnect = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 60000,
      maxDurationMs: 60000,
      disconnectGraceMs: 1000,
    });
    monitor.on('client_disconnected', onDisconnect);
    monitor.start();

    monitor.startDisconnectGrace();
    vi.advanceTimersByTime(999);
    expect(onDisconnect).not.toHaveBeenCalled();

    vi.advanceTimersByTime(1);
    expect(onDisconnect).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('cancels disconnect grace on reconnect', () => {
    const onDisconnect = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 60000,
      maxDurationMs: 60000,
      disconnectGraceMs: 1000,
    });
    monitor.on('client_disconnected', onDisconnect);
    monitor.start();

    monitor.startDisconnectGrace();
    vi.advanceTimersByTime(500);
    monitor.cancelDisconnectGrace();
    vi.advanceTimersByTime(1000);
    expect(onDisconnect).not.toHaveBeenCalled();
    monitor.stop();
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/worker/session-monitor.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement session monitor**

Create `src/worker/session-monitor.ts`:
```typescript
import { EventEmitter } from 'node:events';

interface SessionMonitorConfig {
  idleTimeoutMs: number;
  maxDurationMs: number;
  disconnectGraceMs: number;
}

export class SessionMonitor extends EventEmitter {
  private idleTimer: ReturnType<typeof setTimeout> | null = null;
  private durationTimer: ReturnType<typeof setTimeout> | null = null;
  private graceTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(
    private jobId: string,
    private config: SessionMonitorConfig,
  ) {
    super();
  }

  start(): void {
    this.resetIdleTimer();
    this.durationTimer = setTimeout(() => {
      this.emit('max_duration', this.jobId);
    }, this.config.maxDurationMs);
  }

  stop(): void {
    if (this.idleTimer) { clearTimeout(this.idleTimer); this.idleTimer = null; }
    if (this.durationTimer) { clearTimeout(this.durationTimer); this.durationTimer = null; }
    if (this.graceTimer) { clearTimeout(this.graceTimer); this.graceTimer = null; }
  }

  recordStateUpdate(): void {
    this.resetIdleTimer();
  }

  startDisconnectGrace(): void {
    if (this.graceTimer) return;
    this.graceTimer = setTimeout(() => {
      this.graceTimer = null;
      this.emit('client_disconnected', this.jobId);
    }, this.config.disconnectGraceMs);
  }

  cancelDisconnectGrace(): void {
    if (this.graceTimer) {
      clearTimeout(this.graceTimer);
      this.graceTimer = null;
    }
  }

  private resetIdleTimer(): void {
    if (this.idleTimer) clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => {
      this.emit('idle_timeout', this.jobId);
    }, this.config.idleTimeoutMs);
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
npx vitest run tests/unit/worker/session-monitor.test.ts
```
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/worker/session-monitor.ts tests/unit/worker/session-monitor.test.ts
git commit -m "feat: session monitor with idle, max duration, and disconnect grace timers"
```

---

## Task 13: Worker Manager

**Files:**
- Create: `src/worker/manager.ts`
- Create: `src/worker/main.ts`
- Test: `tests/unit/worker/manager.test.ts`

- [ ] **Step 1: Write failing worker manager tests**

Create `tests/unit/worker/manager.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { WorkerManager } from '../../../src/worker/manager.js';

function createMockDeps() {
  const jobs = new Map<string, any>();
  return {
    redis: {
      set: vi.fn(async () => 'OK'),
      del: vi.fn(async () => 1),
      subscribe: vi.fn(async () => {}),
      on: vi.fn(),
      duplicate: vi.fn(function(this: any) { return { ...this, subscribe: vi.fn(), on: vi.fn() }; }),
      keys: vi.fn(async () => []),
      hgetall: vi.fn(async () => ({})),
      ping: vi.fn(async () => 'PONG'),
    } as any,
    jobRepo: {
      get: vi.fn(async (id: string) => jobs.get(id) ?? null),
      create: vi.fn(),
      updateStatus: vi.fn(async (id: string, status: string) => {
        const job = jobs.get(id);
        if (job) job.status = status;
      }),
      updateState: vi.fn(),
      publishStateUpdate: vi.fn(),
    } as any,
    config: {
      maxConcurrentSessions: 2,
      agoraAppId: 'test-app',
      assetsPath: '/assets/sounds',
      maxSessionDurationHours: 12,
      idleTimeoutMinutes: 30,
      clientDisconnectGraceMinutes: 5,
      logLevel: 'silent',
    } as any,
    logger: {
      info: vi.fn(),
      warn: vi.fn(),
      error: vi.fn(),
      debug: vi.fn(),
      child: vi.fn(function(this: any) { return this; }),
    } as any,
    _jobs: jobs,
  };
}

describe('WorkerManager', () => {
  it('enforces max concurrent session limit', async () => {
    const deps = createMockDeps();
    const manager = new WorkerManager(deps.redis, deps.jobRepo, deps.config, deps.logger);

    expect(manager.canAcceptSession()).toBe(true);
    // Simulate 2 active sessions
    (manager as any).activeSessions.set('s1', {});
    (manager as any).activeSessions.set('s2', {});
    expect(manager.canAcceptSession()).toBe(false);
  });

  it('tracks active session count', () => {
    const deps = createMockDeps();
    const manager = new WorkerManager(deps.redis, deps.jobRepo, deps.config, deps.logger);

    expect(manager.activeSessionCount()).toBe(0);
    (manager as any).activeSessions.set('s1', {});
    expect(manager.activeSessionCount()).toBe(1);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

```bash
npx vitest run tests/unit/worker/manager.test.ts
```
Expected: FAIL

- [ ] **Step 3: Implement worker manager**

Create `src/worker/manager.ts`:
```typescript
import { spawn, type ChildProcess } from 'node:child_process';
import type Redis from 'ioredis';
import type { Logger } from 'pino';
import type { Config } from '../shared/config.js';
import type { JobRepository } from '../shared/job-repository.js';
import { EngineBridge } from './engine-bridge.js';
import { Heartbeat } from './heartbeat.js';
import { SessionMonitor } from './session-monitor.js';

interface ActiveSession {
  jobId: string;
  process: ChildProcess;
  bridge: EngineBridge;
  monitor: SessionMonitor;
  subscriber: Redis;
}

export class WorkerManager {
  private activeSessions = new Map<string, ActiveSession>();
  private heartbeat: Heartbeat;
  private pollInterval: ReturnType<typeof setInterval> | null = null;
  private stopping = false;

  constructor(
    private redis: Redis,
    private jobRepo: JobRepository,
    private config: Config,
    private logger: Logger,
  ) {
    this.heartbeat = new Heartbeat(redis, process.pid);
  }

  canAcceptSession(): boolean {
    return this.activeSessions.size < this.config.maxConcurrentSessions;
  }

  activeSessionCount(): number {
    return this.activeSessions.size;
  }

  async start(): Promise<void> {
    this.logger.info('Worker Manager starting');

    // Recover orphaned jobs
    await this.recoverOrphanedJobs();

    // Start heartbeat
    await this.heartbeat.start();

    // Poll for pending jobs every second
    this.pollInterval = setInterval(() => this.pollForJobs(), 1000);
    this.logger.info('Worker Manager started');
  }

  async stop(): Promise<void> {
    this.stopping = true;
    this.logger.info('Worker Manager stopping');

    // Stop polling
    if (this.pollInterval) {
      clearInterval(this.pollInterval);
      this.pollInterval = null;
    }

    // Shutdown all active sessions
    const shutdownPromises = Array.from(this.activeSessions.entries()).map(
      async ([jobId, session]) => {
        this.logger.info({ jobId }, 'Sending shutdown to engine');
        await session.bridge.send({ type: 'shutdown' });
        session.monitor.stop();
      }
    );
    await Promise.all(shutdownPromises);

    // Wait up to 30s for processes to exit
    await new Promise<void>((resolve) => {
      const timeout = setTimeout(resolve, 30000);
      const check = setInterval(() => {
        if (this.activeSessions.size === 0) {
          clearInterval(check);
          clearTimeout(timeout);
          resolve();
        }
      }, 500);
    });

    // Mark remaining as stopped
    for (const [jobId] of this.activeSessions) {
      await this.jobRepo.updateStatus(jobId, 'stopped');
    }
    this.activeSessions.clear();

    await this.heartbeat.stop();
    this.logger.info('Worker Manager stopped');
  }

  private async recoverOrphanedJobs(): Promise<void> {
    // Scan for jobs in running/starting state with no valid heartbeat
    const keys = await this.redis.keys('snora:job:*');
    for (const key of keys) {
      if (key.includes(':state')) continue;
      const data = await this.redis.hgetall(key);
      if (!data.status || !['running', 'starting'].includes(data.status)) continue;

      if (data.worker_pid) {
        const heartbeatKey = `snora:worker:${data.worker_pid}`;
        const alive = await this.redis.exists(heartbeatKey);
        if (!alive) {
          this.logger.warn({ jobId: data.id, workerPid: data.worker_pid }, 'Recovering orphaned job');
          await this.jobRepo.updateStatus(data.id, 'failed', { error: 'worker_crashed' });
        }
      }
    }
  }

  private async pollForJobs(): Promise<void> {
    if (this.stopping || !this.canAcceptSession()) return;

    // Find pending jobs (simple scan — in production, use a Redis list or sorted set)
    const keys = await this.redis.keys('snora:job:*');
    for (const key of keys) {
      if (key.includes(':state') || !this.canAcceptSession()) continue;
      const data = await this.redis.hgetall(key);
      if (data.status === 'pending') {
        await this.startSession(data.id);
        break; // One at a time
      }
    }
  }

  private async startSession(jobId: string): Promise<void> {
    this.logger.info({ jobId }, 'Starting session');
    await this.jobRepo.updateStatus(jobId, 'starting', { worker_pid: String(process.pid) });

    const job = await this.jobRepo.get(jobId);
    if (!job) return;

    const socketPath = `/tmp/snora-engine-${jobId}.sock`;

    // Spawn engine process
    const engineProcess = spawn('snora-engine', ['--socket', socketPath, '--gpu', String(this.config.gpuDeviceId)], {
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    // Capture engine stderr (JSON logs)
    engineProcess.stderr?.on('data', (data: Buffer) => {
      const line = data.toString().trim();
      if (line) {
        this.logger.info({ jobId, source: 'engine' }, line);
      }
    });

    // Wait for socket to be available, then connect
    const bridge = new EngineBridge(socketPath);
    try {
      await new Promise((resolve) => setTimeout(resolve, 500)); // Wait for engine to start
      await bridge.connect();
    } catch (err) {
      this.logger.error({ jobId, err }, 'Failed to connect to engine');
      await this.jobRepo.updateStatus(jobId, 'failed', { error: 'engine_connect_failed' });
      engineProcess.kill();
      return;
    }

    // Send init message
    const initAcked = await bridge.send({
      type: 'init',
      data: {
        app_id: this.config.agoraAppId,
        token: job.config.agora.token,
        channel: job.config.agora.channel,
        preferences: job.config.preferences,
        assets_path: this.config.assetsPath,
      },
    });

    if (!initAcked) {
      this.logger.error({ jobId }, 'Engine did not ack init');
      await this.jobRepo.updateStatus(jobId, 'failed', { error: 'engine_init_timeout' });
      engineProcess.kill();
      bridge.close();
      return;
    }

    await this.jobRepo.updateStatus(jobId, 'running');

    // Set up session monitor
    const monitor = new SessionMonitor(jobId, {
      idleTimeoutMs: this.config.idleTimeoutMinutes * 60 * 1000,
      maxDurationMs: this.config.maxSessionDurationHours * 60 * 60 * 1000,
      disconnectGraceMs: this.config.clientDisconnectGraceMinutes * 60 * 1000,
    });

    monitor.on('idle_timeout', () => this.stopSession(jobId, 'stopped', 'idle_timeout'));
    monitor.on('max_duration', () => this.stopSession(jobId, 'stopped', 'max_duration'));
    monitor.on('client_disconnected', () => this.stopSession(jobId, 'client_disconnected', 'no_subscribers'));
    monitor.start();

    // Subscribe to state updates via Redis pub/sub
    const subscriber = this.redis.duplicate();
    const channel = `snora:channel:state:${jobId}`;
    await subscriber.subscribe(channel);
    subscriber.on('message', async (_ch: string, message: string) => {
      try {
        const data = JSON.parse(message);
        if (data.type === 'shutdown') {
          await this.stopSession(jobId, 'stopping');
        } else if (data.type === 'token_update') {
          // Cancel token expiry timer if running
          if (tokenExpiryTimer) { clearTimeout(tokenExpiryTimer); tokenExpiryTimer = null; }
          await bridge.send({ type: 'token_update', data: { token: data.token } });
        } else {
          await bridge.send({ type: 'state_update', data });
          monitor.recordStateUpdate();
        }
      } catch (err) {
        this.logger.error({ jobId, err }, 'Failed to forward state update');
      }
    });

    // Handle engine status messages
    let tokenExpiryTimer: ReturnType<typeof setTimeout> | null = null;
    bridge.on('status', (data: Record<string, unknown>) => {
      const reason = data.reason as string;
      if (reason === 'no_subscribers') {
        monitor.startDisconnectGrace();
      } else if (reason === 'subscriber_joined') {
        monitor.cancelDisconnectGrace();
      } else if (reason === 'token_expiring') {
        this.jobRepo.updateStatus(jobId, 'running', { token_status: 'expiring' });
        // Start 60-second countdown — if no token_update received, fail the job
        tokenExpiryTimer = setTimeout(() => {
          this.stopSession(jobId, 'failed', 'token_expired');
        }, 60000);
      } else if (reason === 'agora_disconnected') {
        this.stopSession(jobId, 'failed', 'agora_connection_lost');
      }
    });

    // Handle engine process exit
    engineProcess.on('exit', (code) => {
      this.logger.warn({ jobId, code }, 'Engine process exited');
      const session = this.activeSessions.get(jobId);
      if (session) {
        session.monitor.stop();
        session.bridge.close();
        subscriber.unsubscribe();
        subscriber.disconnect();
        this.activeSessions.delete(jobId);
        if (code !== 0) {
          this.jobRepo.updateStatus(jobId, 'failed', { error: `engine_exit_${code}` });
        }
      }
    });

    this.activeSessions.set(jobId, {
      jobId,
      process: engineProcess,
      bridge,
      monitor,
      subscriber,
    });

    this.logger.info({ jobId }, 'Session started');
  }

  private async stopSession(jobId: string, status: 'stopping' | 'stopped' | 'failed' | 'client_disconnected', reason?: string): Promise<void> {
    const session = this.activeSessions.get(jobId);
    if (!session) return;

    this.logger.info({ jobId, status, reason }, 'Stopping session');
    session.monitor.stop();

    await session.bridge.send({ type: 'shutdown' });

    const extra: Record<string, string> = {};
    if (reason) extra.stop_reason = reason;
    if (status === 'failed' && reason) extra.error = reason;
    await this.jobRepo.updateStatus(jobId, status, extra);
  }
}
```

- [ ] **Step 4: Create worker entry point**

Create `src/worker/main.ts`:
```typescript
import { loadConfig } from '../shared/config.js';
import { createLogger } from '../shared/logger.js';
import { createRedisClient } from '../shared/redis.js';
import { JobRepository } from '../shared/job-repository.js';
import { WorkerManager } from './manager.js';

async function main() {
  const config = loadConfig();
  const logger = createLogger('worker', config.logLevel);
  const redis = createRedisClient(config.redisUrl, logger);
  const jobRepo = new JobRepository(redis);

  const manager = new WorkerManager(redis, jobRepo, config, logger);
  await manager.start();

  for (const signal of ['SIGTERM', 'SIGINT'] as const) {
    process.on(signal, async () => {
      logger.info({ signal }, 'Received shutdown signal');
      await manager.stop();
      redis.disconnect();
      process.exit(0);
    });
  }
}

main();
```

- [ ] **Step 5: Run test to verify it passes**

```bash
npx vitest run tests/unit/worker/manager.test.ts
```
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/worker/manager.ts src/worker/main.ts tests/unit/worker/manager.test.ts
git commit -m "feat: worker manager with job polling, engine spawning, and session lifecycle"
```

---

## Task 14: Mock Engine for Testing

**Files:**
- Create: `tests/fixtures/mock-engine.ts`

- [ ] **Step 1: Create mock engine process**

This is a simple Node.js script that mimics the C++ engine's IPC behavior. Used in integration tests.

Create `tests/fixtures/mock-engine.ts`:
```typescript
import { createServer } from 'node:net';
import { encodeMessage, MessageDecoder } from '../../src/shared/ipc-protocol.js';
import type { IpcMessage } from '../../src/shared/types.js';

const socketPath = process.argv.find((arg, i) => process.argv[i - 1] === '--socket') ?? '/tmp/snora-test.sock';

const server = createServer((conn) => {
  const decoder = new MessageDecoder();
  conn.on('data', (data) => decoder.feed(data));

  decoder.on('message', (msg: IpcMessage) => {
    // Always ack
    conn.write(encodeMessage({ type: 'ack' }));

    if (msg.type === 'init') {
      // Send running status
      conn.write(encodeMessage({ type: 'status', data: { reason: 'running' } }));
      process.stderr.write(JSON.stringify({ level: 'info', msg: 'Engine initialized' }) + '\n');
    } else if (msg.type === 'shutdown') {
      process.stderr.write(JSON.stringify({ level: 'info', msg: 'Engine shutting down' }) + '\n');
      setTimeout(() => {
        server.close();
        process.exit(0);
      }, 100);
    }
  });
});

server.listen(socketPath, () => {
  process.stderr.write(JSON.stringify({ level: 'info', msg: `Mock engine listening on ${socketPath}` }) + '\n');
});

process.on('SIGTERM', () => {
  server.close();
  process.exit(0);
});
```

- [ ] **Step 2: Commit**

```bash
git add tests/fixtures/mock-engine.ts
git commit -m "feat: mock engine process for integration testing"
```

---

## Task 15: Rate Limiting

**Files:**
- Create: `src/api/plugins/rate-limit.ts`
- Test: `tests/unit/api/rate-limit.test.ts`

- [ ] **Step 1: Write failing rate limit tests**

Create `tests/unit/api/rate-limit.test.ts`:
```typescript
import { describe, it, expect } from 'vitest';
import Fastify from 'fastify';
import rateLimit from '@fastify/rate-limit';

describe('rate limiting', () => {
  it('returns 429 when global rate limit exceeded', async () => {
    const app = Fastify();
    await app.register(rateLimit, { max: 2, timeWindow: '1 second' });
    app.get('/test', async () => ({ ok: true }));

    // First 2 requests succeed
    const res1 = await app.inject({ method: 'GET', url: '/test' });
    expect(res1.statusCode).toBe(200);
    const res2 = await app.inject({ method: 'GET', url: '/test' });
    expect(res2.statusCode).toBe(200);

    // Third request is rate limited
    const res3 = await app.inject({ method: 'GET', url: '/test' });
    expect(res3.statusCode).toBe(429);
    expect(res3.headers['retry-after']).toBeDefined();
  });
});
```

- [ ] **Step 2: Run test to verify it passes**

The `@fastify/rate-limit` plugin handles this natively. Verify:
```bash
npx vitest run tests/unit/api/rate-limit.test.ts
```
Expected: PASS (uses existing `@fastify/rate-limit` package)

- [ ] **Step 3: Commit**

```bash
git add tests/unit/api/rate-limit.test.ts
git commit -m "test: rate limiting verification with @fastify/rate-limit"
```

---

## Task 16: Expanded Worker Manager Tests

**Files:**
- Modify: `tests/unit/worker/manager.test.ts`

- [ ] **Step 1: Add orphaned job recovery test**

Add to `tests/unit/worker/manager.test.ts`:
```typescript
describe('recoverOrphanedJobs', () => {
  it('marks jobs as failed when worker heartbeat is missing', async () => {
    const deps = createMockDeps();
    // Simulate an orphaned job in Redis
    deps.redis.keys = vi.fn(async () => ['snora:job:orphan-1']);
    deps.redis.hgetall = vi.fn(async () => ({
      id: 'orphan-1', status: 'running', worker_pid: '9999',
      config: '{}', created_at: '1000', user_id: 'u1',
    }));
    deps.redis.exists = vi.fn(async () => 0); // heartbeat expired

    const manager = new WorkerManager(deps.redis, deps.jobRepo, deps.config, deps.logger);
    await (manager as any).recoverOrphanedJobs();

    expect(deps.jobRepo.updateStatus).toHaveBeenCalledWith(
      'orphan-1', 'failed', { error: 'worker_crashed' }
    );
  });

  it('skips jobs with valid heartbeat', async () => {
    const deps = createMockDeps();
    deps.redis.keys = vi.fn(async () => ['snora:job:alive-1']);
    deps.redis.hgetall = vi.fn(async () => ({
      id: 'alive-1', status: 'running', worker_pid: '1234',
      config: '{}', created_at: '1000', user_id: 'u1',
    }));
    deps.redis.exists = vi.fn(async () => 1); // heartbeat alive

    const manager = new WorkerManager(deps.redis, deps.jobRepo, deps.config, deps.logger);
    await (manager as any).recoverOrphanedJobs();

    expect(deps.jobRepo.updateStatus).not.toHaveBeenCalled();
  });
});

describe('graceful shutdown', () => {
  it('stops polling when stop is called', async () => {
    const deps = createMockDeps();
    const manager = new WorkerManager(deps.redis, deps.jobRepo, deps.config, deps.logger);
    expect((manager as any).stopping).toBe(false);
    await manager.stop();
    expect((manager as any).stopping).toBe(true);
  });
});
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
npx vitest run tests/unit/worker/manager.test.ts
```
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/unit/worker/manager.test.ts
git commit -m "test: expanded worker manager tests for orphan recovery and shutdown"
```

---

## Task 17: Integration Test (API + Redis)

**Files:**
- Create: `tests/integration/session-lifecycle.test.ts`

This test requires a running Redis instance. Use `REDIS_URL` env var or skip if unavailable.

- [ ] **Step 1: Write integration test**

Create `tests/integration/session-lifecycle.test.ts`:
```typescript
import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import Redis from 'ioredis';
import { buildServer } from '../../src/api/server.js';
import { JobRepository } from '../../src/shared/job-repository.js';
import { createLogger } from '../../src/shared/logger.js';
import type { Config } from '../../src/shared/config.js';

const REDIS_URL = process.env.REDIS_URL ?? 'redis://localhost:6379';

const testConfig: Config = {
  apiKey: 'test-api-key',
  redisUrl: REDIS_URL,
  agoraAppId: 'test-app',
  assetsPath: '/assets/sounds',
  maxConcurrentSessions: 4,
  gpuDeviceId: 0,
  port: 0,
  maxSessionDurationHours: 12,
  idleTimeoutMinutes: 30,
  clientDisconnectGraceMinutes: 5,
  logLevel: 'silent',
};

const validSession = {
  user_id: 'integration-test-user',
  agora: { token: 'test-token', channel: 'test-channel' },
  initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
  preferences: { soundscape: 'rain', volume: 0.7 },
};

describe('Session lifecycle (integration)', () => {
  let redis: Redis;
  let app: Awaited<ReturnType<typeof buildServer>>;
  let jobRepo: JobRepository;

  beforeAll(async () => {
    try {
      redis = new Redis(REDIS_URL);
      await redis.ping();
    } catch {
      console.warn('Redis not available, skipping integration tests');
      return;
    }

    const logger = createLogger('api', 'silent');
    jobRepo = new JobRepository(redis);
    app = await buildServer({
      config: testConfig, logger, redis, jobRepo,
      activeSessionCount: () => 0,
    });
  });

  afterAll(async () => {
    await redis?.quit();
  });

  beforeEach(async () => {
    // Clean test keys
    const keys = await redis.keys('snora:*');
    if (keys.length > 0) await redis.del(...keys);
  });

  it('creates a session, updates state, and stops it', async () => {
    // Create session
    const createRes = await app.inject({
      method: 'POST', url: '/sessions',
      headers: { 'x-api-key': 'test-api-key' },
      payload: validSession,
    });
    expect(createRes.statusCode).toBe(202);
    const { job_id } = JSON.parse(createRes.body);
    expect(job_id).toBeDefined();

    // Get session
    const getRes = await app.inject({
      method: 'GET', url: `/sessions/${job_id}`,
      headers: { 'x-api-key': 'test-api-key' },
    });
    expect(getRes.statusCode).toBe(200);
    expect(JSON.parse(getRes.body).status).toBe('pending');

    // Update state
    const stateRes = await app.inject({
      method: 'PUT', url: `/sessions/${job_id}/state`,
      headers: { 'x-api-key': 'test-api-key' },
      payload: { mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3 },
    });
    expect(stateRes.statusCode).toBe(200);

    // Verify state stored in Redis
    const state = await redis.hgetall(`snora:job:${job_id}:state`);
    expect(state.mood).toBe('calm');
    expect(state.heart_rate).toBe('68');

    // Stop session
    const stopRes = await app.inject({
      method: 'DELETE', url: `/sessions/${job_id}`,
      headers: { 'x-api-key': 'test-api-key' },
    });
    expect(stopRes.statusCode).toBe(200);

    // Verify status changed
    const afterStop = await redis.hgetall(`snora:job:${job_id}`);
    expect(afterStop.status).toBe('stopping');
  });

  it('returns 401 without API key', async () => {
    const res = await app.inject({ method: 'POST', url: '/sessions', payload: validSession });
    expect(res.statusCode).toBe(401);
  });

  it('returns 400 for invalid body', async () => {
    const res = await app.inject({
      method: 'POST', url: '/sessions',
      headers: { 'x-api-key': 'test-api-key' },
      payload: { user_id: 'x' },
    });
    expect(res.statusCode).toBe(400);
  });

  it('returns 404 for non-existent session', async () => {
    const res = await app.inject({
      method: 'GET', url: '/sessions/00000000-0000-0000-0000-000000000000',
      headers: { 'x-api-key': 'test-api-key' },
    });
    expect(res.statusCode).toBe(404);
  });
});
```

- [ ] **Step 2: Run integration test** (requires Redis)

```bash
REDIS_URL=redis://localhost:6379 npx vitest run tests/integration/
```
Expected: PASS if Redis is available, skip otherwise.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/session-lifecycle.test.ts
git commit -m "test: integration test for session lifecycle with real Redis"
```

---

## Task 18: Run Full Test Suite & Verify

- [ ] **Step 1: Run all tests**

```bash
npx vitest run
```
Expected: All tests PASS.

- [ ] **Step 2: Type check**

```bash
npx tsc --noEmit
```
Expected: No errors. Fix any type issues found.

- [ ] **Step 3: Final commit if fixes needed**

```bash
git add -A
git commit -m "fix: resolve type errors and test issues"
```

---

## Next Plans

This plan covers the Node.js services. Two additional plans are needed:

1. **`2026-03-22-snora-cuda-engine.md`** — C++ CUDA audio engine: IPC socket server, audio pipeline (noise gen, spectral tilt, binaural beats, nature loops, mixer), Agora SDK integration, CPU fallback mode, GoogleTest unit tests, reference audio tests.

2. **`2026-03-22-snora-infrastructure.md`** — Dockerfile (multi-stage build), `start.sh` entrypoint, Helm chart (`charts/snora/`), GitHub Actions workflows (ci.yml, release.yml, gpu-tests.yml), Docker Compose for integration tests.
