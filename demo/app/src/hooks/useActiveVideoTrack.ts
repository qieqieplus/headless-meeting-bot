import { useMemo } from 'react'
import type { Manifest, VideoTrack, Segment } from '../types/manifest'

// Enriched video track with computed fields for player
export interface EnrichedVideoTrack extends VideoTrack {
  id: string
  m3u8: string  // Path to the active segment's m3u8 file
  offsetMs: number
  activeSegment: Segment | null // The segment that should be playing at current media time
}

interface ActiveVideoResult {
  track: EnrichedVideoTrack | null
}

/**
 * Finds the active segment for a track at the given media time.
 * When end_ms = 0 (full length), uses videoDurationMs if available to calculate effective end time.
 */
const findActiveSegment = (track: VideoTrack, mediaTimeMs: number, videoDurationMs?: number): Segment | null => {
  for (const seg of track.segments) {
    const segStart = seg.start_ms
    let segEnd: number
    if (seg.end_ms === 0) {
      // Full length segment: use video duration if available, otherwise Infinity
      if (videoDurationMs !== undefined && videoDurationMs > 0) {
        segEnd = segStart + videoDurationMs
      } else {
        segEnd = Infinity
      }
    } else {
      segEnd = seg.end_ms
    }
    if (mediaTimeMs >= segStart && mediaTimeMs < segEnd) {
      return seg
    }
  }
  return null
}

/**
 * Enriches a raw video track with computed fields needed by the player.
 */
const enrichVideoTrack = (
  track: VideoTrack, 
  index: number, 
  mediaTimeMs: number,
  videoDurationMs?: number
): EnrichedVideoTrack => {
  // Generate ID if not provided
  const trackId = track.id ?? `${track.type}-${track.user_id ?? index}`

  // Find the active segment at the current media time
  const activeSegment = findActiveSegment(track, mediaTimeMs, videoDurationMs)
  const m3u8Path = activeSegment?.path ?? (track.segments.length > 0 ? track.segments[0].path : '')
  const offsetMs = activeSegment?.start_ms ?? (track.segments.length > 0 ? track.segments[0].start_ms : 0)

  return {
    ...track,
    id: trackId,
    m3u8: m3u8Path,
    offsetMs,
    activeSegment,
  }
}

/**
 * Determines which video track should be active at the given media time.
 * Priority: share > user, then latest segment start time.
 * Returns null when no track is active (gap behavior: show black).
 * 
 * @param videoDurationMs Optional video duration in milliseconds. When provided and a segment
 * has end_ms = 0, uses start_ms + videoDurationMs as the effective end time.
 */
export const useActiveVideoTrack = (
  manifest: Manifest | null,
  mediaTimeMs: number,
  videoDurationMs?: number,
): ActiveVideoResult => {
  return useMemo(() => {
    if (!manifest || !manifest.video || manifest.video.length === 0) {
      return { track: null }
    }

    // Enrich all video tracks with active segment info
    const enrichedTracks = manifest.video.map((t, idx) => enrichVideoTrack(t, idx, mediaTimeMs, videoDurationMs))

    // Helper: check if track is active at mediaTimeMs based on segments
    const isTrackActive = (track: EnrichedVideoTrack): boolean => {
      return track.activeSegment !== null
    }

    // Helper: get the latest segment start time for a track at mediaTimeMs
    const getStartTime = (track: EnrichedVideoTrack): number => {
      return track.activeSegment?.start_ms ?? track.offsetMs
    }

    // Filter active tracks
    const activeTracks = enrichedTracks.filter(isTrackActive)

    if (activeTracks.length === 0) {
      return { track: null }
    }

    // Sort by priority: share > user, then by latest start time
    const sorted = activeTracks.sort((a, b) => {
      const priorityA = a.type === 'share' ? 1 : 0
      const priorityB = b.type === 'share' ? 1 : 0
      if (priorityA !== priorityB) {
        return priorityB - priorityA
      }
      return getStartTime(b) - getStartTime(a)
    })

    return { track: sorted[0] }
  }, [manifest, mediaTimeMs, videoDurationMs])
}

