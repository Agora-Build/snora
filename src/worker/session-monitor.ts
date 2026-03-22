import { EventEmitter } from 'node:events';

interface SessionMonitorConfig {
  idleTimeoutMs: number;
  maxDurationMs: number;
  disconnectGraceMs: number;
}

export class SessionMonitor extends EventEmitter {
  private idleTimer: ReturnType<typeof setTimeout> | null = null;
  private durationTimer: ReturnType<typeof setTimeout> | null = null;
  private graceTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(
    private jobId: string,
    private config: SessionMonitorConfig,
  ) {
    super();
  }

  start(): void {
    this.resetIdleTimer();
    this.durationTimer = setTimeout(() => {
      this.emit('max_duration', this.jobId);
    }, this.config.maxDurationMs);
  }

  stop(): void {
    if (this.idleTimer) { clearTimeout(this.idleTimer); this.idleTimer = null; }
    if (this.durationTimer) { clearTimeout(this.durationTimer); this.durationTimer = null; }
    if (this.graceTimer) { clearTimeout(this.graceTimer); this.graceTimer = null; }
  }

  recordStateUpdate(): void {
    this.resetIdleTimer();
  }

  startDisconnectGrace(): void {
    if (this.graceTimer) return;
    this.graceTimer = setTimeout(() => {
      this.graceTimer = null;
      this.emit('client_disconnected', this.jobId);
    }, this.config.disconnectGraceMs);
  }

  cancelDisconnectGrace(): void {
    if (this.graceTimer) {
      clearTimeout(this.graceTimer);
      this.graceTimer = null;
    }
  }

  private resetIdleTimer(): void {
    if (this.idleTimer) clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => {
      this.emit('idle_timeout', this.jobId);
    }, this.config.idleTimeoutMs);
  }
}
