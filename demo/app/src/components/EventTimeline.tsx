import { useMemo, useState } from 'react'
import type { MouseEvent } from 'react'
import type { TimelineEvent } from '../types/manifest'
import { clamp, formatClockTime } from '../utils/timeSync'

interface EventTimelineProps {
  durationMs: number
  mediaTimeMs: number
  events: TimelineEvent[]
  onSeek: (timeMs: number) => void
  loading: boolean
  error?: string | null
}

const eventTypesFrom = (events: TimelineEvent[]) =>
  Array.from(new Set(events.map((event) => event.type))).sort()

export const EventTimeline = ({
  durationMs,
  mediaTimeMs,
  events,
  onSeek,
  loading,
  error,
}: EventTimelineProps) => {
  const [hiddenTypes, setHiddenTypes] = useState<Set<string>>(new Set(['meeting_status']))
  const [hoverTime, setHoverTime] = useState<number | null>(null)

  const eventTypes = useMemo(() => eventTypesFrom(events), [events])
  const filteredEvents = events.filter((event) => {
    if (hiddenTypes.has(event.type)) {
      // Always show meeting_status events with "ended" status
      // Check both new format (event field) and legacy (status field)
      if (event.type === 'meeting_status') {
        const eventValue = event.event ?? event.status
        return eventValue === 'ended'
      }
      return false
    }
    return true
  })
  const progressPct = durationMs > 0 ? clamp((mediaTimeMs / durationMs) * 100, 0, 100) : 0

  const handleScrub = (event: MouseEvent<HTMLDivElement>) => {
    if (!durationMs) return
    const { left, width } = event.currentTarget.getBoundingClientRect()
    const ratio = clamp((event.clientX - left) / width, 0, 1)
    onSeek(durationMs * ratio)
  }

  const handleHover = (event: MouseEvent<HTMLDivElement>) => {
    if (!durationMs) return
    const { left, width } = event.currentTarget.getBoundingClientRect()
    const ratio = clamp((event.clientX - left) / width, 0, 1)
    setHoverTime(durationMs * ratio)
  }

  const clearHover = () => setHoverTime(null)

  const handleEventClick = (event: TimelineEvent) => {
    onSeek(event.mediaTimeMs)
  }

  return (
    <div className="rounded-3xl border border-white/5 bg-slate-950/60 p-5 shadow-panel">
      <div className="flex items-center justify-between">
        <div>
          <p className="text-sm font-semibold text-white">Event timeline</p>
          <p className="text-xs text-slate-400">
            {loading
              ? 'Loading events…'
              : error
                ? `Failed to load events (${error})`
                : `${events.length} events`}
          </p>
        </div>
        <div className="flex flex-wrap gap-2">
          {eventTypes.map((type) => {
            const isActive = !hiddenTypes.has(type)
            return (
              <button
                key={type}
                type="button"
                onClick={() => {
                  setHiddenTypes((current) => {
                    const next = new Set(current)
                    if (next.has(type)) next.delete(type)
                    else next.add(type)
                    return next
                  })
                }}
                className={`rounded-full px-3 py-1 text-xs font-semibold ${
                  isActive ? 'bg-white text-slate-900' : 'bg-white/10 text-white'
                }`}
              >
                {type}
              </button>
            )
          })}
        </div>
      </div>

      <div
        className="relative mt-4 h-16 cursor-pointer rounded-2xl bg-gradient-to-r from-slate-900/70 to-slate-800/70"
        onClick={handleScrub}
        onMouseMove={handleHover}
        onMouseLeave={clearHover}
      >
        <div
          className="absolute inset-y-0 left-0 rounded-2xl bg-cyan-400/40"
          style={{ width: `${progressPct}%` }}
        />
        {filteredEvents.map((event) => {
          const leftPct = durationMs > 0 ? clamp((event.mediaTimeMs / durationMs) * 100, 0, 100) : 0
          return (
            <button
              key={event.id}
              type="button"
              onClick={(e) => {
                e.stopPropagation()
                handleEventClick(event)
              }}
              className="absolute top-1/2 h-4 w-1 -translate-y-1/2 rounded-full bg-white shadow hover:h-6 hover:w-2 transition-all cursor-pointer"
              style={{ left: `calc(${leftPct}% - 1px)`, backgroundColor: event.color ?? '#22d3ee' }}
              title={event.displayLabel}
            />
          )
        })}

        <span
          className="absolute top-full mt-2 -translate-x-1/2 rounded-full bg-slate-800/90 px-3 py-1 text-xs text-white"
          style={{ left: `${progressPct}%` }}
        >
          {formatClockTime(mediaTimeMs / 1000)}
        </span>

        {hoverTime !== null && (
          <span
            className="pointer-events-none absolute -top-7 -translate-x-1/2 rounded-full bg-white/90 px-2 py-0.5 text-[10px] font-semibold text-slate-900"
            style={{ left: `${clamp((hoverTime / durationMs) * 100, 0, 100)}%` }}
          >
            {formatClockTime(hoverTime / 1000)}
          </span>
        )}
      </div>

      {filteredEvents.length > 0 && (
        <div className="mt-4 grid gap-3 md:grid-cols-2">
          {filteredEvents.slice(0, 6).map((event) => (
            <button
              key={event.id}
              type="button"
              onClick={() => handleEventClick(event)}
              className="rounded-2xl border border-white/5 bg-slate-900/60 px-3 py-2 text-sm text-slate-100 hover:bg-slate-800/80 hover:border-white/10 transition-colors cursor-pointer text-left"
            >
              <div className="flex items-center justify-between">
                <p className="font-semibold">{event.displayLabel}</p>
                <span className="text-xs text-slate-400">
                  {formatClockTime(event.mediaTimeMs / 1000)}
                </span>
              </div>
              {event.message && <p className="text-xs text-slate-400">{event.message}</p>}
            </button>
          ))}
        </div>
      )}
    </div>
  )
}

