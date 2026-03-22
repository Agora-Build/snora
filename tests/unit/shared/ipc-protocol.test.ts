import { describe, it, expect } from 'vitest';
import { encodeMessage, MessageDecoder } from '../../../src/shared/ipc-protocol.js';
import type { IpcMessage } from '../../../src/shared/types.js';

describe('encodeMessage', () => {
  it('produces 4-byte length prefix + JSON payload', () => {
    const msg: IpcMessage = { type: 'ack' };
    const buf = encodeMessage(msg);
    const payloadLen = buf.readUInt32BE(0);
    const payload = buf.subarray(4).toString('utf-8');
    expect(payloadLen).toBe(Buffer.byteLength(JSON.stringify(msg), 'utf-8'));
    expect(JSON.parse(payload)).toEqual(msg);
  });

  it('rejects messages exceeding 64KB', () => {
    const msg: IpcMessage = { type: 'state_update', data: { big: 'x'.repeat(70000) } };
    expect(() => encodeMessage(msg)).toThrow('exceeds max');
  });
});

describe('MessageDecoder', () => {
  it('decodes a complete message from a single buffer', () => {
    const decoder = new MessageDecoder();
    const msg: IpcMessage = { type: 'state_update', data: { heart_rate: 68 } };
    const encoded = encodeMessage(msg);

    const messages: IpcMessage[] = [];
    decoder.on('message', (m) => messages.push(m));
    decoder.feed(encoded);

    expect(messages).toHaveLength(1);
    expect(messages[0]).toEqual(msg);
  });

  it('handles fragmented data across multiple feeds', () => {
    const decoder = new MessageDecoder();
    const msg: IpcMessage = { type: 'ack' };
    const encoded = encodeMessage(msg);

    const messages: IpcMessage[] = [];
    decoder.on('message', (m) => messages.push(m));

    for (let i = 0; i < encoded.length; i++) {
      decoder.feed(encoded.subarray(i, i + 1));
    }

    expect(messages).toHaveLength(1);
    expect(messages[0]).toEqual(msg);
  });

  it('handles multiple messages in one buffer', () => {
    const decoder = new MessageDecoder();
    const msg1: IpcMessage = { type: 'ack' };
    const msg2: IpcMessage = { type: 'status', data: { reason: 'running' } };
    const combined = Buffer.concat([encodeMessage(msg1), encodeMessage(msg2)]);

    const messages: IpcMessage[] = [];
    decoder.on('message', (m) => messages.push(m));
    decoder.feed(combined);

    expect(messages).toHaveLength(2);
    expect(messages[0]).toEqual(msg1);
    expect(messages[1]).toEqual(msg2);
  });

  it('emits error on malformed JSON', () => {
    const decoder = new MessageDecoder();
    const badPayload = Buffer.from('not json');
    const header = Buffer.alloc(4);
    header.writeUInt32BE(badPayload.length, 0);
    const frame = Buffer.concat([header, badPayload]);

    const errors: Error[] = [];
    decoder.on('error', (e) => errors.push(e));
    decoder.feed(frame);

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toContain('malformed');
  });

  it('emits error if payload exceeds 64KB', () => {
    const decoder = new MessageDecoder();
    const header = Buffer.alloc(4);
    header.writeUInt32BE(70000, 0);

    const errors: Error[] = [];
    decoder.on('error', (e) => errors.push(e));
    decoder.feed(header);

    expect(errors).toHaveLength(1);
    expect(errors[0].message).toContain('exceeds max');
  });
});
