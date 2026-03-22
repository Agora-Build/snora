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

  beforeAll(async () => {
    redis = new Redis(REDIS_URL);
    await redis.ping();

    const logger = createLogger('api', 'silent');
    const jobRepo = new JobRepository(redis);
    app = await buildServer({
      config: testConfig, logger, redis, jobRepo,
      activeSessionCount: () => 0,
    });
  });

  afterAll(async () => {
    await app?.close();
    await redis?.quit();
  });

  beforeEach(async () => {
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
