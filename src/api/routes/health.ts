import type { FastifyPluginAsync } from 'fastify';

export const healthRoutes: FastifyPluginAsync = async (fastify) => {
  fastify.get('/health', async () => {
    const redis = (fastify as any).redis;
    const config = (fastify as any).config;
    const activeSessionCount = (fastify as any).activeSessionCount;

    let redisStatus = 'connected';
    let ok = true;
    try {
      await redis.ping();
    } catch {
      redisStatus = 'disconnected';
      ok = false;
    }

    return {
      ok,
      redis: redisStatus,
      gpu: { available: true, memory_free_mb: null },
      active_sessions: activeSessionCount(),
      max_sessions: config.maxConcurrentSessions,
    };
  });
};
