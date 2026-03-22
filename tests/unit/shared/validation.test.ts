import { describe, it, expect } from 'vitest';
import Ajv from 'ajv';
import { createSessionSchema, stateUpdateSchema } from '../../../src/shared/validation.js';

const ajv = new Ajv({ useDefaults: true });

describe('createSessionSchema', () => {
  const validate = ajv.compile(createSessionSchema);

  it('accepts valid request', () => {
    const valid = validate({
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(true);
  });

  it('rejects missing user_id', () => {
    const valid = validate({
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'anxious', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(false);
  });

  it('rejects invalid mood', () => {
    const valid = validate({
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'happy', heart_rate: 82, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(false);
  });

  it('rejects heart_rate out of range', () => {
    const valid = validate({
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'calm', heart_rate: 300, hrv: 35, respiration_rate: 18, stress_level: 0.7 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    });
    expect(valid).toBe(false);
  });

  it('defaults binaural_beats to true', () => {
    const data = {
      user_id: 'user-123',
      agora: { token: 'xxx', channel: 'ch-1' },
      initial_state: { mood: 'calm', heart_rate: 70, hrv: 50, respiration_rate: 14, stress_level: 0.3 },
      preferences: { soundscape: 'rain', volume: 0.7 },
    };
    validate(data);
    expect((data as any).preferences.binaural_beats).toBe(true);
  });
});

describe('stateUpdateSchema', () => {
  const validate = ajv.compile(stateUpdateSchema);

  it('accepts valid state update', () => {
    const valid = validate({
      mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3,
    });
    expect(valid).toBe(true);
  });

  it('accepts optional timestamp', () => {
    const valid = validate({
      mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 0.3,
      timestamp: 1710000000,
    });
    expect(valid).toBe(true);
  });

  it('rejects stress_level > 1.0', () => {
    const valid = validate({
      mood: 'calm', heart_rate: 68, hrv: 48, respiration_rate: 14, stress_level: 1.5,
    });
    expect(valid).toBe(false);
  });
});
