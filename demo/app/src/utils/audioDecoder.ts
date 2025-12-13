import Hls from 'hls.js'

export interface AudioHandle {
  element: HTMLAudioElement
  destroy: () => void
}

/**
 * Create a native audio handle for MP3/AAC/M4A/WAV files.
 * These formats support HTTP range requests for streaming and seeking.
 */
export const createNativeAudioHandle = (
  sourceUrl: string,
  onError?: (message: string) => void,
): AudioHandle => {
  const element = document.createElement('audio')
  element.crossOrigin = 'anonymous'
  element.preload = 'auto'
  element.setAttribute('playsinline', 'true')
  element.src = sourceUrl
  element.load()

  element.addEventListener('error', () => {
    const error = element.error
    if (error && onError) {
      let message = 'Unknown audio error'
      switch (error.code) {
        case MediaError.MEDIA_ERR_ABORTED:
          message = 'Audio loading aborted'
          break
        case MediaError.MEDIA_ERR_NETWORK:
          message = 'Network error loading audio'
          break
        case MediaError.MEDIA_ERR_DECODE:
          message = 'Audio decode error'
          break
        case MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED:
          message = 'Audio format not supported'
          break
      }
      onError(message)
    }
  })

  return {
    element,
    destroy: () => {
      element.src = ''
      element.load()
    },
  }
}

/**
 * Create an HLS audio handle for m3u8 playlists.
 * Only use this for HLS streams; prefer createNativeAudioHandle for MP3/AAC.
 */
export const createHlsAudioHandle = (
  sourceUrl: string,
  onError?: (message: string) => void,
): AudioHandle => {
  const element = document.createElement('audio')
  element.crossOrigin = 'anonymous'
  element.preload = 'auto'
  element.setAttribute('playsinline', 'true')

  let hls: Hls | null = null

  if (element.canPlayType('application/vnd.apple.mpegurl')) {
    element.src = sourceUrl
    element.load()
  } else if (Hls.isSupported()) {
    hls = new Hls({ enableWorker: true, lowLatencyMode: true })
    hls.loadSource(sourceUrl)
    hls.attachMedia(element)
    hls.on(Hls.Events.ERROR, (_evt, data) => {
      if (data?.fatal && onError) {
        onError(data.error?.message ?? data.type ?? 'Unknown HLS error')
      }
    })
  } else {
    element.src = sourceUrl
    onError?.('HLS is not supported in this browser')
  }

  return {
    element,
    destroy: () => {
      if (hls) {
        hls.destroy()
        hls = null
      }
      element.src = ''
      element.load()
    },
  }
}

/**
 * Create an audio handle based on the format.
 * Automatically chooses native playback for MP3/AAC/M4A, HLS for m3u8.
 */
export const createAudioHandle = (
  sourceUrl: string,
  format: 'mp3' | 'aac' | 'm4a' | 'wav' | 'm3u8',
  onError?: (message: string) => void,
): AudioHandle => {
  if (format === 'm3u8') {
    return createHlsAudioHandle(sourceUrl, onError)
  }
  return createNativeAudioHandle(sourceUrl, onError)
}

