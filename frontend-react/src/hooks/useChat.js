import { useState, useRef } from 'react'

export function useChat(endpoint, extraHeaders = {}, { onUnauthorized } = {}) {
  const [messages, setMessages] = useState([])
  const [streaming, setStreaming] = useState(false)
  const abortRef = useRef(null)

  // Abort an in-flight generation. Keeps whatever text already streamed in.
  function stop() {
    abortRef.current?.abort()
  }

  // Core streaming routine. Starts from `base` (prior turns), appends the new
  // user message + a streaming assistant placeholder, and streams the reply.
  async function runGeneration(base, text, docId) {
    const history = base.map(({ role, content }) => ({ role, content }))

    setMessages([
      ...base.map(({ role, content }) => ({ role, content })),
      { role: 'user', content: text },
      { role: 'assistant', content: '', streaming: true },
    ])
    setStreaming(true)

    const controller = new AbortController()
    abortRef.current = controller

    try {
      const res = await fetch(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', ...extraHeaders },
        body: JSON.stringify({ message: text, history, ...(docId ? { doc_id: docId } : {}) }),
        signal: controller.signal,
      })

      if (res.status === 401) {
        setMessages([])
        setStreaming(false)
        onUnauthorized?.()
        return
      }
      if (!res.ok) throw new Error(`HTTP ${res.status}`)

      const reader = res.body.getReader()
      const decoder = new TextDecoder()
      let buf = ''

      while (true) {
        const { done, value } = await reader.read()
        if (done) break
        buf += decoder.decode(value, { stream: true })

        let idx
        while ((idx = buf.indexOf('\n\n')) !== -1) {
          const line = buf.slice(0, idx)
          buf = buf.slice(idx + 2)
          if (!line.startsWith('data: ')) continue
          try {
            const data = JSON.parse(line.slice(6))
            if (data.token) {
              setMessages(prev => {
                const next = [...prev]
                const last = next[next.length - 1]
                next[next.length - 1] = { ...last, content: last.content + data.token }
                return next
              })
            }
            if (data.done) {
              setMessages(prev => {
                const next = [...prev]
                next[next.length - 1] = { ...next[next.length - 1], streaming: false }
                return next
              })
            }
            if (data.error) {
              setMessages(prev => {
                const next = [...prev]
                next[next.length - 1] = { ...next[next.length - 1], streaming: false, content: `Error: ${data.error}` }
                return next
              })
            }
          } catch { /* skip malformed SSE event */ }
        }
      }
    } catch (err) {
      // Intentional stop (AbortError) keeps partial text; only real failures show an error.
      if (err?.name !== 'AbortError') {
        setMessages(prev => {
          const next = [...prev]
          next[next.length - 1] = {
            ...next[next.length - 1],
            streaming: false,
            content: 'Connection error. Is the server running?',
          }
          return next
        })
      }
    } finally {
      abortRef.current = null
      setStreaming(false)
      setMessages(prev => {
        const next = [...prev]
        if (next[next.length - 1]?.streaming) {
          next[next.length - 1] = { ...next[next.length - 1], streaming: false }
        }
        return next
      })
    }
  }

  // Send a new message (appends to the current conversation).
  function sendMessage(text, docId = null) {
    return runGeneration(messages, text, docId)
  }

  // Re-run the last user prompt to get a fresh reply (drops the old reply).
  function regenerate(docId = null) {
    let i = messages.length - 1
    while (i >= 0 && messages[i].role !== 'user') i--
    if (i < 0) return
    return runGeneration(messages.slice(0, i), messages[i].content, docId)
  }

  // Edit a user message: truncate everything from that message onward and resend.
  function editMessage(index, newText, docId = null) {
    if (index < 0 || index >= messages.length) return
    return runGeneration(messages.slice(0, index), newText, docId)
  }

  return { messages, streaming, sendMessage, setMessages, stop, regenerate, editMessage }
}
