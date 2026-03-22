import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import Redis from 'ioredis';
import { Heartbeat } from '../../../src/worker/heartbeat.js';

describe('Heartbeat', () => {
  let redis: Redis;

  beforeAll(async () => {
    redis = new Redis('redis://localhost:6379');
  });

  afterAll(async () => {
    await redis.quit();
  });

  it('sets heartbeat key with TTL on start', async () => {
    const heartbeat = new Heartbeat(redis, 99999);
    await heartbeat.start();

    const value = await redis.get('snora:worker:99999');
    expect(value).toBe('1');

    const ttl = await redis.ttl('snora:worker:99999');
    expect(ttl).toBeGreaterThan(0);
    expect(ttl).toBeLessThanOrEqual(10);

    await heartbeat.stop();
  });

  it('deletes heartbeat key on stop', async () => {
    const heartbeat = new Heartbeat(redis, 99998);
    await heartbeat.start();
    await heartbeat.stop();

    const value = await redis.get('snora:worker:99998');
    expect(value).toBeNull();
  });
});
