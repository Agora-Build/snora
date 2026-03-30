#!/usr/bin/env tsx
// Live streaming test with distinct bio phases so you can HEAR the audio adapt.
// Cycles through: anxious → neutral → calm → sleepy, each lasting 15 seconds.
// Join the Agora channel to listen. Press Ctrl+C to stop.

import { connect } from 'node:net';
import { encodeMessage, MessageDecoder } from '../src/shared/ipc-protocol.js';

const SOCKET_PATH = process.argv[2] || '/tmp/snora-live.sock';
const APP_ID = process.argv[3] || '';
const TOKEN = process.argv[4] || '';
const CHANNEL = process.argv[5] || 'snora-test';
const SOUNDSCAPE = process.argv[6] || 'ocean';

if (!TOKEN || !APP_ID) {
  console.error('Usage: tsx scripts/stream-live.ts <socket> <app_id> <token> <channel> [soundscape]');
  console.error('Soundscapes: ocean (default), rain, wind');
  process.exit(1);
}

const decoder = new MessageDecoder();
const socket = connect(SOCKET_PATH, () => {
  console.log('[client] Connected to engine socket');
  console.log(`[client] Sending init: channel=${CHANNEL} soundscape=${SOUNDSCAPE}`);

  socket.write(encodeMessage({
    type: 'init',
    data: {
      app_id: APP_ID,
      token: TOKEN,
      channel: CHANNEL,
      preferences: {
        soundscape: SOUNDSCAPE,
        binaural_beats: true,
        volume: 0.8,
      },
      assets_path: '',
    },
  }));
});

socket.on('data', (data: Buffer) => decoder.feed(data));

// Bio phases — each runs for 15 seconds so you can clearly hear the difference
const phases = [
  {
    name: 'ANXIOUS (bright, fast breathing, alpha binaural)',
    mood: 'anxious', stress: 0.9, hr: 95, hrv: 20, resp: 22,
  },
  {
    name: 'STRESSED (darker, slowing down)',
    mood: 'stressed', stress: 0.7, hr: 85, hrv: 30, resp: 18,
  },
  {
    name: 'NEUTRAL (balanced tone, theta binaural)',
    mood: 'neutral', stress: 0.5, hr: 75, hrv: 40, resp: 15,
  },
  {
    name: 'CALM (brighter, slow breathing, delta binaural)',
    mood: 'calm', stress: 0.2, hr: 62, hrv: 55, resp: 10,
  },
  {
    name: 'SLEEPY (very bright, deep slow breathing)',
    mood: 'sleepy', stress: 0.05, hr: 55, hrv: 65, resp: 7,
  },
];

let streaming = false;

decoder.on('message', (msg: { type: string; data?: Record<string, unknown> }) => {
  const ts = new Date().toISOString().slice(11, 19);

  if (msg.type === 'status') {
    const reason = msg.data?.reason;
    if (reason === 'running' && !streaming) {
      streaming = true;
      console.log(`\n[${ts}] ENGINE IS STREAMING TO AGORA`);
      console.log(`Channel: ${CHANNEL} | Soundscape: ${SOUNDSCAPE}`);
      console.log('Join this channel to hear audio. Press Ctrl+C to stop.\n');
      console.log('Bio phases cycle every 15 seconds:');
      phases.forEach((p, i) => console.log(`  ${i + 1}. ${p.name}`));
      console.log('');
      startBioPhases();
    } else if (reason === 'subscriber_joined') {
      console.log(`[${ts}] Listener joined the channel`);
    } else if (reason === 'no_subscribers') {
      console.log(`[${ts}] No listeners in channel`);
    }
  }
});

function startBioPhases() {
  let phaseIndex = 0;

  function applyPhase() {
    const phase = phases[phaseIndex % phases.length];
    const ts = new Date().toISOString().slice(11, 19);
    console.log(`[${ts}] >>> Phase ${(phaseIndex % phases.length) + 1}: ${phase.name}`);

    socket.write(encodeMessage({
      type: 'state_update',
      data: {
        mood: phase.mood,
        heart_rate: phase.hr,
        hrv: phase.hrv,
        respiration_rate: phase.resp,
        stress_level: phase.stress,
      },
    }));

    phaseIndex++;
  }

  // Apply first phase immediately
  applyPhase();

  // Send state updates every 2 seconds (smoother transitions),
  // switch phase every 15 seconds
  let tickInPhase = 0;
  setInterval(() => {
    tickInPhase++;
    if (tickInPhase >= 7) { // 7 * 2s ≈ 15 seconds per phase
      tickInPhase = 0;
      applyPhase();
    } else {
      // Re-send current phase data (keeps connection alive)
      const phase = phases[(phaseIndex - 1) % phases.length];
      socket.write(encodeMessage({
        type: 'state_update',
        data: {
          mood: phase.mood,
          heart_rate: phase.hr,
          hrv: phase.hrv,
          respiration_rate: phase.resp,
          stress_level: phase.stress,
        },
      }));
    }
  }, 2000);
}

process.on('SIGINT', () => {
  console.log('\n[client] Sending shutdown...');
  socket.write(encodeMessage({ type: 'shutdown' }));
  setTimeout(() => process.exit(0), 1000);
});

socket.on('close', () => {
  console.log('[client] Disconnected');
  process.exit(0);
});
