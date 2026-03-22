import type { FastifyPluginAsync } from 'fastify';
import fp from 'fastify-plugin';

interface AuthPluginOptions {
  apiKey: string;
}

const SKIP_AUTH_PATHS = ['/health', '/metrics'];

const authPluginImpl: FastifyPluginAsync<AuthPluginOptions> = async (fastify, opts) => {
  fastify.addHook('onRequest', async (request, reply) => {
    if (SKIP_AUTH_PATHS.includes(request.url)) return;

    const key = request.headers['x-api-key'];
    if (!key || key !== opts.apiKey) {
      reply.code(401).send({ error: 'Unauthorized' });
    }
  });
};

export const authPlugin = fp(authPluginImpl, { name: 'auth' });
