import { describe, it, expect } from 'vitest';
import Fastify from 'fastify';
import rateLimit from '@fastify/rate-limit';

describe('rate limiting', () => {
  it('returns 429 when global rate limit exceeded', async () => {
    const app = Fastify();
    await app.register(rateLimit, { max: 2, timeWindow: '1 second' });
    app.get('/test', async () => ({ ok: true }));

    const res1 = await app.inject({ method: 'GET', url: '/test' });
    expect(res1.statusCode).toBe(200);
    const res2 = await app.inject({ method: 'GET', url: '/test' });
    expect(res2.statusCode).toBe(200);

    const res3 = await app.inject({ method: 'GET', url: '/test' });
    expect(res3.statusCode).toBe(429);
    expect(res3.headers['retry-after']).toBeDefined();

    await app.close();
  });
});
