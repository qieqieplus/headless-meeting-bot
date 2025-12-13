import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { Manifest, Participant, AudioTrack, Segment } from '../types/manifest'
import { createAudioHandle } from '../utils/audioDecoder'

type ResolveFn = (path: string) => string

/**
 * Simplified audio mixer for segmented audio.
 * 
 * Since the new API provides segments (multiple audio files per track),
 * we use a simple approach: play only the "mixed" track (if available),
 * treating each segment file as a complete, self-contained audio stream.
 * 
 */

export interface AudioMixerTrackState {
  id: string
  label: string
  participantId?: string
  kind: 'mixed' | 'user'
  volume: number
  isMuted: boolean
  isSolo: boolean
}

export interface AudioMixerState {
  ready: boolean
  enabled: boolean
  error?: string | null
  masterVolume: number
  tracks: AudioMixerTrackState[]
  soloedTrackId: string | null
  clockMediaTimeMs: number
  resumeAudio: () => Promise<void>
  setTrackVolume: (id: string, volume: number) => void
  toggleMute: (id: string) => void
  soloTrack: (id: string | null) => void
  setMasterVolume: (value: number) => void
  seekToMediaTime: (mediaTimeMs: number) => void
}

interface TrackInfo {
  id: string
  label: string
  participantId?: string
  kind: 'mixed' | 'user'
  url: string
  format: 'mp3' | 'aac' | 'm4a' | 'wav' | 'm3u8'
  segments: Segment[] // Store segments for this track
  activeSegmentIndex: number // Index of the currently active segment
}

interface InternalTrack {
  info: TrackInfo
  audioEl: HTMLAudioElement
  sourceNode: MediaElementAudioSourceNode
  gainNode: GainNode
  destroy: () => void
  state: AudioMixerTrackState
  timeUpdateHandler?: () => void
}

const inferFormat = (path: string): 'mp3' | 'aac' | 'm4a' | 'wav' | 'm3u8' => {
  const lower = path.toLowerCase()
  if (lower.endsWith('.mp3')) return 'mp3'
  if (lower.endsWith('.aac')) return 'aac'
  if (lower.endsWith('.m4a')) return 'm4a'
  if (lower.endsWith('.m3u8')) return 'm3u8'
  return 'wav'
}

/**
 * Finds the active segment for an audio track at the given media time.
 */
/**
 * Finds the active audio segment for a track at the given media time.
 * When end_ms = 0 (full length), uses getSegmentDuration if available to calculate effective end time.
 */
const findActiveAudioSegment = (
  track: AudioTrack, 
  mediaTimeMs: number,
  getSegmentDuration?: (segmentPath: string) => number | undefined
): number => {
  for (let i = 0; i < track.segments.length; i++) {
    const seg = track.segments[i]
    const segStart = seg.start_ms
    let segEnd: number
    if (seg.end_ms === 0) {
      // Full length segment: use audio duration if available, otherwise Infinity
      const durationMs = getSegmentDuration?.(seg.path)
      if (durationMs !== undefined && durationMs > 0) {
        segEnd = segStart + durationMs
      } else {
        segEnd = Infinity
      }
    } else {
      segEnd = seg.end_ms
    }
    if (mediaTimeMs >= segStart && mediaTimeMs < segEnd) {
      return i
    }
  }
  // If no active segment found, return the first segment (or -1 if no segments)
  return track.segments.length > 0 ? 0 : -1
}

const extractTracksFromManifest = (
  manifest: Manifest | null,
  resolveAssetUrl: ResolveFn,
  mediaTimeMs: number = 0,
  getSegmentDuration?: (segmentPath: string) => number | undefined,
): TrackInfo[] => {
  if (!manifest) return []

  const participantsById = new Map<string, Participant>(
    manifest.participants?.map((p) => [p.id, p]) ?? [],
  )

  return manifest.audio.map((track: AudioTrack, index: number) => {
    // Generate ID if not provided: use type + user_id or fallback to index
    const trackId = track.id ?? `${track.type}-${track.user_id ?? index}`
    const participantIdStr = track.user_id?.toString()
    const participant = participantIdStr ? participantsById.get(participantIdStr) : null
    const label =
      track.type === 'mixed'
        ? `${manifest.meeting_id} (Mixed)`
        : participant?.name ?? `User ${track.user_id ?? trackId}`

    // Find the active segment at the current media time
    const activeSegmentIndex = findActiveAudioSegment(track, mediaTimeMs, getSegmentDuration)
    const activeSegment = activeSegmentIndex >= 0 ? track.segments[activeSegmentIndex] : null
    const path = activeSegment?.path ?? (track.segments.length > 0 ? track.segments[0].path : '')

    return {
      id: trackId,
      label,
      participantId: participantIdStr,
      kind: track.type === 'mixed' ? 'mixed' : 'user',
      url: resolveAssetUrl(path),
      format: inferFormat(path),
      segments: track.segments,
      activeSegmentIndex,
    }
  })
}

export interface UseAudioMixerOptions {
  manifest: Manifest | null
  resolveAssetUrl: ResolveFn
  isPlaying: boolean
  playbackRate: number
}

export const useAudioMixer = ({
  manifest,
  resolveAssetUrl,
  isPlaying,
  playbackRate,
}: UseAudioMixerOptions): AudioMixerState => {
  const contextRef = useRef<AudioContext | null>(null)
  const masterGainRef = useRef<GainNode | null>(null)
  const tracksRef = useRef<Map<string, InternalTrack>>(new Map())
  const clockTimeRef = useRef(0) // Authoritative clock time in ms

  const [masterVolume, setMasterVolumeState] = useState(0.85)
  const [ready, setReady] = useState(false)
  const [enabled, setEnabled] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [tracksSnapshot, setTracksSnapshot] = useState<AudioMixerTrackState[]>([])
  const [soloedTrackId, setSoloedTrackId] = useState<string | null>(null)
  const [clockMediaTimeMs, setClockMediaTimeMs] = useState(0)
  const mediaTimeRef = useRef(0) // Track current media time for segment selection

  const ensureContext = useCallback(() => {
    if (!contextRef.current) {
      const ctx = new AudioContext()
      ctx.addEventListener('statechange', () => {
        setEnabled(ctx.state === 'running')
      })
      contextRef.current = ctx
    }
    return contextRef.current
  }, [])

  const ensureMasterGain = useCallback(
    (ctx: AudioContext) => {
      if (!masterGainRef.current) {
        const gain = ctx.createGain()
        gain.gain.value = masterVolume
        gain.connect(ctx.destination)
        masterGainRef.current = gain
      }
      return masterGainRef.current
    },
    [masterVolume],
  )

  const publishTracks = useCallback(() => {
    setTracksSnapshot(Array.from(tracksRef.current.values()).map((t) => ({ ...t.state })))
    setReady(Array.from(tracksRef.current.values()).some((t) => t.audioEl.readyState >= 2))
  }, [])

  const syncGain = useCallback(
    (track: InternalTrack, solo: string | null) => {
      const isSolo = solo === track.info.id
      track.state.isSolo = isSolo
      const shouldPlay = !track.state.isMuted && (!solo || isSolo)
      track.gainNode.gain.value = shouldPlay ? track.state.volume : 0
    },
    [],
  )

  // Setup tracks based on manifest
  useEffect(() => {
    if (!manifest) {
      // Clean up all tracks
      tracksRef.current.forEach((track) => {
        track.audioEl.pause()
        track.destroy()
      })
      tracksRef.current.clear()
      publishTracks()
      setError(null)
      return
    }

    const ctx = ensureContext()
    const masterGain = ensureMasterGain(ctx)
    setError(null)

    // Create function to get segment duration from loaded audio elements
    const getSegmentDuration = (segmentPath: string): number | undefined => {
      // Find the track that has this segment path
      for (const track of tracksRef.current.values()) {
        const segment = track.info.segments.find((seg) => seg.path === segmentPath)
        if (segment) {
          const duration = track.audioEl.duration
          if (Number.isFinite(duration) && duration > 0) {
            return duration * 1000 // Convert to milliseconds
          }
          return undefined
        }
      }
      return undefined
    }

    const trackInfos = extractTracksFromManifest(manifest, resolveAssetUrl, mediaTimeRef.current, getSegmentDuration)
    const trackIds = trackInfos.map((t) => t.id)

    // Remove old tracks
    tracksRef.current.forEach((track, id) => {
      if (!trackIds.includes(id)) {
        track.audioEl.pause()
        // Remove event listeners before destroying
        if (track.timeUpdateHandler) {
          track.audioEl.removeEventListener('timeupdate', track.timeUpdateHandler)
        }
        track.destroy()
        tracksRef.current.delete(id)
      }
    })

    // Add new tracks
    trackInfos.forEach((info) => {
      if (tracksRef.current.has(info.id)) return

      const handle = createAudioHandle(info.url, info.format, (msg) => setError(msg))
      const sourceNode = ctx.createMediaElementSource(handle.element)
      const gainNode = ctx.createGain()
      gainNode.gain.value = 0
      sourceNode.connect(gainNode).connect(masterGain)

      const track: InternalTrack = {
        info,
        audioEl: handle.element,
        sourceNode,
        gainNode,
        destroy: handle.destroy,
        state: {
          id: info.id,
          label: info.label,
          participantId: info.participantId,
          kind: info.kind,
          volume: 1,
          isMuted: info.kind !== 'mixed',
          isSolo: false,
        },
      }

      // Track time from mixed track
      if (info.kind === 'mixed') {
        const timeUpdateHandler = () => {
          const reportedTimeMs = track.audioEl.currentTime * 1000
          const currentClockTime = clockTimeRef.current
          
          // Only accept timeupdate if it's close to our clock (within 500ms)
          // This prevents stale updates after seeks while allowing normal playback updates
          const timeDiff = Math.abs(reportedTimeMs - currentClockTime)
          if (timeDiff < 500) {
            clockTimeRef.current = reportedTimeMs
            setClockMediaTimeMs(reportedTimeMs)
          }
        }
        track.timeUpdateHandler = timeUpdateHandler
        track.audioEl.addEventListener('timeupdate', timeUpdateHandler)
      }

      track.audioEl.addEventListener('canplay', () => {
        // Initialize clock from mixed track when it's ready
        if (info.kind === 'mixed' && track.audioEl.currentTime > 0) {
          const initialTimeMs = track.audioEl.currentTime * 1000
          clockTimeRef.current = initialTimeMs
          setClockMediaTimeMs(initialTimeMs)
        }
        publishTracks()
      })
      track.audioEl.addEventListener('error', () =>
        setError(`Failed to load: ${info.label}`),
      )

      tracksRef.current.set(info.id, track)
      syncGain(track, soloedTrackId)
    })

    publishTracks()
  }, [manifest, resolveAssetUrl, ensureContext, ensureMasterGain, publishTracks, syncGain, soloedTrackId])

  // Update media time ref when clock changes
  useEffect(() => {
    mediaTimeRef.current = clockMediaTimeMs
  }, [clockMediaTimeMs])

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      tracksRef.current.forEach((track) => {
        track.audioEl.pause()
        track.destroy()
      })
      tracksRef.current.clear()
      masterGainRef.current?.disconnect()
      contextRef.current?.close()
    }
  }, [])

  // Sync master volume
  useEffect(() => {
    if (masterGainRef.current) {
      masterGainRef.current.gain.value = masterVolume
    }
  }, [masterVolume])

  // Resume audio
  const resumeAudio = useCallback(async () => {
    const ctx = ensureContext()
    await ctx.resume()
    setEnabled(ctx.state === 'running')
    await Promise.all(
      Array.from(tracksRef.current.values()).map((t) =>
        t.audioEl.play().catch(() => undefined),
      ),
    )
  }, [ensureContext])

  // Play/pause based on isPlaying
  useEffect(() => {
    tracksRef.current.forEach((track) => {
      if (!enabled) {
        track.audioEl.pause()
        return
      }
      if (isPlaying) {
        void track.audioEl.play().catch(() => undefined)
      } else {
        track.audioEl.pause()
      }
    })
  }, [isPlaying, enabled])

  // Playback rate
  useEffect(() => {
    tracksRef.current.forEach((track) => {
      track.audioEl.playbackRate = playbackRate
    })
  }, [playbackRate])

  const setTrackVolume = useCallback(
    (id: string, volume: number) => {
      const track = tracksRef.current.get(id)
      if (!track) return
      track.state.volume = volume
      syncGain(track, soloedTrackId)
      publishTracks()
    },
    [publishTracks, syncGain, soloedTrackId],
  )

  const toggleMute = useCallback(
    (id: string) => {
      const track = tracksRef.current.get(id)
      if (!track) return
      track.state.isMuted = !track.state.isMuted
      syncGain(track, soloedTrackId)
      publishTracks()
    },
    [publishTracks, syncGain, soloedTrackId],
  )

  const soloTrack = useCallback(
    (id: string | null) => {
      const newSolo = soloedTrackId === id ? null : id
      setSoloedTrackId(newSolo)
      tracksRef.current.forEach((track) => {
        syncGain(track, newSolo)
      })
      publishTracks()
    },
    [publishTracks, soloedTrackId, syncGain],
  )

  const setMasterVolume = useCallback((value: number) => {
    setMasterVolumeState(value)
  }, [])

  const seekToMediaTime = useCallback((mediaTimeMs: number) => {
    // Update authoritative clock immediately
    clockTimeRef.current = mediaTimeMs
    mediaTimeRef.current = mediaTimeMs
    setClockMediaTimeMs(mediaTimeMs)
    
    // Create function to get segment duration from loaded audio elements
    const getSegmentDuration = (segmentPath: string): number | undefined => {
      for (const track of tracksRef.current.values()) {
        const segment = track.info.segments.find((seg) => seg.path === segmentPath)
        if (segment) {
          const duration = track.audioEl.duration
          if (Number.isFinite(duration) && duration > 0) {
            return duration * 1000 // Convert to milliseconds
          }
          return undefined
        }
      }
      return undefined
    }
    
    // For each track, check if we need to switch segments
    if (manifest) {
      tracksRef.current.forEach((track) => {
        const audioTrack = manifest.audio.find((t) => {
          const trackId = t.id ?? `${t.type}-${t.user_id ?? 'unknown'}`
          return trackId === track.info.id
        })
        
        if (audioTrack) {
          const newActiveIndex = findActiveAudioSegment(audioTrack, mediaTimeMs, getSegmentDuration)
          const currentSegment = audioTrack.segments[newActiveIndex]
          
          // If segment changed, we'd need to reload the audio source
          // For now, just seek within the current segment
          // TODO: Implement dynamic segment switching for full multi-segment support
          if (currentSegment) {
            // Calculate offset within the segment
            const segmentOffset = Math.max(0, mediaTimeMs - currentSegment.start_ms)
            track.audioEl.currentTime = segmentOffset / 1000
          } else {
            track.audioEl.currentTime = mediaTimeMs / 1000
          }
        } else {
          track.audioEl.currentTime = mediaTimeMs / 1000
        }
      })
    } else {
      // Fallback: seek all tracks directly
      tracksRef.current.forEach((track) => {
        track.audioEl.currentTime = mediaTimeMs / 1000
      })
    }
  }, [manifest])

  return useMemo(
    () => ({
      ready,
      enabled,
      error,
      masterVolume,
      tracks: tracksSnapshot,
      soloedTrackId,
      clockMediaTimeMs,
      resumeAudio,
      setTrackVolume,
      toggleMute,
      soloTrack,
      setMasterVolume,
      seekToMediaTime,
    }),
    [
      ready,
      enabled,
      error,
      masterVolume,
      tracksSnapshot,
      soloedTrackId,
      clockMediaTimeMs,
      resumeAudio,
      setTrackVolume,
      toggleMute,
      soloTrack,
      setMasterVolume,
      seekToMediaTime,
    ],
  )
}

