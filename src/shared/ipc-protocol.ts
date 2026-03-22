import { EventEmitter } from 'node:events';
import type { IpcMessage } from './types.js';

const MAX_MESSAGE_SIZE = 64 * 1024; // 64KB
const HEADER_SIZE = 4;

export function encodeMessage(msg: IpcMessage): Buffer {
  const json = JSON.stringify(msg);
  const payload = Buffer.from(json, 'utf-8');
  if (payload.length > MAX_MESSAGE_SIZE) {
    throw new Error(`Message size ${payload.length} exceeds max ${MAX_MESSAGE_SIZE}`);
  }
  const header = Buffer.alloc(HEADER_SIZE);
  header.writeUInt32BE(payload.length, 0);
  return Buffer.concat([header, payload]);
}

export class MessageDecoder extends EventEmitter {
  private buffer = Buffer.alloc(0);
  private expectedLength: number | null = null;

  feed(data: Buffer): void {
    this.buffer = Buffer.concat([this.buffer, data]);
    this.drain();
  }

  private drain(): void {
    while (true) {
      if (this.expectedLength === null) {
        if (this.buffer.length < HEADER_SIZE) return;
        this.expectedLength = this.buffer.readUInt32BE(0);
        this.buffer = this.buffer.subarray(HEADER_SIZE);

        if (this.expectedLength > MAX_MESSAGE_SIZE) {
          this.emit('error', new Error(`Message size ${this.expectedLength} exceeds max ${MAX_MESSAGE_SIZE}`));
          this.expectedLength = null;
          this.buffer = Buffer.alloc(0);
          return;
        }
      }

      if (this.buffer.length < this.expectedLength) return;

      const payload = this.buffer.subarray(0, this.expectedLength);
      this.buffer = this.buffer.subarray(this.expectedLength);
      this.expectedLength = null;

      try {
        const msg = JSON.parse(payload.toString('utf-8')) as IpcMessage;
        this.emit('message', msg);
      } catch {
        this.emit('error', new Error('IPC message malformed JSON'));
      }
    }
  }
}
