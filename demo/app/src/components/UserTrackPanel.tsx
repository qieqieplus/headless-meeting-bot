import type { AudioMixerTrackState } from '../hooks/useAudioMixer'
import type { Participant, TimelineEvent } from '../types/manifest'
import { formatClockTime } from '../utils/timeSync'

interface UserTrackPanelProps {
  participants: Participant[]
  events: TimelineEvent[]
  mediaTimeMs: number
  tracks: AudioMixerTrackState[]
}

const ParticipantRow = ({
  participant,
  events,
  mediaTimeMs,
  track,
}: {
  participant: Participant
  events: TimelineEvent[]
  mediaTimeMs: number
  track?: AudioMixerTrackState
}) => {
  const initials =
    participant.name
      ?.split(' ')
      .map((chunk) => chunk.trim())
      .filter(Boolean)
      .map((chunk) => chunk[0])
      .join('')
      .slice(0, 2) ?? 'U'

  const lastEvent = [...events]
    .filter((event) => {
      if (event.userId !== participant.id || event.mediaTimeMs > mediaTimeMs) return false
      // Hide meeting_status events except "ended"
      if (event.type === 'meeting_status' && event.status !== 'ended') return false
      return true
    })
    .pop()

  const statusLabel = track?.isMuted
    ? 'Muted'
    : track?.isSolo
      ? 'Soloed'
      : lastEvent?.displayLabel ?? 'Idle'

  return (
    <li className="flex items-center gap-3 rounded-2xl border border-white/5 bg-slate-900/40 px-3 py-2 text-sm text-white">
      <div className="flex h-10 w-10 items-center justify-center rounded-2xl bg-cyan-400/10 text-base font-semibold text-cyan-200">
        {initials}
      </div>
      <div className="flex-1">
        <p className="font-semibold">{participant.name}</p>
        <p className="text-xs text-slate-400">{statusLabel}</p>
      </div>
      {lastEvent && (
        <span className="text-xs text-slate-400">
          {formatClockTime(lastEvent.mediaTimeMs / 1000)}
        </span>
      )}
    </li>
  )
}

export const UserTrackPanel = ({
  participants,
  events,
  mediaTimeMs,
  tracks,
}: UserTrackPanelProps) => {
  const participantTracks = new Map<string, AudioMixerTrackState>(
    tracks.filter((track) => track.participantId).map((track) => [track.participantId!, track]),
  )

  const currentEvents = events
    .filter((event) => {
      if (event.mediaTimeMs > mediaTimeMs) return false
      // Hide meeting_status events except "ended"
      if (event.type === 'meeting_status' && event.status !== 'ended') return false
      return true
    })
    .slice(-5)
    .reverse()

  return (
    <div className="rounded-3xl border border-white/5 bg-slate-950/70 p-5 shadow-panel">
      <p className="text-sm font-semibold text-white">Participants</p>
      <p className="text-xs text-slate-400">{participants.length} attendees</p>

      <ul className="mt-4 space-y-2">
        {participants.length === 0 && (
          <li className="text-sm text-slate-400">No participant metadata in manifest.</li>
        )}
        {participants.map((participant) => (
          <ParticipantRow
            key={participant.id}
            participant={participant}
            events={events}
            mediaTimeMs={mediaTimeMs}
            track={participantTracks.get(participant.id)}
          />
        ))}
      </ul>

      {currentEvents.length > 0 && (
        <div className="mt-4 rounded-2xl border border-white/5 bg-slate-900/40 p-3 text-xs text-white">
          <p className="mb-2 font-semibold text-slate-200">Recent activity</p>
          <ul className="space-y-1.5">
            {currentEvents.map((event) => (
              <li key={event.id} className="flex items-center justify-between">
                <span>{event.displayLabel}</span>
                <span className="text-slate-400">{formatClockTime(event.mediaTimeMs / 1000)}</span>
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  )
}

