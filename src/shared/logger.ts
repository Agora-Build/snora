import pino from 'pino';

export type ComponentName = 'api' | 'worker' | 'engine';

export function createLogger(component: ComponentName, level = 'info') {
  return pino({
    level,
    base: { component },
    timestamp: pino.stdTimeFunctions.isoTime,
    formatters: {
      level(label) {
        return { level: label };
      },
    },
  });
}
