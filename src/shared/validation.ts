export const physiologicalStateProperties = {
  mood: { type: 'string', enum: ['anxious', 'stressed', 'neutral', 'calm', 'relaxed', 'sleepy'] },
  heart_rate: { type: 'integer', minimum: 30, maximum: 220 },
  hrv: { type: 'integer', minimum: 5, maximum: 300 },
  respiration_rate: { type: 'number', minimum: 4, maximum: 40 },
  stress_level: { type: 'number', minimum: 0, maximum: 1 },
} as const;

export const createSessionSchema = {
  type: 'object',
  required: ['user_id', 'agora', 'initial_state', 'preferences'],
  additionalProperties: false,
  properties: {
    user_id: { type: 'string', minLength: 1, maxLength: 128, pattern: '^[a-zA-Z0-9_-]+$' },
    agora: {
      type: 'object',
      required: ['token', 'channel'],
      additionalProperties: false,
      properties: {
        token: { type: 'string', minLength: 1 },
        channel: { type: 'string', minLength: 1 },
      },
    },
    initial_state: {
      type: 'object',
      required: ['mood', 'heart_rate', 'hrv', 'respiration_rate', 'stress_level'],
      additionalProperties: false,
      properties: {
        ...physiologicalStateProperties,
        timestamp: { type: 'integer' },
      },
    },
    preferences: {
      type: 'object',
      required: ['soundscape', 'volume'],
      additionalProperties: false,
      properties: {
        soundscape: { type: 'string', minLength: 1 },
        binaural_beats: { type: 'boolean', default: true },
        volume: { type: 'number', minimum: 0, maximum: 1 },
      },
    },
  },
} as const;

export const stateUpdateSchema = {
  type: 'object',
  required: ['mood', 'heart_rate', 'hrv', 'respiration_rate', 'stress_level'],
  additionalProperties: false,
  properties: {
    ...physiologicalStateProperties,
    timestamp: { type: 'integer' },
  },
} as const;

export const tokenUpdateSchema = {
  type: 'object',
  required: ['token'],
  additionalProperties: false,
  properties: {
    token: { type: 'string', minLength: 1 },
  },
} as const;
