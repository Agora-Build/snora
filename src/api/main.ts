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
