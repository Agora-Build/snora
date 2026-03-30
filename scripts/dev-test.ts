#!/usr/bin/env tsx
// Quick smoke test: connect → init → 5 state updates → shutdown.
// Verifies engine IPC works. Does not require joining the Agora channel.

import { connect } from 'node:net';
import { encodeMessage, MessageDecoder } from '../src/shared/ipc-protocol.js';

const SOCKET_PATH = process.argv[2] || '/tmp/snora-dev-test.sock';
const APP_ID = process.argv[3] || '';
const TOKEN = process.argv[4] || '';
const CHANNEL = process.argv[5] || 'snora-test';

if (!TOKEN) {
  console.error('Usage: tsx scripts/dev-test.ts <socket> <app_id> <token> <channel>');
  process.exit(1);
}

const decoder = new MessageDecoder();
const socket = connect(SOCKET_PATH, () => {
  console.log('[client] Connected to engine');
  console.log('[client] Sending init...');
  socket.write(encodeMessage({
    type: 'init',
    data: {
      app_id: APP_ID,
      token: TOKEN,
      channel: CHANNEL,
      preferences: { soundscape: 'ocean', binaural_beats: true, volume: 0.7 },
      assets_path: '',
    },
  }));
});

socket.on('data', (data: Buffer) => decoder.feed(data));

decoder.on('message', (msg: { type: string; data?: Record<string, unknown> }) => {
  console.log(`[engine] ${msg.type}:`, JSON.stringify(msg.data || {}));

  if (msg.type === 'status' && msg.data?.reason === 'running') {
    console.log('[client] Engine is running! Sending state updates...');

    let count = 0;
    const interval = setInterval(() => {
      count++;
      const stress = Math.max(0.1, 0.8 - count * 0.15);
      const mood = stress > 0.5 ? 'anxious' : stress > 0.3 ? 'neutral' : 'calm';
      console.log(`[client] Update #${count}: mood=${mood} stress=${stress.toFixed(2)}`);
      socket.write(encodeMessage({
        type: 'state_update',
        data: {
          mood,
          heart_rate: Math.round(90 - count * 5),
          hrv: Math.round(25 + count * 6),
          respiration_rate: 20 - count * 2,
          stress_level: parseFloat(stress.toFixed(2)),
        },
      }));

      if (count >= 5) {
        clearInterval(interval);
        console.log('[client] Waiting 2s then sending shutdown...');
        setTimeout(() => {
          console.log('[client] Sending shutdown...');
          socket.write(encodeMessage({ type: 'shutdown' }));
        }, 2000);
      }
    }, 1000);
  }
});

socket.on('close', () => {
  console.log('[client] Socket closed');
  process.exit(0);
});

socket.on('error', (err) => {
  console.error('[client] Error:', err.message);
  process.exit(1);
});

setTimeout(() => {
  console.error('[client] Timeout — force exit');
  process.exit(1);
}, 30000);
