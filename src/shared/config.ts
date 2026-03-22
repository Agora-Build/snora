export interface Config {
  apiKey: string;
  redisUrl: string;
  agoraAppId: string;
  assetsPath: string;
  maxConcurrentSessions: number;
  gpuDeviceId: number;
  port: number;
  maxSessionDurationHours: number;
  idleTimeoutMinutes: number;
  clientDisconnectGraceMinutes: number;
  logLevel: string;
}

function required(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

function optional(name: string, defaultValue: string): string {
  return process.env[name] ?? defaultValue;
}

export function loadConfig(): Config {
  return {
    apiKey: required('SNORA_API_KEY'),
    redisUrl: required('REDIS_URL'),
    agoraAppId: required('AGORA_APP_ID'),
    assetsPath: optional('ASSETS_PATH', '/assets/sounds'),
    maxConcurrentSessions: parseInt(optional('MAX_CONCURRENT_SESSIONS', '4'), 10),
    gpuDeviceId: parseInt(optional('GPU_DEVICE_ID', '0'), 10),
    port: parseInt(optional('PORT', '8080'), 10),
    maxSessionDurationHours: parseInt(optional('MAX_SESSION_DURATION_HOURS', '12'), 10),
    idleTimeoutMinutes: parseInt(optional('IDLE_TIMEOUT_MINUTES', '30'), 10),
    clientDisconnectGraceMinutes: parseInt(optional('CLIENT_DISCONNECT_GRACE_MINUTES', '5'), 10),
    logLevel: optional('LOG_LEVEL', 'info'),
  };
}
