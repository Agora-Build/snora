#!/usr/bin/env tsx
// Connect to engine and keep it streaming indefinitely until Ctrl+C

import { connect } from 'node:net';
import { encodeMessage, MessageDecoder } from '../src/shared/ipc-protocol.js';

const SOCKET_PATH = process.argv[2] || '/tmp/snora-live.sock';
const APP_ID = process.argv[3] || '';
const TOKEN = process.argv[4] || '';
const CHANNEL = process.argv[5] || 'snora-test';

if (!TOKEN || !APP_ID) {
  console.error('Usage: tsx scripts/stream-live.ts <socket> <app_id> <token> <channel>');
  process.exit(1);
}

const decoder = new MessageDecoder();
const socket = connect(SOCKET_PATH, () => {
  console.log('[client] Connected to engine socket');
  console.log(`[client] Sending init: app=${APP_ID.slice(0, 8)}... channel=${CHANNEL}`);

  socket.write(encodeMessage({
    type: 'init',
    data: {
      app_id: APP_ID,
      token: TOKEN,
      channel: CHANNEL,
      preferences: {
        soundscape: 'ocean',
        binaural_beats: true,
        volume: 0.7,
      },
      assets_path: '',
    },
  }));
});

socket.on('data', (data: Buffer) => decoder.feed(data));

let streaming = false;

decoder.on('message', (msg: { type: string; data?: Record<string, unknown> }) => {
  const ts = new Date().toISOString().slice(11, 19);
  console.log(`[${ts}] engine ${msg.type}: ${JSON.stringify(msg.data || {})}`);

  if (msg.type === 'status' && msg.data?.reason === 'running' && !streaming) {
    streaming = true;
    console.log('\n=== ENGINE IS STREAMING TO AGORA ===');
    console.log(`Channel: ${CHANNEL}`);
    console.log('Join this channel with the 128 project to hear audio.');
    console.log('Press Ctrl+C to stop.\n');

    // Send periodic state updates to keep the audio interesting
    let tick = 0;
    setInterval(() => {
      tick++;
      const stress = 0.5 + 0.3 * Math.sin(tick * 0.1);
      const mood = stress > 0.6 ? 'anxious' : stress > 0.4 ? 'neutral' : 'calm';
      socket.write(encodeMessage({
        type: 'state_update',
        data: {
          mood,
          heart_rate: Math.round(70 + stress * 20),
          hrv: Math.round(40 - stress * 15),
          respiration_rate: 14 + stress * 6,
          stress_level: parseFloat(stress.toFixed(2)),
        },
      }));
    }, 2000);
  }
});

process.on('SIGINT', () => {
  console.log('\n[client] Sending shutdown...');
  socket.write(encodeMessage({ type: 'shutdown' }));
  setTimeout(() => process.exit(0), 1000);
});

socket.on('close', () => {
  console.log('[client] Disconnected');
  process.exit(0);
});
