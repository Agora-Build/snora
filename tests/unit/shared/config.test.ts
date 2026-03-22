import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { loadConfig } from '../../../src/shared/config.js';

describe('loadConfig', () => {
  const originalEnv = process.env;

  beforeEach(() => {
    process.env = { ...originalEnv };
  });

  afterEach(() => {
    process.env = originalEnv;
  });

  it('loads required env vars', () => {
    process.env.SNORA_API_KEY = 'test-key';
    process.env.REDIS_URL = 'redis://localhost:6379';
    process.env.AGORA_APP_ID = 'test-app-id';

    const config = loadConfig();
    expect(config.apiKey).toBe('test-key');
    expect(config.redisUrl).toBe('redis://localhost:6379');
    expect(config.agoraAppId).toBe('test-app-id');
  });

  it('uses defaults for optional vars', () => {
    process.env.SNORA_API_KEY = 'test-key';
    process.env.REDIS_URL = 'redis://localhost:6379';
    process.env.AGORA_APP_ID = 'test-app-id';

    const config = loadConfig();
    expect(config.port).toBe(8080);
    expect(config.maxConcurrentSessions).toBe(4);
    expect(config.maxSessionDurationHours).toBe(12);
    expect(config.idleTimeoutMinutes).toBe(30);
    expect(config.clientDisconnectGraceMinutes).toBe(5);
    expect(config.logLevel).toBe('info');
    expect(config.assetsPath).toBe('/assets/sounds');
  });

  it('throws if required vars are missing', () => {
    delete process.env.SNORA_API_KEY;
    expect(() => loadConfig()).toThrow('SNORA_API_KEY');
  });
});
