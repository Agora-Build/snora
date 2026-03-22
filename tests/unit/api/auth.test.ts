import { describe, it, expect } from 'vitest';
import Fastify from 'fastify';
import { authPlugin } from '../../../src/api/plugins/auth.js';

async function buildApp(apiKey: string) {
  const app = Fastify();
  await app.register(authPlugin, { apiKey });
  app.get('/test', async () => ({ ok: true }));
  return app;
}

describe('authPlugin', () => {
  it('allows requests with valid API key', async () => {
    const app = await buildApp('test-key');
    const res = await app.inject({ method: 'GET', url: '/test', headers: { 'x-api-key': 'test-key' } });
    expect(res.statusCode).toBe(200);
  });

  it('rejects requests without API key', async () => {
    const app = await buildApp('test-key');
    const res = await app.inject({ method: 'GET', url: '/test' });
    expect(res.statusCode).toBe(401);
  });

  it('rejects requests with wrong API key', async () => {
    const app = await buildApp('test-key');
    const res = await app.inject({ method: 'GET', url: '/test', headers: { 'x-api-key': 'wrong' } });
    expect(res.statusCode).toBe(401);
  });

  it('skips auth for /health', async () => {
    const app = await buildApp('test-key');
    app.get('/health', async () => ({ ok: true }));
    const res = await app.inject({ method: 'GET', url: '/health' });
    expect(res.statusCode).toBe(200);
  });

  it('skips auth for /metrics', async () => {
    const app = await buildApp('test-key');
    app.get('/metrics', async () => 'metrics');
    const res = await app.inject({ method: 'GET', url: '/metrics' });
    expect(res.statusCode).toBe(200);
  });
});
