import { describe, it, expect, afterEach } from 'vitest';
import { createServer, type Server } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomUUID } from 'node:crypto';
import { EngineBridge } from '../../../src/worker/engine-bridge.js';
import { encodeMessage, MessageDecoder } from '../../../src/shared/ipc-protocol.js';

describe('EngineBridge', () => {
  let socketPath: string;
  let mockServer: Server;

  afterEach(() => {
    mockServer?.close();
  });

  function startAutoAckServer(): Promise<void> {
    socketPath = join(tmpdir(), `snora-test-${randomUUID()}.sock`);
    return new Promise((resolve) => {
      mockServer = createServer((conn) => {
        const decoder = new MessageDecoder();
        conn.on('data', (data: Buffer) => decoder.feed(data));
        decoder.on('message', () => {
          conn.write(encodeMessage({ type: 'ack' }));
        });
      });
      mockServer.listen(socketPath, () => resolve());
    });
  }

  it('connects to engine socket and sends init message', async () => {
    await startAutoAckServer();
    const bridge = new EngineBridge(socketPath);
    await bridge.connect();

    const acked = await bridge.send({ type: 'init', data: { app_id: 'test' } });
    expect(acked).toBe(true);

    bridge.close();
  });

  it('sends state_update and receives ack', async () => {
    await startAutoAckServer();
    const bridge = new EngineBridge(socketPath);
    await bridge.connect();

    const acked = await bridge.send({
      type: 'state_update',
      data: { heart_rate: 68, mood: 'calm', stress_level: 0.3 },
    });
    expect(acked).toBe(true);

    bridge.close();
  });

  it('times out if no ack received', async () => {
    // Server that never responds
    socketPath = join(tmpdir(), `snora-silent-${randomUUID()}.sock`);
    mockServer = createServer(() => {});
    await new Promise<void>((r) => mockServer.listen(socketPath, r));

    const bridge = new EngineBridge(socketPath, 500);
    await bridge.connect();

    const acked = await bridge.send({ type: 'init', data: {} });
    expect(acked).toBe(false);

    bridge.close();
  });

  it('emits status events from engine', async () => {
    socketPath = join(tmpdir(), `snora-status-${randomUUID()}.sock`);
    mockServer = createServer((conn) => {
      const decoder = new MessageDecoder();
      conn.on('data', (data: Buffer) => decoder.feed(data));
      decoder.on('message', () => {
        conn.write(encodeMessage({ type: 'ack' }));
        conn.write(encodeMessage({ type: 'status', data: { reason: 'no_subscribers' } }));
      });
    });
    await new Promise<void>((r) => mockServer.listen(socketPath, r));

    const bridge = new EngineBridge(socketPath);
    await bridge.connect();

    const statuses: any[] = [];
    bridge.on('status', (data) => statuses.push(data));

    await bridge.send({ type: 'init', data: {} });
    await new Promise(r => setTimeout(r, 100));

    expect(statuses).toHaveLength(1);
    expect(statuses[0].reason).toBe('no_subscribers');

    bridge.close();
  });
});
