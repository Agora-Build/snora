import { Redis } from 'ioredis';
import { loadConfig } from '../shared/config.js';
import { createLogger } from '../shared/logger.js';
import { JobRepository } from '../shared/job-repository.js';
import { WorkerManager } from './manager.js';

async function main(): Promise<void> {
  const config = loadConfig();
  const logger = createLogger('worker', config.logLevel);

  logger.info('Snora worker starting');

  const redis = new Redis(config.redisUrl, {
    maxRetriesPerRequest: null,
    enableReadyCheck: true,
  });

  redis.on('error', (err: Error) => {
    logger.error({ err }, 'Redis connection error');
  });

  redis.on('connect', () => {
    logger.info('Connected to Redis');
  });

  const jobRepo = new JobRepository(redis);
  const manager = new WorkerManager(redis, jobRepo, config, logger);

  // Graceful shutdown handler (also registered inside manager, but we handle Redis cleanup here)
  const gracefulShutdown = async (signal: string): Promise<void> => {
    logger.info({ signal }, 'Received shutdown signal');
    try {
      await manager.shutdown();
    } finally {
      await redis.quit();
      process.exit(0);
    }
  };

  process.on('SIGTERM', () => gracefulShutdown('SIGTERM'));
  process.on('SIGINT', () => gracefulShutdown('SIGINT'));

  try {
    await manager.start();
    logger.info('Worker manager running');
  } catch (err) {
    logger.error({ err }, 'Worker manager failed to start');
    await redis.quit();
    process.exit(1);
  }
}

main().catch((err) => {
  console.error('Fatal worker error:', err);
  process.exit(1);
});
