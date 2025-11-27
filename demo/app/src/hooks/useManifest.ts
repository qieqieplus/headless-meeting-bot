import { useCallback, useEffect, useMemo, useState } from 'react'
import type { Manifest } from '../types/manifest'

const resolveToAbsoluteUrl = (input: string) =>
  new URL(input, window.location.href).toString()

export const useManifest = (initialUrl: string) => {
  const [manifestUrl, setManifestUrl] = useState(initialUrl)
  const [resolvedUrl, setResolvedUrl] = useState(() => resolveToAbsoluteUrl(initialUrl))
  const [manifest, setManifest] = useState<Manifest | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [baseUrl, setBaseUrl] = useState<string | null>(null)

  useEffect(() => {
    setResolvedUrl(resolveToAbsoluteUrl(manifestUrl))
  }, [manifestUrl])

  useEffect(() => {
    if (!resolvedUrl) {
      return
    }

    const controller = new AbortController()
    setLoading(true)
    setError(null)

    fetch(resolvedUrl, { signal: controller.signal })
      .then(async (resp) => {
        if (!resp.ok) {
          throw new Error(`Manifest request failed (${resp.status})`)
        }
        const fetchedBase = new URL('.', resp.url).toString()
        setBaseUrl(fetchedBase)
        return resp.json() as Promise<Manifest>
      })
      .then((data) => {
        setManifest(data)
      })
      .catch((err: Error) => {
        if (controller.signal.aborted) {
          return
        }
        console.error('[manifest]', err)
        setError(err.message)
        setManifest(null)
      })
      .finally(() => {
        if (!controller.signal.aborted) {
          setLoading(false)
        }
      })

    return () => controller.abort()
  }, [resolvedUrl])

  const resolveAssetUrl = useCallback(
    (path: string) => {
      if (!path) {
        return ''
      }
      if (path.startsWith('http://') || path.startsWith('https://')) {
        return path
      }
      const base = baseUrl ?? resolvedUrl
      return new URL(path, base).toString()
    },
    [baseUrl, resolvedUrl],
  )

  const refresh = useCallback(() => {
    setResolvedUrl((current) => {
      if (!current) return current
      const url = new URL(current)
      url.searchParams.set('ts', Date.now().toString())
      return url.toString()
    })
  }, [])

  return useMemo(
    () => ({
      manifest,
      manifestUrl,
      setManifestUrl,
      resolvedUrl,
      loading,
      error,
      refresh,
      resolveAssetUrl,
    }),
    [manifest, manifestUrl, resolvedUrl, loading, error, refresh, resolveAssetUrl],
  )
}

