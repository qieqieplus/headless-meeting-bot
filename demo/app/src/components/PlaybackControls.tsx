import { useMemo } from 'react'
import type { FormEvent } from 'react'
import { formatClockTime } from '../utils/timeSync'

interface PlaybackControlsProps {
  isReady: boolean
  isPlaying: boolean
  isBuffering: boolean
  currentTime: number
  duration: number
  playbackRate: number
  onPlay: () => void | Promise<void>
  onPause: () => void | Promise<void>
  onSeek: (seconds: number) => void
  onRateChange: (rate: number) => void
  manifestUrl: string
  manifestError?: string | null
  manifestLoading: boolean
  onManifestUrlChange: (value: string) => void
  onManifestLoad: () => void
}

const rateOptions = [0.75, 1, 1.25, 1.5, 2]

export const PlaybackControls = ({
  isReady,
  isPlaying,
  isBuffering,
  currentTime,
  duration,
  playbackRate,
  onPlay,
  onPause,
  onSeek,
  onRateChange,
  manifestUrl,
  manifestError,
  manifestLoading,
  onManifestUrlChange,
  onManifestLoad,
}: PlaybackControlsProps) => {
  const progress = useMemo(
    () => (duration > 0 ? Math.min(100, (currentTime / duration) * 100) : 0),
    [currentTime, duration],
  )

  const onSubmit = (event: FormEvent) => {
    event.preventDefault()
    onManifestLoad()
  }

  const playLabel = isPlaying ? 'Pause' : 'Play'

  return (
    <div className="rounded-3xl border border-white/5 bg-slate-950/70 p-5 shadow-panel">
      <form className="flex flex-col gap-3 lg:flex-row lg:items-center" onSubmit={onSubmit}>
        <div className="flex-1">
          <label className="text-xs uppercase tracking-wide text-slate-400">
            Manifest URL
            <input
              value={manifestUrl}
              onChange={(event) => onManifestUrlChange(event.target.value)}
              className="mt-1 w-full rounded-2xl border border-white/10 bg-white/5 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400/70"
              placeholder="/recordings/123456789_20250118_120000/manifest.json"
            />
          </label>
        </div>
        <button
          type="submit"
          className="self-end rounded-2xl bg-cyan-500/90 px-4 py-2 text-sm font-semibold text-cyan-950 shadow-lg shadow-cyan-500/30 transition hover:bg-cyan-400/90"
          disabled={manifestLoading}
        >
          {manifestLoading ? 'Loading…' : 'Load manifest'}
        </button>
      </form>

      {manifestError && (
        <p className="mt-2 text-sm text-rose-300">
          Failed to load manifest: <span className="font-mono">{manifestError}</span>
        </p>
      )}

      <div className="mt-4 flex flex-col gap-4 lg:flex-row lg:items-center">
        <div className="flex items-center gap-3">
          <button
            type="button"
            onClick={isPlaying ? onPause : onPlay}
            disabled={!isReady}
            className="inline-flex h-12 w-12 items-center justify-center rounded-2xl bg-white text-slate-900 shadow-lg transition disabled:opacity-50"
          >
            {isPlaying ? '❚❚' : '▶'}
          </button>
          <div>
            <p className="text-2xl font-semibold text-white">{formatClockTime(currentTime)}</p>
            <p className="text-xs text-slate-400">{formatClockTime(duration)}</p>
          </div>
        </div>

        <div className="flex-1">
          <div className="flex items-center gap-3 text-xs text-slate-400">
            <span>{isBuffering ? 'Buffering…' : playLabel}</span>
            <div className="h-px flex-1 bg-gradient-to-r from-slate-700 via-white/60 to-slate-800" />
          </div>
          <input
            type="range"
            min={0}
            max={duration || 0}
            step={0.1}
            value={isFinite(currentTime) ? currentTime : 0}
            onChange={(event) => onSeek(Number(event.target.value))}
            className="mt-2 w-full accent-cyan-400"
            disabled={!isReady}
          />
          <div className="mt-2 flex items-center justify-between text-[11px] uppercase tracking-wide text-slate-500">
            <span>{progress.toFixed(1)}%</span>
            <span>space/k to toggle, j/l to seek ±5s</span>
          </div>
        </div>

        <div className="flex items-center gap-2">
          {rateOptions.map((rate) => (
            <button
              key={rate}
              type="button"
              onClick={() => onRateChange(rate)}
              className={`rounded-2xl px-3 py-2 text-sm font-semibold ${
                playbackRate === rate
                  ? 'bg-white text-slate-900'
                  : 'bg-white/10 text-white hover:bg-white/20'
              }`}
            >
              {rate}x
            </button>
          ))}
        </div>
      </div>
    </div>
  )
}

