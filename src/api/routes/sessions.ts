import type { FastifyPluginAsync } from 'fastify';
import { createSessionSchema, stateUpdateSchema, tokenUpdateSchema } from '../../shared/validation.js';
import type { JobRepository } from '../../shared/job-repository.js';

const UUID_REGEX = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function validateUuid(id: string): boolean {
  return UUID_REGEX.test(id);
}

export const sessionRoutes: FastifyPluginAsync = async (fastify) => {
  const repo = (fastify as any).jobRepo as JobRepository;
  const throttle = (fastify as any).throttle as Map<string, number>;

  // POST /sessions (stricter rate limit: 10/min per API key)
  fastify.post('/', {
    schema: { body: createSessionSchema },
    config: { rateLimit: { max: 10, timeWindow: '1 minute' } },
  }, async (request, reply) => {
    const body = request.body as any;
    const job = await repo.create({
      user_id: body.user_id,
      config: {
        agora: body.agora,
        preferences: body.preferences,
      },
    });

    // Store initial state
    await repo.updateState(job.id, {
      mood: body.initial_state.mood,
      heart_rate: String(body.initial_state.heart_rate),
      hrv: String(body.initial_state.hrv),
      respiration_rate: String(body.initial_state.respiration_rate),
      stress_level: String(body.initial_state.stress_level),
    });

    reply.code(202).send({ job_id: job.id, status: job.status });
  });

  // GET /sessions/:id
  fastify.get<{ Params: { id: string } }>('/:id', async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job) {
      reply.code(404).send({ error: 'Session not found' });
      return;
    }

    return {
      job_id: job.id,
      status: job.status,
      created_at: job.created_at,
      error: job.error,
      stop_reason: job.stop_reason,
      token_status: job.token_status,
    };
  });

  // PUT /sessions/:id/state
  fastify.put<{ Params: { id: string } }>('/:id/state', {
    schema: { body: stateUpdateSchema },
  }, async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job) {
      reply.code(404).send({ error: 'Session not found' });
      return;
    }

    // Throttle: 1 update per second per session
    const now = Date.now();
    const lastUpdate = throttle.get(id) ?? 0;
    if (now - lastUpdate < 1000) {
      return { ok: true };
    }
    throttle.set(id, now);

    const body = request.body as any;
    const state = {
      mood: body.mood,
      heart_rate: String(body.heart_rate),
      hrv: String(body.hrv),
      respiration_rate: String(body.respiration_rate),
      stress_level: String(body.stress_level),
    };

    await repo.updateState(id, state);
    await repo.publishStateUpdate(id, body);

    return { ok: true };
  });

  // PUT /sessions/:id/token
  fastify.put<{ Params: { id: string } }>('/:id/token', {
    schema: { body: tokenUpdateSchema },
  }, async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job || job.status !== 'running') {
      reply.code(404).send({ error: 'Running session not found' });
      return;
    }

    const body = request.body as any;
    await repo.publishStateUpdate(id, { type: 'token_update', token: body.token });
    await repo.updateStatus(id, job.status, { token_status: '' });

    return { ok: true };
  });

  // DELETE /sessions/:id
  fastify.delete<{ Params: { id: string } }>('/:id', async (request, reply) => {
    const { id } = request.params;
    if (!validateUuid(id)) {
      reply.code(400).send({ error: 'Invalid job ID format' });
      return;
    }

    const job = await repo.get(id);
    if (!job) {
      reply.code(404).send({ error: 'Session not found' });
      return;
    }

    await repo.updateStatus(id, 'stopping');
    await repo.publishStateUpdate(id, { type: 'shutdown' });

    return { status: 'stopping' };
  });
};
