import type { RefObject } from 'react'
import type { Manifest, TimelineEvent } from '../types/manifest'
import { formatClockTime } from '../utils/timeSync'

interface VideoPlayerProps {
  videoRef: RefObject<HTMLVideoElement | null>
  manifest: Manifest | null
  isReady: boolean
  isBuffering: boolean
  error?: string | null
  mediaTimeMs: number
  events: TimelineEvent[]
}

const statusBadge = (isReady: boolean, isBuffering: boolean) => {
  if (!isReady) return { label: 'Loading', color: 'bg-amber-500/80 text-amber-100' }
  if (isBuffering) return { label: 'Buffering', color: 'bg-cyan-500/80 text-cyan-900' }
  return { label: 'Live', color: 'bg-emerald-400/80 text-emerald-900' }
}

export const VideoPlayer = ({
  videoRef,
  manifest,
  isReady,
  isBuffering,
  error,
  mediaTimeMs,
  events,
}: VideoPlayerProps) => {
  const recentEvents = events
    .filter((evt) => {
      if (evt.mediaTimeMs > mediaTimeMs) return false
      // Hide meeting_status events except "ended"
      // Check both new format (event field) and legacy (status field)
      if (evt.type === 'meeting_status') {
        const eventValue = evt.event ?? evt.status
        if (eventValue !== 'ended') return false
      }
      return true
    })
    .slice(-4)
    .reverse()

  const status = statusBadge(isReady, isBuffering)

  return (
    <div className="relative overflow-hidden rounded-3xl border border-white/5 bg-slate-950/70 shadow-panel backdrop-blur">
      <video
        ref={videoRef}
        className="aspect-video w-full bg-black"
        playsInline
        controls={false}
        muted
      />

      <div className="pointer-events-none absolute left-5 top-5 space-y-1">
        <div className="inline-flex items-center gap-2 rounded-full bg-slate-900/70 px-4 py-1 text-xs font-semibold uppercase tracking-wide text-slate-200">
          <span
            className={`inline-flex h-2.5 w-2.5 items-center justify-center rounded-full ${isReady ? 'bg-emerald-400' : 'bg-amber-400'}`}
          />
          {manifest?.meeting_id ?? 'Replay'}
        </div>
        <div className={`inline-flex items-center gap-2 rounded-full px-3 py-1 text-xs ${status.color}`}>
          <span className="font-semibold">{status.label}</span>
          <span className="text-white/70">{formatClockTime(mediaTimeMs / 1000)}</span>
        </div>
      </div>

      {recentEvents.length > 0 && (
        <div className="pointer-events-none absolute top-5 right-5 w-52 space-y-2 rounded-2xl bg-slate-900/80 p-3 text-xs text-slate-200 shadow-lg">
          <p className="text-[11px] uppercase tracking-wide text-slate-400">Recent events</p>
          <ul className="space-y-1.5">
            {recentEvents.map((event) => (
              <li key={event.id} className="flex items-start gap-3">
                <span
                  className="mt-1 inline-block h-2.5 w-2.5 flex-shrink-0 rounded-full"
                  style={{ backgroundColor: event.color }}
                />
                <div>
                  <p className="font-semibold leading-tight">{event.displayLabel}</p>
                  <p className="text-[11px] text-slate-400">
                    {formatClockTime(event.mediaTimeMs / 1000)}
                  </p>
                </div>
              </li>
            ))}
          </ul>
        </div>
      )}

      {!videoRef.current?.src && (
        <div className="absolute inset-0 flex items-center justify-center bg-black text-sm font-medium text-slate-200">
          No video stream…
        </div>
      )}

      {error && (
        <div className="absolute inset-0 flex flex-col items-center justify-center bg-black/70 text-center text-rose-200">
          <p className="text-base font-semibold">Video error</p>
          <p className="text-sm text-rose-300/80">{error}</p>
        </div>
      )}
    </div>
  )
}

