#!/usr/bin/env tsx
// Dev test: connect to engine via IPC, send init with real Agora token,
// send state updates, observe status messages.

import { connect } from 'node:net';
import { encodeMessage, MessageDecoder } from '../src/shared/ipc-protocol.js';

const SOCKET_PATH = process.argv[2] || '/tmp/snora-dev-test.sock';
const APP_ID = process.argv[3] || '2655d20a82fc47cebcff82d5bd5d53ef';
const TOKEN = process.argv[4] || '';
const CHANNEL = process.argv[5] || 'snora-test';

if (!TOKEN) {
  console.error('Usage: tsx scripts/dev-test.ts <socket> <app_id> <token> <channel>');
  process.exit(1);
}

const decoder = new MessageDecoder();
const socket = connect(SOCKET_PATH, () => {
  console.log('[client] Connected to engine');

  // Send init
  const initMsg = {
    type: 'init',
    data: {
      app_id: APP_ID,
      token: TOKEN,
      channel: CHANNEL,
      preferences: {
        soundscape: 'rain',
        binaural_beats: true,
        volume: 0.7,
      },
      assets_path: '',
    },
  };
  console.log('[client] Sending init...');
  socket.write(encodeMessage(initMsg));
});

socket.on('data', (data: Buffer) => decoder.feed(data));

decoder.on('message', (msg: { type: string; data?: Record<string, unknown> }) => {
  console.log(`[engine] ${msg.type}:`, JSON.stringify(msg.data || {}));

  if (msg.type === 'status' && msg.data?.reason === 'running') {
    console.log('[client] Engine is running! Sending state updates...');

    // Send a few state updates over 5 seconds
    let count = 0;
    const interval = setInterval(() => {
      count++;
      const stress = Math.max(0.1, 0.8 - count * 0.1);
      const mood = stress > 0.5 ? 'anxious' : stress > 0.3 ? 'neutral' : 'calm';
      const update = {
        type: 'state_update',
        data: {
          mood,
          heart_rate: Math.round(90 - count * 3),
          hrv: Math.round(25 + count * 4),
          respiration_rate: 20 - count * 1.5,
          stress_level: stress,
        },
      };
      console.log(`[client] State update #${count}: mood=${mood} stress=${stress.toFixed(1)}`);
      socket.write(encodeMessage(update));

      if (count >= 5) {
        clearInterval(interval);
        // Let it stream for 3 more seconds, then shutdown
        console.log('[client] Waiting 3s then sending shutdown...');
        setTimeout(() => {
          console.log('[client] Sending shutdown...');
          socket.write(encodeMessage({ type: 'shutdown' }));
        }, 3000);
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

// Timeout after 30 seconds
setTimeout(() => {
  console.error('[client] Timeout — force exit');
  process.exit(1);
}, 30000);
