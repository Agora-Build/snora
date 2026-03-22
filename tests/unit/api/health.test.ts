import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import Fastify from 'fastify';
import Redis from 'ioredis';
import { healthRoutes } from '../../../src/api/routes/health.js';
import { metricsRoutes } from '../../../src/api/routes/metrics.js';

describe('GET /health', () => {
  let redis: Redis;

  beforeAll(async () => {
    redis = new Redis('redis://localhost:6379');
  });

  afterAll(async () => {
    await redis.quit();
  });

  it('returns health status with connected Redis', async () => {
    const app = Fastify();
    app.decorate('redis', redis);
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
    expect(body.gpu).toBeDefined();
    await app.close();
  });
});

describe('GET /metrics', () => {
  it('returns prometheus metrics', async () => {
    const app = Fastify();
    await app.register(metricsRoutes);

    const res = await app.inject({ method: 'GET', url: '/metrics' });
    expect(res.statusCode).toBe(200);
    expect(res.headers['content-type']).toContain('text/plain');
    expect(res.body).toContain('snora_sessions_active');
    await app.close();
  });
});
