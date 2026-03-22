import { spawn, type ChildProcess } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import type Redis from 'ioredis';
import type { Logger } from 'pino';
import { EngineBridge } from './engine-bridge.js';
import { Heartbeat } from './heartbeat.js';
import { SessionMonitor } from './session-monitor.js';
import type { JobRepository } from '../shared/job-repository.js';
import type { Config } from '../shared/config.js';
import type { Job } from '../shared/types.js';

const TOKEN_EXPIRY_COUNTDOWN_MS = 60_000;
const SHUTDOWN_WAIT_MS = 30_000;
const POLL_INTERVAL_MS = 2_000;

interface ActiveSession {
  job: Job;
  process: ChildProcess;
  bridge: EngineBridge;
  monitor: SessionMonitor;
  subscriber: Redis;
  socketPath: string;
  tokenExpiryTimer: ReturnType<typeof setTimeout> | null;
}

export class WorkerManager {
  /** Map of jobId → session context */
  private activeSessions = new Map<string, ActiveSession>();

  private heartbeat: Heartbeat;
  private pollTimer: ReturnType<typeof setInterval> | null = null;
  private shuttingDown = false;

  constructor(
    private redis: Redis,
    private jobRepo: JobRepository,
    private config: Config,
    private logger: Logger,
  ) {
    this.heartbeat = new Heartbeat(redis, process.pid);
  }

  // ─── Public API ────────────────────────────────────────────────────────────

  canAcceptSession(): boolean {
    return this.activeSessions.size < this.config.maxConcurrentSessions;
  }

  activeSessionCount(): number {
    return this.activeSessions.size;
  }

  // ─── Lifecycle ─────────────────────────────────────────────────────────────

  async start(): Promise<void> {
    this.logger.info({ pid: process.pid }, 'Worker manager starting');

    await this.recoverOrphanedJobs();
    await this.heartbeat.start();

    process.on('SIGTERM', () => this.shutdown());

    this.pollTimer = setInterval(() => this.poll(), POLL_INTERVAL_MS);
    // Run first poll immediately
    await this.poll();
  }

  async shutdown(): Promise<void> {
    if (this.shuttingDown) return;
    this.shuttingDown = true;

    this.logger.info('Worker manager shutting down');

    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }

    // Send shutdown to all active engines
    const shutdownPromises = [...this.activeSessions.values()].map((session) =>
      session.bridge.send({ type: 'shutdown' }).catch(() => {}),
    );
    await Promise.all(shutdownPromises);

    // Wait up to 30s for engines to exit
    const deadline = Date.now() + SHUTDOWN_WAIT_MS;
    while (this.activeSessions.size > 0 && Date.now() < deadline) {
      await new Promise((r) => setTimeout(r, 500));
    }

    // Mark any remaining sessions as stopped
    for (const [jobId, session] of this.activeSessions) {
      this.logger.warn({ jobId }, 'Force-stopping session after shutdown timeout');
      session.bridge.close();
      session.process.kill();
      await this.jobRepo.updateStatus(jobId, 'stopped', { stop_reason: 'worker_shutdown' });
      await this.cleanupSession(jobId, false);
    }

    await this.heartbeat.stop();
    this.logger.info('Worker manager stopped');
  }

  // ─── Orphan Recovery ───────────────────────────────────────────────────────

  private async recoverOrphanedJobs(): Promise<void> {
    const keys = await this.redis.keys('snora:job:*');
    // Filter out state sub-keys
    const jobKeys = keys.filter((k) => !k.includes(':state'));

    for (const key of jobKeys) {
      const data = await this.redis.hgetall(key);
      if (!data || !data.id) continue;
      if (data.status !== 'running' && data.status !== 'starting') continue;

      const pid = data.worker_pid;
      if (!pid) {
        // No PID recorded — mark failed
        this.logger.warn({ jobId: data.id }, 'Orphaned job with no PID, marking failed');
        await this.jobRepo.updateStatus(data.id, 'failed', { error: 'worker_crashed' });
        continue;
      }

      const heartbeatKey = `snora:worker:${pid}`;
      const alive = await this.redis.exists(heartbeatKey);
      if (!alive) {
        this.logger.warn({ jobId: data.id, pid }, 'Orphaned job detected, marking failed');
        await this.jobRepo.updateStatus(data.id, 'failed', { error: 'worker_crashed' });
      } else {
        this.logger.info({ jobId: data.id, pid }, 'Job has valid heartbeat, skipping recovery');
      }
    }
  }

  // ─── Polling ───────────────────────────────────────────────────────────────

  private async poll(): Promise<void> {
    if (this.shuttingDown) return;
    if (!this.canAcceptSession()) return;

    // Scan for pending jobs
    const keys = await this.redis.keys('snora:job:*');
    const jobKeys = keys.filter((k) => !k.includes(':state'));

    for (const key of jobKeys) {
      if (!this.canAcceptSession() || this.shuttingDown) break;

      const data = await this.redis.hgetall(key);
      if (!data || data.status !== 'pending') continue;

      const job: Job = {
        id: data.id,
        status: data.status as Job['status'],
        user_id: data.user_id,
        config: JSON.parse(data.config),
        created_at: parseInt(data.created_at, 10),
      };

      // Attempt atomic claim: set status to starting only if still pending
      const updated = await this.redis.hset(key, 'status', 'starting', 'worker_pid', String(process.pid));
      if (updated === 0 && data.status !== 'pending') continue; // raced

      this.logger.info({ jobId: job.id }, 'Claiming pending job');
      await this.jobRepo.updateStatus(job.id, 'starting', { worker_pid: String(process.pid) });

      this.spawnSession(job).catch((err) => {
        this.logger.error({ jobId: job.id, err }, 'Failed to spawn session');
      });
    }
  }

  // ─── Session Spawning ──────────────────────────────────────────────────────

  private async spawnSession(job: Job): Promise<void> {
    const socketPath = join(tmpdir(), `snora-engine-${job.id}.sock`);

    this.logger.info({ jobId: job.id, socketPath }, 'Spawning engine process');

    // Spawn the C++ engine (binary name: snora-engine, assumed to be on PATH)
    const engineProcess = spawn('snora-engine', ['--socket', socketPath, '--gpu', String(this.config.gpuDeviceId)], {
      env: { ...process.env },
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    engineProcess.stdout?.on('data', (data: Buffer) => {
      this.logger.debug({ jobId: job.id }, `engine stdout: ${data.toString().trim()}`);
    });
    engineProcess.stderr?.on('data', (data: Buffer) => {
      this.logger.debug({ jobId: job.id }, `engine stderr: ${data.toString().trim()}`);
    });

    // Give engine a moment to create the socket
    await new Promise((r) => setTimeout(r, 500));

    const bridge = new EngineBridge(socketPath);

    try {
      await bridge.connect();
    } catch (err) {
      this.logger.error({ jobId: job.id, err }, 'Failed to connect to engine socket');
      engineProcess.kill();
      await this.jobRepo.updateStatus(job.id, 'failed', { error: 'engine_connect_failed' });
      return;
    }

    const monitor = new SessionMonitor(job.id, {
      idleTimeoutMs: this.config.idleTimeoutMinutes * 60 * 1000,
      maxDurationMs: this.config.maxSessionDurationHours * 60 * 60 * 1000,
      disconnectGraceMs: this.config.clientDisconnectGraceMinutes * 60 * 1000,
    });

    // Subscribe to Redis pub/sub for state updates for this job
    const subscriber = this.redis.duplicate();
    const stateChannel = `snora:channel:state:${job.id}`;
    const tokenChannel = `snora:channel:token:${job.id}`;
    await subscriber.subscribe(stateChannel, tokenChannel);

    const session: ActiveSession = {
      job,
      process: engineProcess,
      bridge,
      monitor,
      subscriber,
      socketPath,
      tokenExpiryTimer: null,
    };

    this.activeSessions.set(job.id, session);

    // Wire up event handlers
    this.wireSessionEvents(job.id, session);

    // Send init message to engine
    const initPayload = {
      app_id: this.config.agoraAppId,
      token: job.config.agora.token,
      channel: job.config.agora.channel,
      soundscape: job.config.preferences.soundscape,
      binaural_beats: job.config.preferences.binaural_beats ?? false,
      volume: job.config.preferences.volume,
      assets_path: this.config.assetsPath,
    };

    const acked = await bridge.send({ type: 'init', data: initPayload });
    if (!acked) {
      this.logger.error({ jobId: job.id }, 'Engine did not ack init message');
      await this.terminateSession(job.id, 'failed', 'engine_init_timeout');
      return;
    }

    // Mark job as running
    await this.jobRepo.updateStatus(job.id, 'running', { worker_pid: String(process.pid) });
    job.status = 'running';

    // Start the session monitor
    monitor.start();

    this.logger.info({ jobId: job.id }, 'Session started');
  }

  // ─── Event Wiring ──────────────────────────────────────────────────────────

  private wireSessionEvents(jobId: string, session: ActiveSession): void {
    const { bridge, monitor, subscriber, process: engineProcess } = session;

    // Redis pub/sub messages → engine IPC
    subscriber.on('message', (channel: string, message: string) => {
      const currentSession = this.activeSessions.get(jobId);
      if (!currentSession) return;

      const stateChannel = `snora:channel:state:${jobId}`;
      const tokenChannel = `snora:channel:token:${jobId}`;

      if (channel === stateChannel) {
        let parsed: Record<string, unknown>;
        try {
          parsed = JSON.parse(message);
        } catch {
          this.logger.warn({ jobId, channel }, 'Malformed state update payload');
          return;
        }
        monitor.recordStateUpdate();
        bridge.send({ type: 'state_update', data: parsed }).catch((err) => {
          this.logger.error({ jobId, err }, 'Failed to forward state update to engine');
        });
      } else if (channel === tokenChannel) {
        // Token refreshed — cancel any expiry countdown and forward to engine
        if (currentSession.tokenExpiryTimer) {
          clearTimeout(currentSession.tokenExpiryTimer);
          currentSession.tokenExpiryTimer = null;
        }
        let parsed: Record<string, unknown>;
        try {
          parsed = JSON.parse(message);
        } catch {
          this.logger.warn({ jobId, channel }, 'Malformed token update payload');
          return;
        }
        bridge.send({ type: 'token_update', data: parsed }).catch((err) => {
          this.logger.error({ jobId, err }, 'Failed to forward token update to engine');
        });
      }
    });

    // Engine status events (from IPC)
    bridge.on('status', (data: Record<string, unknown>) => {
      const currentSession = this.activeSessions.get(jobId);
      if (!currentSession) return;

      const reason = data.reason as string | undefined;
      this.logger.debug({ jobId, reason }, 'Engine status event');

      switch (reason) {
        case 'no_subscribers':
          monitor.startDisconnectGrace();
          break;

        case 'subscriber_joined':
          monitor.cancelDisconnectGrace();
          break;

        case 'token_expiring':
          // Start 60-second countdown; if not refreshed in time, fail the session
          if (!currentSession.tokenExpiryTimer) {
            currentSession.tokenExpiryTimer = setTimeout(() => {
              this.logger.warn({ jobId }, 'Token expired, terminating session');
              this.terminateSession(jobId, 'failed', 'token_expired').catch((err) => {
                this.logger.error({ jobId, err }, 'Error terminating session after token expiry');
              });
            }, TOKEN_EXPIRY_COUNTDOWN_MS);
          }
          break;

        case 'agora_disconnected':
          this.terminateSession(jobId, 'failed', 'agora_disconnected').catch((err) => {
            this.logger.error({ jobId, err }, 'Error terminating session after agora disconnect');
          });
          break;

        default:
          this.logger.debug({ jobId, reason }, 'Unhandled engine status reason');
      }
    });

    // Engine bridge error/close
    bridge.on('error', (err: Error) => {
      this.logger.error({ jobId, err }, 'Engine bridge error');
    });

    bridge.on('close', () => {
      // Bridge closed: engine socket went away
      const currentSession = this.activeSessions.get(jobId);
      if (!currentSession) return;
      this.logger.warn({ jobId }, 'Engine bridge closed unexpectedly');
    });

    // Engine process exit
    engineProcess.on('exit', (code: number | null, signal: string | null) => {
      this.logger.info({ jobId, code, signal }, 'Engine process exited');
      const isFailed = code !== 0 && code !== null;
      if (isFailed) {
        this.terminateSession(jobId, 'failed', 'engine_crashed').catch((err) => {
          this.logger.error({ jobId, err }, 'Error marking session failed after engine crash');
        });
      } else {
        this.cleanupSession(jobId, false).catch((err) => {
          this.logger.error({ jobId, err }, 'Error cleaning up session after engine exit');
        });
      }
    });

    // Session monitor events
    monitor.on('idle_timeout', async (id: string) => {
      this.logger.info({ jobId: id }, 'Session idle timeout');
      await this.terminateSession(id, 'stopped', 'idle_timeout').catch((err) => {
        this.logger.error({ jobId: id, err }, 'Error terminating idle session');
      });
    });

    monitor.on('max_duration', async (id: string) => {
      this.logger.info({ jobId: id }, 'Session max duration reached');
      await this.terminateSession(id, 'stopped', 'max_duration').catch((err) => {
        this.logger.error({ jobId: id, err }, 'Error terminating max-duration session');
      });
    });

    monitor.on('client_disconnected', async (id: string) => {
      this.logger.info({ jobId: id }, 'Client disconnect grace expired');
      await this.terminateSession(id, 'client_disconnected', 'client_disconnected').catch((err) => {
        this.logger.error({ jobId: id, err }, 'Error terminating disconnected session');
      });
    });
  }

  // ─── Session Termination ───────────────────────────────────────────────────

  private async terminateSession(
    jobId: string,
    finalStatus: 'stopped' | 'failed' | 'client_disconnected',
    reason: string,
  ): Promise<void> {
    const session = this.activeSessions.get(jobId);
    if (!session) return;

    this.logger.info({ jobId, finalStatus, reason }, 'Terminating session');

    // Send shutdown to engine (best-effort)
    await session.bridge.send({ type: 'shutdown' }).catch(() => {});

    const extra: Record<string, string> = {};
    if (finalStatus === 'failed') {
      extra.error = reason;
    } else {
      extra.stop_reason = reason;
    }

    await this.jobRepo.updateStatus(jobId, finalStatus, extra);
    await this.cleanupSession(jobId, true);
  }

  private async cleanupSession(jobId: string, killProcess: boolean): Promise<void> {
    const session = this.activeSessions.get(jobId);
    if (!session) return;

    // Cancel token expiry timer
    if (session.tokenExpiryTimer) {
      clearTimeout(session.tokenExpiryTimer);
      session.tokenExpiryTimer = null;
    }

    session.monitor.stop();
    session.bridge.close();

    if (killProcess && session.process.exitCode === null) {
      session.process.kill('SIGTERM');
    }

    await session.subscriber.unsubscribe().catch(() => {});
    await session.subscriber.quit().catch(() => {});

    this.activeSessions.delete(jobId);
    this.logger.info({ jobId }, 'Session cleaned up');
  }
}
