import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import Redis from 'ioredis';
import { WorkerManager } from '../../../src/worker/manager.js';
import { JobRepository } from '../../../src/shared/job-repository.js';
import { createLogger } from '../../../src/shared/logger.js';
import type { Config } from '../../../src/shared/config.js';

const testConfig: Config = {
  apiKey: 'test',
  redisUrl: 'redis://localhost:6379',
  agoraAppId: 'test-app',
  assetsPath: '/assets/sounds',
  maxConcurrentSessions: 2,
  gpuDeviceId: 0,
  port: 8080,
  maxSessionDurationHours: 12,
  idleTimeoutMinutes: 30,
  clientDisconnectGraceMinutes: 5,
  logLevel: 'silent',
};

describe('WorkerManager', () => {
  let redis: Redis;
  let jobRepo: JobRepository;
  let logger: ReturnType<typeof createLogger>;

  beforeAll(async () => {
    redis = new Redis('redis://localhost:6379');
    jobRepo = new JobRepository(redis);
    logger = createLogger('worker', 'silent');
  });

  afterAll(async () => {
    await redis.quit();
  });

  beforeEach(async () => {
    const keys = await redis.keys('snora:*');
    if (keys.length > 0) await redis.del(...keys);
  });

  it('enforces max concurrent session limit', () => {
    const manager = new WorkerManager(redis, jobRepo, testConfig, logger);
    expect(manager.canAcceptSession()).toBe(true);
    // Access internal state to simulate active sessions
    (manager as any).activeSessions.set('s1', {});
    (manager as any).activeSessions.set('s2', {});
    expect(manager.canAcceptSession()).toBe(false);
  });

  it('tracks active session count', () => {
    const manager = new WorkerManager(redis, jobRepo, testConfig, logger);
    expect(manager.activeSessionCount()).toBe(0);
    (manager as any).activeSessions.set('s1', {});
    expect(manager.activeSessionCount()).toBe(1);
  });

  it('recovers orphaned jobs on startup', async () => {
    // Create a job that looks orphaned (running status, no heartbeat)
    const job = await jobRepo.create({
      user_id: 'orphan-user',
      config: { agora: { token: 't', channel: 'c' }, preferences: { soundscape: 'rain', volume: 0.5 } },
    });
    await jobRepo.updateStatus(job.id, 'running', { worker_pid: '99999' });
    // No heartbeat key exists for pid 99999

    const manager = new WorkerManager(redis, jobRepo, testConfig, logger);
    await (manager as any).recoverOrphanedJobs();

    const recovered = await jobRepo.get(job.id);
    expect(recovered!.status).toBe('failed');
    expect(recovered!.error).toBe('worker_crashed');
  });

  it('does not recover jobs with valid heartbeat', async () => {
    const job = await jobRepo.create({
      user_id: 'alive-user',
      config: { agora: { token: 't', channel: 'c' }, preferences: { soundscape: 'rain', volume: 0.5 } },
    });
    await jobRepo.updateStatus(job.id, 'running', { worker_pid: '88888' });
    // Set a valid heartbeat
    await redis.set('snora:worker:88888', '1', 'EX', 10);

    const manager = new WorkerManager(redis, jobRepo, testConfig, logger);
    await (manager as any).recoverOrphanedJobs();

    const alive = await jobRepo.get(job.id);
    expect(alive!.status).toBe('running'); // unchanged
  });
});
