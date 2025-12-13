import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import Hls from 'hls.js'
import { clamp, mediaTimeFromVideo, videoTimeFromMedia } from '../utils/timeSync'

interface UseVideoPlayerOptions {
  sourceUrl: string | null
  offsetMs: number
  enableShortcuts?: boolean
}

export const useVideoPlayer = ({
  sourceUrl,
  offsetMs,
  enableShortcuts = true,
}: UseVideoPlayerOptions) => {
  const videoRef = useRef<HTMLVideoElement | null>(null)
  const [isReady, setIsReady] = useState(false)
  const [isPlaying, setIsPlaying] = useState(false)
  const [isBuffering, setIsBuffering] = useState(false)
  const [currentTime, setCurrentTime] = useState(0)
  const [duration, setDuration] = useState(0)
  const [playbackRate, setPlaybackRateState] = useState(1)
  const [error, setError] = useState<string | null>(null)
  const hlsRef = useRef<Hls | null>(null)
  const playPromiseRef = useRef<Promise<void> | null>(null)
  const bufferingTimeoutRef = useRef<number | null>(null)

  const detachHls = () => {
    hlsRef.current?.destroy()
    hlsRef.current = null
  }

  useEffect(() => {
    const targetVideo = videoRef.current
    if (!targetVideo) {
      return
    }

    detachHls()

    if (!sourceUrl) {
      targetVideo.pause()
      targetVideo.removeAttribute('src')
      targetVideo.load()
      setIsReady(false)
      setIsPlaying(false)
      setIsBuffering(false)
      setError(null)
      return
    }

    const handleLoaded = () => {
      setDuration(Number.isFinite(targetVideo.duration) ? targetVideo.duration : 0)
      setIsReady(true)
      setIsBuffering(false)
    }
    const handleWaiting = () => {
      // Debounce buffering state to avoid rapid flickering
      if (bufferingTimeoutRef.current !== null) {
        window.clearTimeout(bufferingTimeoutRef.current)
      }
      bufferingTimeoutRef.current = window.setTimeout(() => {
        setIsBuffering(true)
        bufferingTimeoutRef.current = null
      }, 150)
    }
    const handlePlaying = () => {
      // Clear any pending buffering timeout
      if (bufferingTimeoutRef.current !== null) {
        window.clearTimeout(bufferingTimeoutRef.current)
        bufferingTimeoutRef.current = null
      }
      setIsPlaying(true)
      setIsBuffering(false)
    }
    const handlePause = () => setIsPlaying(false)
    const handleTimeUpdate = () => setCurrentTime(targetVideo.currentTime)

    targetVideo.addEventListener('loadedmetadata', handleLoaded)
    targetVideo.addEventListener('canplay', handleLoaded)
    targetVideo.addEventListener('waiting', handleWaiting)
    targetVideo.addEventListener('playing', handlePlaying)
    targetVideo.addEventListener('pause', handlePause)
    targetVideo.addEventListener('timeupdate', handleTimeUpdate)

    if (targetVideo.canPlayType('application/vnd.apple.mpegurl')) {
      targetVideo.src = sourceUrl
    } else if (Hls.isSupported()) {
      const hls = new Hls({ enableWorker: true, lowLatencyMode: true })
      hls.on(Hls.Events.ERROR, (_evt, data) => {
        if (data?.fatal) {
          setError(data.error?.message ?? data.type ?? 'Unknown HLS error')
          detachHls()
        }
      })
      hls.loadSource(sourceUrl)
      hls.attachMedia(targetVideo)
      hlsRef.current = hls
    } else {
      queueMicrotask(() => setError('HLS is not supported in this browser'))
    }

    return () => {
      targetVideo.removeEventListener('loadedmetadata', handleLoaded)
      targetVideo.removeEventListener('canplay', handleLoaded)
      targetVideo.removeEventListener('waiting', handleWaiting)
      targetVideo.removeEventListener('playing', handlePlaying)
      targetVideo.removeEventListener('pause', handlePause)
      targetVideo.removeEventListener('timeupdate', handleTimeUpdate)
      if (bufferingTimeoutRef.current !== null) {
        window.clearTimeout(bufferingTimeoutRef.current)
        bufferingTimeoutRef.current = null
      }
      targetVideo.pause()
      targetVideo.removeAttribute('src')
      targetVideo.load()
      detachHls()
      setIsReady(false)
      setIsPlaying(false)
      setIsBuffering(false)
      setError(null)
    }
  }, [sourceUrl])

  useEffect(() => {
    const video = videoRef.current
    if (video) {
      video.playbackRate = playbackRate
    }
  }, [playbackRate])

  const play = useCallback(async () => {
    const video = videoRef.current
    if (!video) return
    
    // Wait for any pending play promise to resolve before starting a new one
    if (playPromiseRef.current) {
      await playPromiseRef.current.catch(() => {
        // Ignore errors from previous play attempts
      })
    }
    
    playPromiseRef.current = video.play().catch((err: Error) => {
      // Only set error if it's not an interruption error
      if (!err.message.includes('interrupted')) {
        setError(err.message)
      }
    })
    
    await playPromiseRef.current
    playPromiseRef.current = null
  }, [])

  const pause = useCallback(async () => {
    const video = videoRef.current
    if (!video) return
    
    // Wait for any pending play promise before pausing
    if (playPromiseRef.current) {
      await playPromiseRef.current.catch(() => {
        // Ignore errors from play attempts
      })
      playPromiseRef.current = null
    }
    
    video.pause()
  }, [])

  const togglePlayback = useCallback(() => {
    if (isPlaying) {
      void pause()
    } else {
      void play()
    }
  }, [isPlaying, pause, play])

  const seek = useCallback(
    (seconds: number) => {
      const video = videoRef.current
      if (!video) return
      const safeTime = clamp(seconds, 0, Number.isFinite(duration) ? duration : video.duration || 0)
      video.currentTime = safeTime
      setCurrentTime(safeTime)
    },
    [duration],
  )

  const setPlaybackRate = useCallback((rate: number) => {
    setPlaybackRateState(rate)
  }, [])

  const seekToMediaTime = useCallback(
    (mediaTimeMs: number) => {
      seek(videoTimeFromMedia(mediaTimeMs, offsetMs))
    },
    [seek, offsetMs],
  )

  useEffect(() => {
    if (!enableShortcuts) {
      return
    }

    const handler = (event: KeyboardEvent) => {
      const tagName = (event.target as HTMLElement | null)?.tagName
      if (tagName === 'INPUT' || tagName === 'TEXTAREA') {
        return
      }
      switch (event.key.toLowerCase()) {
        case ' ':
        case 'k':
          event.preventDefault()
          togglePlayback()
          break
        case 'j':
          event.preventDefault()
          seek(currentTime - 5)
          break
        case 'l':
          event.preventDefault()
          seek(currentTime + 5)
          break
        default:
          break
      }
    }

    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [enableShortcuts, currentTime, seek, togglePlayback])

  const mediaTimeMs = useMemo(() => mediaTimeFromVideo(currentTime, offsetMs), [currentTime, offsetMs])

  return {
    videoRef,
    isReady,
    isBuffering,
    isPlaying,
    currentTime,
    duration,
    playbackRate,
    mediaTimeMs,
    error,
    play,
    pause,
    togglePlayback,
    seek,
    seekToMediaTime,
    setPlaybackRate,
  }
}

