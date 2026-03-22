import type { FastifyPluginAsync } from 'fastify';
import { Registry, Counter, Gauge, Histogram, collectDefaultMetrics } from 'prom-client';

export const registry = new Registry();

export const sessionsActive = new Gauge({ name: 'snora_sessions_active', help: 'Currently running sessions', registers: [registry] });
export const sessionsTotal = new Counter({ name: 'snora_sessions_total', help: 'Total sessions', labelNames: ['status'], registers: [registry] });
export const engineSpawnDuration = new Histogram({ name: 'snora_engine_spawn_duration_seconds', help: 'Engine spawn time', registers: [registry] });
export const stateUpdatesTotal = new Counter({ name: 'snora_state_updates_total', help: 'State updates', labelNames: ['result'], registers: [registry] });
export const engineCrashesTotal = new Counter({ name: 'snora_engine_crashes_total', help: 'Engine crashes', registers: [registry] });

collectDefaultMetrics({ register: registry });

export const metricsRoutes: FastifyPluginAsync = async (fastify) => {
  fastify.get('/metrics', async (_request, reply) => {
    const metrics = await registry.metrics();
    reply.type(registry.contentType).send(metrics);
  });
};
