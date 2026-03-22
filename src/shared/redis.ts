import { Redis } from 'ioredis';
import type { Logger } from 'pino';

export function createRedisClient(url: string, logger: Logger): Redis {
  const client = new Redis(url, {
    maxRetriesPerRequest: 3,
    retryStrategy(times: number) {
      const delay = Math.min(times * 200, 5000);
      return delay;
    },
  });

  client.on('error', (err: Error) => {
    logger.error({ err }, 'Redis connection error');
  });

  client.on('connect', () => {
    logger.info('Redis connected');
  });

  return client;
}
