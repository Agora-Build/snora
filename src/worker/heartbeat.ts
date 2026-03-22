import type Redis from 'ioredis';

const HEARTBEAT_INTERVAL = 5000;
const HEARTBEAT_TTL = 10;

export class Heartbeat {
  private interval: ReturnType<typeof setInterval> | null = null;

  constructor(
    private redis: Redis,
    private pid: number,
  ) {}

  private get key(): string {
    return `snora:worker:${this.pid}`;
  }

  async start(): Promise<void> {
    await this.beat();
    this.interval = setInterval(() => this.beat(), HEARTBEAT_INTERVAL);
  }

  async stop(): Promise<void> {
    if (this.interval) {
      clearInterval(this.interval);
      this.interval = null;
    }
    await this.redis.del(this.key);
  }

  private async beat(): Promise<void> {
    await this.redis.set(this.key, '1', 'EX', HEARTBEAT_TTL);
  }
}
