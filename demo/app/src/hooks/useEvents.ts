import { useEffect, useMemo, useState } from 'react'
import type { Manifest, ManifestEvent, TimelineEvent } from '../types/manifest'

const colorPalette = [
  '#38bdf8',
  '#f472b6',
  '#f97316',
  '#4ade80',
  '#c084fc',
  '#facc15',
  '#67e8f9',
  '#fb7185',
]

const formatFallbackLabel = (value: string) => {
  const normalized = value.replace(/_/g, ' ')
  return normalized.charAt(0).toUpperCase() + normalized.slice(1)
}

const SPEAKING_RESUME_MERGE_WINDOW_MS = 1500

const labelForEvent = (event: ManifestEvent) => {
  // Handle new format: meeting_status events use 'event' field
  if (event.type === 'meeting_status' && event.event) {
    return formatFallbackLabel(event.event)
  }
  // Legacy: check status field
  if (event.status) return formatFallbackLabel(event.status)
  // Handle user_event or other events with 'event' field
  if (event.event) {
    const userName = event.user?.name || (event.user_id ? `User ${event.user_id}` : 'User')
    const eventLabel = formatFallbackLabel(event.event)
    return `${userName}: ${eventLabel}`
  }
  if (event.message) return event.message
  return formatFallbackLabel(event.type)
}

const toTimelineEvent = (
  event: ManifestEvent,
  manifest: Manifest,
  index: number,
): TimelineEvent => {
  // New format: use timestamp (relative time) if available
  // Otherwise calculate from wall_ts or ts
  let relativeTime: number
  if (typeof event.timestamp === 'number') {
    relativeTime = Math.max(0, event.timestamp)
  } else {
    const eventTs = event.wall_ts ?? event.ts ?? manifest.t0_unix_ms
    relativeTime = Math.max(0, eventTs - manifest.t0_unix_ms)
  }
  
  const eventTs = event.wall_ts ?? (manifest.t0_unix_ms + relativeTime)
  const userId = event.user?.id ? String(event.user.id) : (event.user_id ? String(event.user_id) : undefined)

  return {
    ...event,
    id: `${event.type}-${userId ?? 'unknown'}-${index}`,
    ts: eventTs,
    userId,
    mediaTimeMs: relativeTime,
    displayLabel: labelForEvent(event),
    color: colorPalette[index % colorPalette.length],
  }
}

const mergeSpeakingEvents = (events: TimelineEvent[]): TimelineEvent[] => {
  if (events.length < 2) {
    return events
  }
  const merged: TimelineEvent[] = []
  let skipNextActive = false

  for (let i = 0; i < events.length; i += 1) {
    if (skipNextActive) {
      skipNextActive = false
      continue
    }

    const current = events[i]
    const next = events[i + 1]

    if (
      current.event === 'inactive_speaking' &&
      next &&
      next.event === 'active_speaking' &&
      current.userId &&
      current.userId === next.userId &&
      next.mediaTimeMs - current.mediaTimeMs <= SPEAKING_RESUME_MERGE_WINDOW_MS
    ) {
      skipNextActive = true
      continue
    }

    merged.push(current)
  }

  return merged
}

export const useEvents = (manifest: Manifest | null) => {
  const [eventsState, setEventsState] = useState<{ key: string | null; items: TimelineEvent[] }>({
    key: null,
    items: [],
  })
  const [loadingKey, setLoadingKey] = useState<string | null>(null)
  const [errorState, setErrorState] = useState<{ key: string | null; message: string | null }>({
    key: null,
    message: null,
  })

  const events = manifest?.events ?? []
  const requestKey = manifest ? `${manifest.meeting_id}:events` : null

  useEffect(() => {
    if (!manifest || events.length === 0) {
      setEventsState({ key: requestKey, items: [] })
      setLoadingKey(null)
      setErrorState({ key: requestKey, message: null })
      return
    }

    const timelineEvents = mergeSpeakingEvents(
      events
        .filter((evt) => 
          typeof evt.timestamp === 'number' || 
          typeof evt.wall_ts === 'number' || 
          typeof evt.ts === 'number'
        )
        .map((evt, index) => toTimelineEvent(evt, manifest, index))
        .sort((a, b) => a.mediaTimeMs - b.mediaTimeMs)
    )
    setEventsState({ key: requestKey, items: timelineEvents })
    setLoadingKey(null)
  }, [manifest, events, requestKey])

  const hasActiveKey = Boolean(requestKey)
  const loading = hasActiveKey ? loadingKey === requestKey : false
  const error = hasActiveKey && errorState.key === requestKey ? errorState.message : null
  const timelineEvents = eventsState.key === requestKey ? eventsState.items : []

  const eventTypes = useMemo(() => Array.from(new Set(timelineEvents.map((evt) => evt.type))).sort(), [timelineEvents])

  return { events: timelineEvents, loading, error, eventTypes }
}

