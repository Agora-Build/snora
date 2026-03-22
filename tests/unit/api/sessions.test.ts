import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import Fastify from 'fastify';
import Redis from 'ioredis';
import { sessionRoutes } from '../../../src/api/routes/sessions.js';
import { JobRepository } from '../../../src/shared/job-repository.js';

const validBody = {
  user_id: 'user-1',
  agora: { token: 'tok', channel: 'ch' },
  initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
  preferences: { soundscape: 'rain', volume: 0.7 },
};

describe('Session Routes', () => {
  let redis: Redis;
  let app: ReturnType<typeof Fastify>;

  beforeAll(async () => {
    redis = new Redis('redis://localhost:6379');
    const repo = new JobRepository(redis);

    app = Fastify();
    app.decorate('jobRepo', repo);
    app.decorate('throttle', new Map<string, number>());
    await app.register(sessionRoutes, { prefix: '/sessions' });
    await app.ready();
  });

  afterAll(async () => {
    await app.close();
    await redis.quit();
  });

  beforeEach(async () => {
    const keys = await redis.keys('snora:*');
    if (keys.length > 0) await redis.del(...keys);
  });

  it('POST /sessions creates a session and returns 202', async () => {
    const res = await app.inject({ method: 'POST', url: '/sessions', payload: validBody });
    expect(res.statusCode).toBe(202);
    const body = JSON.parse(res.body);
    expect(body.job_id).toMatch(/^[0-9a-f-]{36}$/);
    expect(body.status).toBe('pending');
  });

  it('POST /sessions rejects invalid body with 400', async () => {
    const res = await app.inject({ method: 'POST', url: '/sessions', payload: { user_id: 'x' } });
    expect(res.statusCode).toBe(400);
  });

  it('GET /sessions/:id returns job', async () => {
    const createRes = await app.inject({ method: 'POST', url: '/sessions', payload: validBody });
    const { job_id } = JSON.parse(createRes.body);

    const res = await app.inject({ method: 'GET', url: `/sessions/${job_id}` });
    expect(res.statusCode).toBe(200);
    expect(JSON.parse(res.body).job_id).toBe(job_id);
  });

  it('GET /sessions/:id returns 404 for non-existent', async () => {
    const res = await app.inject({ method: 'GET', url: '/sessions/00000000-0000-0000-0000-000000000000' });
    expect(res.statusCode).toBe(404);
  });

  it('GET /sessions/:id returns 400 for invalid UUID', async () => {
    const res = await app.inject({ method: 'GET', url: '/sessions/not-a-uuid!!!' });
    expect(res.statusCode).toBe(400);
  });

  it('PUT /sessions/:id/state accepts valid update', async () => {
    const createRes = await app.inject({ method: 'POST', url: '/sessions', payload: validBody });
    const { job_id } = JSON.parse(createRes.body);

    const res = await app.inject({
      method: 'PUT', url: `/sessions/${job_id}/state`,
      payload: { mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3 },
    });
    expect(res.statusCode).toBe(200);

    // Verify state stored in Redis
    const state = await redis.hgetall(`snora:job:${job_id}:state`);
    expect(state.mood).toBe('calm');
  });

  it('DELETE /sessions/:id stops a session', async () => {
    const createRes = await app.inject({ method: 'POST', url: '/sessions', payload: validBody });
    const { job_id } = JSON.parse(createRes.body);

    const res = await app.inject({ method: 'DELETE', url: `/sessions/${job_id}` });
    expect(res.statusCode).toBe(200);
    expect(JSON.parse(res.body).status).toBe('stopping');

    // Verify status in Redis
    const jobData = await redis.hgetall(`snora:job:${job_id}`);
    expect(jobData.status).toBe('stopping');
  });
});
