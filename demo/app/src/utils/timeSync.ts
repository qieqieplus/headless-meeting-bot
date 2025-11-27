export const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max)

export const mediaTimeFromVideo = (videoSeconds: number, offsetMs: number) =>
  Math.max(0, videoSeconds * 1000 + offsetMs)

export const videoTimeFromMedia = (mediaTimeMs: number, offsetMs: number) =>
  Math.max(0, (mediaTimeMs - offsetMs) / 1000)

export const formatClockTime = (totalSeconds: number) => {
  if (!Number.isFinite(totalSeconds)) {
    return '00:00'
  }

  const seconds = Math.max(0, Math.floor(totalSeconds))
  const hrs = Math.floor(seconds / 3600)
  const mins = Math.floor((seconds % 3600) / 60)
  const secs = seconds % 60

  const two = (n: number) => n.toString().padStart(2, '0')
  return hrs > 0 ? `${hrs}:${two(mins)}:${two(secs)}` : `${two(mins)}:${two(secs)}`
}

export const formatTimestampMs = (valueMs: number) => {
  const date = new Date(valueMs)
  return `${date.toLocaleDateString()} ${date.toLocaleTimeString()}`
}

