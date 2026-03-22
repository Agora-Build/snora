import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import Redis from 'ioredis';
import { JobRepository } from '../../../src/shared/job-repository.js';

describe('JobRepository', () => {
  let redis: Redis;
  let repo: JobRepository;

  beforeAll(async () => {
    redis = new Redis('redis://localhost:6379');
    repo = new JobRepository(redis);
  });

  afterAll(async () => {
    await redis.quit();
  });

  beforeEach(async () => {
    // Clean all snora test keys
    const keys = await redis.keys('snora:*');
    if (keys.length > 0) await redis.del(...keys);
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
    expect(fetched!.user_id).toBe('user-1');
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
    const ttl = await redis.ttl(`snora:job:${job.id}`);
    expect(ttl).toBeGreaterThan(0);
    expect(ttl).toBeLessThanOrEqual(86400);
  });

  it('stores and retrieves physiological state', async () => {
    const job = await repo.create({
      user_id: 'user-1',
      config: {
        agora: { token: 'tok', channel: 'ch' },
        preferences: { soundscape: 'rain', volume: 0.7 },
      },
    });

    await repo.updateState(job.id, {
      mood: 'calm',
      heart_rate: '68',
      stress_level: '0.3',
    });

    const state = await redis.hgetall(`snora:job:${job.id}:state`);
    expect(state.mood).toBe('calm');
    expect(state.heart_rate).toBe('68');
  });

  it('publishes state updates via pub/sub', async () => {
    const job = await repo.create({
      user_id: 'user-1',
      config: {
        agora: { token: 'tok', channel: 'ch' },
        preferences: { soundscape: 'rain', volume: 0.7 },
      },
    });

    // Subscribe to channel
    const subscriber = redis.duplicate();
    const received: string[] = [];
    await subscriber.subscribe(`snora:channel:state:${job.id}`);
    subscriber.on('message', (_ch, msg) => received.push(msg));

    // Small delay for subscription to be active
    await new Promise(r => setTimeout(r, 100));

    await repo.publishStateUpdate(job.id, { mood: 'calm', heart_rate: 68 });

    // Wait for message
    await new Promise(r => setTimeout(r, 100));

    expect(received).toHaveLength(1);
    expect(JSON.parse(received[0])).toEqual({ mood: 'calm', heart_rate: 68 });

    await subscriber.unsubscribe();
    await subscriber.quit();
  });
});
