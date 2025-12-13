import { useEffect, useMemo, useRef, useState } from 'react'
import { VideoPlayer } from './components/VideoPlayer'
import { PlaybackControls } from './components/PlaybackControls'
import { EventTimeline } from './components/EventTimeline'
import { AudioTrackControls } from './components/AudioTrackControls'
import { UserTrackPanel } from './components/UserTrackPanel'
import { useManifest } from './hooks/useManifest'
import { useVideoPlayer } from './hooks/useVideoPlayer'
import { useEvents } from './hooks/useEvents'
import { useAudioMixer } from './hooks/useAudioMixer'
import { useActiveVideoTrack } from './hooks/useActiveVideoTrack'
import { formatClockTime } from './utils/timeSync'

const initialManifestUrl =
  new URLSearchParams(window.location.search).get('manifest') ??
  '/recordings/video/manifest.json'

function App() {
  const manifestState = useManifest(initialManifestUrl)
  const { manifest, manifestUrl, setManifestUrl, loading: manifestLoading, error: manifestError, resolveAssetUrl } =
    manifestState
  const [manifestInput, setManifestInput] = useState(manifestUrl)
  const [isPlaying, setIsPlaying] = useState(false)
  const isPlayingRef = useRef(isPlaying)
  const lastTrackIdRef = useRef<string | null>(null)

  useEffect(() => {
    const params = new URLSearchParams(window.location.search)
    params.set('manifest', manifestUrl)
    const nextUrl = `${window.location.pathname}?${params.toString()}`
    window.history.replaceState({}, '', nextUrl)
  }, [manifestUrl])

  // Use state to track playback rate across both video and audio
  const [playbackRate, setPlaybackRate] = useState(1)

  // Keep an imperative reference to the latest playing state without
  // forcing the sync effect to re-run on every toggle.
  useEffect(() => {
    isPlayingRef.current = isPlaying
  }, [isPlaying])

  // Initialize audio mixer first (it provides the master clock)
  const audioMixer = useAudioMixer({
    manifest,
    resolveAssetUrl,
    isPlaying,
    playbackRate,
  })

  // Transport time: prefer audio clock, fallback to 0
  const transportTimeMs = audioMixer.clockMediaTimeMs
  const transportTimeRef = useRef(transportTimeMs)

  useEffect(() => {
    transportTimeRef.current = transportTimeMs
  }, [transportTimeMs])

  // Determine active video track based on transport time
  // Note: videoDurationMs is undefined initially, will be updated on re-render once video loads
  const activeVideo = useActiveVideoTrack(manifest, transportTimeMs, undefined)

  // Compute the video source URL based on active track
  const videoSource = activeVideo.track ? resolveAssetUrl(activeVideo.track.m3u8) : null
  const videoOffset = activeVideo.track?.offsetMs ?? 0

  const video = useVideoPlayer({
    sourceUrl: videoSource,
    offsetMs: videoOffset,
  })

  // Sync video playback rate with state
  useEffect(() => {
    video.setPlaybackRate(playbackRate)
  }, [playbackRate, video.setPlaybackRate])

  const eventsState = useEvents(manifest)

  const handleManifestLoad = () => {
    const trimmed = manifestInput.trim()
    if (!trimmed) {
      return
    }
    setManifestInput(trimmed)
    setManifestUrl(trimmed)
  }

  const handlePlay = async () => {
    await audioMixer.resumeAudio()
    setIsPlaying(true)
    await video.play()
  }

  const handlePause = async () => {
    setIsPlaying(false)
    await video.pause()
  }

  const handleSeek = (seconds: number) => {
    const timeMs = seconds * 1000
    audioMixer.seekToMediaTime(timeMs)
    video.seekToMediaTime(timeMs)
  }

  const handleSeekMediaTime = (timeMs: number) => {
    audioMixer.seekToMediaTime(timeMs)
    video.seekToMediaTime(timeMs)
  }

  // Align video with the mixed audio clock when the active video track changes
  useEffect(() => {
    const track = activeVideo.track
    if (!video.isReady || !track) {
      return
    }

    // Only react the first time we see this track id to avoid chasing the clock
    if (lastTrackIdRef.current === track.id) {
      return
    }
    lastTrackIdRef.current = track.id

    video.seekToMediaTime(transportTimeRef.current)
    if (isPlayingRef.current) {
      void video.play()
    }
  }, [activeVideo.track?.id, video.isReady])

  const durationMs = manifest?.duration_ms ?? video.duration * 1000


  const headerSubtitle = useMemo(() => {
    if (!manifest) return 'Load a manifest.json to begin replaying your meeting'
    const duration = formatClockTime(durationMs / 1000)
    const participantCount = manifest.participants?.length ?? 0
    return `${participantCount} participants · ${duration} · ${manifest.meeting_id}`
  }, [manifest, durationMs])

  useEffect(() => {
    const titleFragment = manifest?.meeting_id
    document.title = titleFragment ? `${titleFragment} · Meeting Replay` : 'Meeting Replay Demo'
  }, [manifest?.meeting_id])

  return (
    <div className="min-h-screen bg-night pb-10 text-white">
      <div className="mx-auto max-w-7xl space-y-6 px-5 py-8">
        <header className="rounded-3xl border border-white/5 bg-slate-950/60 p-6 shadow-panel">
          <p className="text-xs uppercase tracking-[0.3em] text-cyan-300">Replay editor</p>
          <h1 className="mt-2 text-3xl font-semibold text-white">
            {manifest?.meeting_id ?? 'Meeting Recording Replay'}
          </h1>
          <p className="mt-2 text-sm text-slate-300">{headerSubtitle}</p>
        </header>

        <div className="grid gap-6 lg:grid-cols-[1.7fr,1fr]">
          <div className="space-y-5">
            <VideoPlayer
              videoRef={video.videoRef}
              manifest={manifest}
              isReady={video.isReady}
              isBuffering={video.isBuffering}
              error={video.error}
              mediaTimeMs={transportTimeMs}
              events={eventsState.events}
            />

            <PlaybackControls
              isReady={audioMixer.ready || video.isReady}
              isPlaying={isPlaying}
              isBuffering={video.isBuffering}
              currentTime={transportTimeMs / 1000}
              duration={durationMs / 1000}
              playbackRate={playbackRate}
              onPlay={handlePlay}
              onPause={handlePause}
              onSeek={handleSeek}
              onRateChange={setPlaybackRate}
              manifestUrl={manifestInput}
              manifestError={manifestError}
              manifestLoading={manifestLoading}
              onManifestUrlChange={setManifestInput}
              onManifestLoad={handleManifestLoad}
            />

            <EventTimeline
              durationMs={durationMs}
              mediaTimeMs={transportTimeMs}
              events={eventsState.events}
              onSeek={handleSeekMediaTime}
              loading={eventsState.loading}
              error={eventsState.error}
            />
          </div>

          <aside className="space-y-5">
            <UserTrackPanel
              participants={manifest?.participants ?? []}
              events={eventsState.events}
              mediaTimeMs={transportTimeMs}
              tracks={audioMixer.tracks}
            />
            <AudioTrackControls mixer={audioMixer} />
          </aside>
        </div>
      </div>
    </div>
  )
}

export default App
