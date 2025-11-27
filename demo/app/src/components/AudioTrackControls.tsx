import type { AudioMixerState, AudioMixerTrackState } from '../hooks/useAudioMixer'

interface AudioTrackControlsProps {
  mixer: AudioMixerState
}

const TrackRow = ({
  track,
  mixer,
}: {
  track: AudioMixerTrackState
  mixer: AudioMixerState
}) => {
  const { setTrackVolume, soloTrack, soloedTrackId } = mixer
  const isSelected = soloedTrackId === track.id
  const hasSelection = soloedTrackId !== null

  return (
    <div className={`rounded-2xl border p-3 text-sm ${isSelected ? 'border-emerald-400/40 bg-emerald-900/10' : 'border-white/5 bg-slate-900/30'}`}>
      <div className="flex items-center gap-3">
        <input
          type="radio"
          name="audio-track"
          checked={isSelected}
          onChange={() => soloTrack(track.id)}
          className="h-4 w-4 accent-emerald-500"
        />
        <div className="flex-1 min-w-0">
          <p className="font-semibold text-white truncate">{track.label}</p>
          <p className="text-xs text-slate-400">
            {track.kind === 'mixed' ? 'Mixed output' : 'Participant track'}
          </p>
        </div>
      </div>

      {(isSelected || !hasSelection) && (
        <div className="mt-3 flex items-center gap-3">
          <input
            type="range"
            min={0}
            max={1}
            step={0.01}
            value={track.volume}
            onChange={(event) => setTrackVolume(track.id, Number(event.target.value))}
            className="flex-1 accent-emerald-400"
          />
          <span className="w-12 text-right text-xs text-slate-300">
            {(track.volume * 100).toFixed(0)}%
          </span>
        </div>
      )}
    </div>
  )
}

export const AudioTrackControls = ({ mixer }: AudioTrackControlsProps) => {
  const { ready, enabled, error, masterVolume, tracks, setMasterVolume } = mixer

  return (
    <div className="rounded-3xl border border-white/5 bg-slate-950/60 p-5 shadow-panel">
      <div className="flex items-center justify-between">
        <div>
          <p className="text-sm font-semibold text-white">Audio mixer</p>
          <p className="text-xs text-slate-400">
            {ready ? (enabled ? 'Synced to video clock' : 'Requires user gesture') : 'Buffering…'}
          </p>
        </div>
      </div>

      <div className="mt-4">
        <label className="text-xs uppercase tracking-wide text-slate-400">
          Master volume ({(masterVolume * 100).toFixed(0)}%)
          <input
            type="range"
            min={0}
            max={1.2}
            step={0.01}
            value={masterVolume}
            onChange={(event) => setMasterVolume(Number(event.target.value))}
            className="mt-2 w-full accent-emerald-400"
          />
        </label>
      </div>

      <div className="mt-4 space-y-3">
        {tracks.length === 0 && (
          <p className="text-sm text-slate-400">No tracks discovered in manifest.</p>
        )}
        {tracks.map((track) => (
          <TrackRow key={track.id} track={track} mixer={mixer} />
        ))}
      </div>

      {error && <p className="mt-3 text-sm text-rose-300">{error}</p>}
    </div>
  )
}

