import { v4 as uuidv4 } from 'uuid';
import type { Redis } from 'ioredis';
import type { Job, JobStatus } from './types.js';

const TERMINAL_STATUSES: JobStatus[] = ['stopped', 'failed', 'client_disconnected'];
const JOB_TTL_SECONDS = 86400; // 24 hours

interface CreateJobInput {
  user_id: string;
  config: Job['config'];
}

export class JobRepository {
  constructor(private redis: Redis) {}

  async create(input: CreateJobInput): Promise<Job> {
    const id = uuidv4();
    const job: Job = {
      id,
      status: 'pending',
      user_id: input.user_id,
      config: input.config,
      created_at: Date.now(),
    };

    const key = `snora:job:${id}`;
    await this.redis.hset(key, 'id', id, 'status', 'pending', 'user_id', input.user_id,
      'config', JSON.stringify(input.config), 'created_at', String(job.created_at));

    return job;
  }

  async get(id: string): Promise<Job | null> {
    const key = `snora:job:${id}`;
    const data = await this.redis.hgetall(key);
    if (!data || !data.id) return null;

    return {
      id: data.id,
      status: data.status as JobStatus,
      user_id: data.user_id,
      config: JSON.parse(data.config),
      worker_pid: data.worker_pid ? parseInt(data.worker_pid, 10) : undefined,
      created_at: parseInt(data.created_at, 10),
      error: data.error || undefined,
      stop_reason: data.stop_reason || undefined,
      token_status: data.token_status || undefined,
    };
  }

  async updateStatus(id: string, status: JobStatus, extra?: Record<string, string>): Promise<void> {
    const key = `snora:job:${id}`;
    const fields = ['status', status];
    if (extra) {
      for (const [k, v] of Object.entries(extra)) {
        fields.push(k, v);
      }
    }
    await this.redis.hset(key, ...fields);

    if (TERMINAL_STATUSES.includes(status)) {
      await this.redis.expire(key, JOB_TTL_SECONDS);
      await this.redis.expire(`snora:job:${id}:state`, JOB_TTL_SECONDS);
    }
  }

  async updateState(id: string, state: Record<string, string>): Promise<void> {
    const key = `snora:job:${id}:state`;
    const fields: string[] = [];
    for (const [k, v] of Object.entries(state)) {
      fields.push(k, v);
    }
    await this.redis.hset(key, ...fields);
  }

  async publishStateUpdate(id: string, state: Record<string, unknown>): Promise<void> {
    const channel = `snora:channel:state:${id}`;
    await this.redis.publish(channel, JSON.stringify(state));
  }
}
