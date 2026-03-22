import Fastify from 'fastify';
import rateLimit from '@fastify/rate-limit';
import type { Config } from '../shared/config.js';
import type { Logger } from 'pino';
import type Redis from 'ioredis';
import type { JobRepository } from '../shared/job-repository.js';
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
    loggerInstance: opts.logger,
    bodyLimit: 4096,
  });

  app.decorate('config', opts.config);
  app.decorate('redis', opts.redis);
  app.decorate('jobRepo', opts.jobRepo);
  app.decorate('throttle', new Map<string, number>());
  app.decorate('activeSessionCount', opts.activeSessionCount);

  await app.register(rateLimit, {
    max: 100,
    timeWindow: '1 second',
    keyGenerator: (request) => request.headers['x-api-key'] as string ?? request.ip,
  });
  await app.register(authPlugin, { apiKey: opts.config.apiKey });

  await app.register(sessionRoutes, { prefix: '/sessions' });
  await app.register(healthRoutes);
  await app.register(metricsRoutes);

  return app;
}
