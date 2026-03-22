import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { SessionMonitor } from '../../../src/worker/session-monitor.js';

describe('SessionMonitor', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('emits idle_timeout when no state updates received', () => {
    const onTimeout = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 1000, maxDurationMs: 60000, disconnectGraceMs: 500,
    });
    monitor.on('idle_timeout', onTimeout);
    monitor.start();
    vi.advanceTimersByTime(1000);
    expect(onTimeout).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('resets idle timer on state update', () => {
    const onTimeout = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 1000, maxDurationMs: 60000, disconnectGraceMs: 500,
    });
    monitor.on('idle_timeout', onTimeout);
    monitor.start();
    vi.advanceTimersByTime(800);
    monitor.recordStateUpdate();
    vi.advanceTimersByTime(800);
    expect(onTimeout).not.toHaveBeenCalled();
    vi.advanceTimersByTime(200);
    expect(onTimeout).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('emits max_duration when session exceeds limit', () => {
    const onMaxDuration = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 60000, maxDurationMs: 2000, disconnectGraceMs: 500,
    });
    monitor.on('max_duration', onMaxDuration);
    monitor.start();
    vi.advanceTimersByTime(2000);
    expect(onMaxDuration).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('handles disconnect grace period', () => {
    const onDisconnect = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 60000, maxDurationMs: 60000, disconnectGraceMs: 1000,
    });
    monitor.on('client_disconnected', onDisconnect);
    monitor.start();
    monitor.startDisconnectGrace();
    vi.advanceTimersByTime(999);
    expect(onDisconnect).not.toHaveBeenCalled();
    vi.advanceTimersByTime(1);
    expect(onDisconnect).toHaveBeenCalledTimes(1);
    monitor.stop();
  });

  it('cancels disconnect grace on reconnect', () => {
    const onDisconnect = vi.fn();
    const monitor = new SessionMonitor('job-1', {
      idleTimeoutMs: 60000, maxDurationMs: 60000, disconnectGraceMs: 1000,
    });
    monitor.on('client_disconnected', onDisconnect);
    monitor.start();
    monitor.startDisconnectGrace();
    vi.advanceTimersByTime(500);
    monitor.cancelDisconnectGrace();
    vi.advanceTimersByTime(1000);
    expect(onDisconnect).not.toHaveBeenCalled();
    monitor.stop();
  });
});
