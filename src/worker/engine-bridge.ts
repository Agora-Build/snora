import { connect, type Socket } from 'node:net';
import { EventEmitter } from 'node:events';
import { encodeMessage, MessageDecoder } from '../shared/ipc-protocol.js';
import type { IpcMessage } from '../shared/types.js';

const DEFAULT_ACK_TIMEOUT = 5000;

export class EngineBridge extends EventEmitter {
  private socket: Socket | null = null;
  private decoder = new MessageDecoder();
  private pendingAck: { resolve: (acked: boolean) => void } | null = null;

  constructor(
    private socketPath: string,
    private ackTimeout = DEFAULT_ACK_TIMEOUT,
  ) {
    super();
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket = connect(this.socketPath, () => resolve());
      this.socket.on('error', (err) => {
        this.emit('error', err);
        reject(err);
      });
      this.socket.on('close', () => this.emit('close'));
      this.socket.on('data', (data) => this.decoder.feed(data));

      this.decoder.on('message', (msg: IpcMessage) => {
        if (msg.type === 'ack' && this.pendingAck) {
          this.pendingAck.resolve(true);
          this.pendingAck = null;
        } else if (msg.type === 'status') {
          this.emit('status', msg.data);
        }
      });

      this.decoder.on('error', (err) => this.emit('error', err));
    });
  }

  send(msg: IpcMessage): Promise<boolean> {
    return new Promise((resolve) => {
      if (!this.socket) {
        resolve(false);
        return;
      }

      const timer = setTimeout(() => {
        this.pendingAck = null;
        resolve(false);
      }, this.ackTimeout);

      this.pendingAck = {
        resolve: (acked) => {
          clearTimeout(timer);
          resolve(acked);
        },
      };

      this.socket.write(encodeMessage(msg));
    });
  }

  close(): void {
    if (this.pendingAck) {
      this.pendingAck.resolve(false);
      this.pendingAck = null;
    }
    this.socket?.destroy();
    this.socket = null;
  }
}
