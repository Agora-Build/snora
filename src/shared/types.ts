export const MOODS = ['anxious', 'stressed', 'neutral', 'calm', 'relaxed', 'sleepy'] as const;
export type Mood = typeof MOODS[number];

export const JOB_STATUSES = ['pending', 'starting', 'running', 'stopping', 'stopped', 'failed', 'client_disconnected'] as const;
export type JobStatus = typeof JOB_STATUSES[number];

export const TERMINAL_STATUSES: readonly JobStatus[] = ['stopped', 'failed', 'client_disconnected'] as const;

export interface PhysiologicalState {
  mood: Mood;
  heart_rate: number;
  hrv: number;
  respiration_rate: number;
  stress_level: number;
  timestamp?: number;
}

export interface SessionPreferences {
  soundscape: string;
  binaural_beats?: boolean;
  volume: number;
}

export interface CreateSessionRequest {
  user_id: string;
  agora: {
    token: string;
    channel: string;
  };
  initial_state: PhysiologicalState;
  preferences: SessionPreferences;
}

export interface Job {
  id: string;
  status: JobStatus;
  user_id: string;
  config: {
    agora: { token: string; channel: string };
    preferences: SessionPreferences;
  };
  worker_pid?: number;
  created_at: number;
  error?: string;
  stop_reason?: string;
  token_status?: string;
}

// IPC message types (Node.js <-> C++ engine)
export type IpcMessageType = 'init' | 'state_update' | 'shutdown' | 'ack' | 'token_update' | 'status';

export interface IpcMessage {
  type: IpcMessageType;
  data?: Record<string, unknown>;
}
