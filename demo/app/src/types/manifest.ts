export interface Participant {
  id: string
  name: string
  role?: string
  avatarUrl?: string
}

export interface Segment {
  path: string
  start_ms: number
  end_ms: number
}

export interface VideoTrack {
  id?: string
  type: 'share' | 'user'
  user_id: number
  segments: Segment[]
}

export interface AudioFormat {
  sample_rate: number
  channels: number
  encoding: string
  bitrate?: number
}

export interface AudioTrack {
  id?: string
  type: 'mixed' | 'user' | 'share'
  user_id?: number
  segments: Segment[]
  format: AudioFormat
}

export interface Manifest {
  meeting_id: string
  t0_unix_ms: number
  duration_ms: number
  audio: AudioTrack[]
  video: VideoTrack[]
  events: Array<ManifestEvent>
  participants?: Participant[]
  metadata?: Record<string, unknown>
}

export interface ManifestEvent {
  type: 'meeting_status' | 'user_event' | string
  event?: string // For meeting_status: "connecting" | "in_meeting" | "ended" | "idle" | "unknown"
                 // For user_event: "snapshot" | "share_started" | "share_stopped" | "audio_unmuted" | "audio_muted" | etc.
  user_id?: string | number
  user?: {
    id: number
    name: string
    audio: number
    video: number
    share: number
  }
  timestamp?: number // Relative time in ms from t0_unix_ms
  ts?: number // Legacy: relative time
  wall_ts?: number // Absolute wall clock time in ms
  status?: string // Legacy: for meeting_status events
  message?: string
}

export interface TimelineEvent extends ManifestEvent {
  id: string
  userId?: string
  mediaTimeMs: number
  displayLabel: string
  color?: string
}

