import { useState, useCallback } from 'react'

const KEY = 'zathas_conversations'
const MAX = 50   // cap stored conversations; oldest are evicted

function load() {
  try { return JSON.parse(localStorage.getItem(KEY)) || [] } catch { return [] }
}
function persist(list) {
  try { localStorage.setItem(KEY, JSON.stringify(list)) } catch { /* quota — ignore */ }
}
function sortTrim(list) {
  return [...list].sort((a, b) => b.updatedAt - a.updatedAt).slice(0, MAX)
}

export function genId() {
  return Date.now().toString(36) + Math.random().toString(36).slice(2, 7)
}

export function useConversations() {
  const [conversations, setConversations] = useState(load)

  // Insert or update a conversation. Preserves an existing AI title and createdAt
  // when the incoming record doesn't carry them (avoids wiping titles on save).
  const upsert = useCallback((conv) => {
    setConversations(prev => {
      const existing = prev.find(c => c.id === conv.id)
      const merged = {
        ...conv,
        title:     conv.title || existing?.title || '',
        createdAt: existing?.createdAt || conv.createdAt || Date.now(),
      }
      const next = sortTrim([merged, ...prev.filter(c => c.id !== conv.id)])
      persist(next)
      return next
    })
  }, [])

  const remove = useCallback((id) => {
    setConversations(prev => {
      const next = prev.filter(c => c.id !== id)
      persist(next)
      return next
    })
  }, [])

  const rename = useCallback((id, title) => {
    setConversations(prev => {
      const next = prev.map(c => (c.id === id ? { ...c, title } : c))
      persist(next)
      return next
    })
  }, [])

  return { conversations, upsert, remove, rename }
}
