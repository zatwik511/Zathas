import { useMemo, useState, useRef, useEffect } from 'react'
import { marked } from '../lib/markdown'
import ZathasMark from './ZathasMark'

export default function ChatMessage({
  message, accent = 'blue', isLast = false, streaming = false, onRegenerate, onEdit,
}) {
  const isUser = message.role === 'user'
  const [copied, setCopied] = useState(false)
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState('')
  const proseRef = useRef(null)

  const html = useMemo(() => {
    if (isUser || message.streaming) return null
    return marked.parse(message.content || '')
  }, [isUser, message.streaming, message.content])

  // Decorate rendered code blocks with a header (language label + copy button).
  useEffect(() => {
    if (isUser || message.streaming) return
    const root = proseRef.current
    if (!root) return
    root.querySelectorAll('pre').forEach((pre) => {
      if (pre.dataset.enhanced) return
      pre.dataset.enhanced = '1'
      const code = pre.querySelector('code')
      const langClass = [...(code?.classList || [])].find((c) => c.startsWith('language-'))
      const lang = langClass ? langClass.replace('language-', '') : 'code'

      const head = document.createElement('div')
      head.className = 'code-head'
      const label = document.createElement('span')
      label.className = 'code-lang'
      label.textContent = lang
      const btn = document.createElement('button')
      btn.type = 'button'
      btn.className = 'code-copy'
      btn.textContent = 'Copy'
      btn.addEventListener('click', () => {
        navigator.clipboard?.writeText(code?.textContent || '')
        btn.textContent = 'Copied'
        setTimeout(() => { btn.textContent = 'Copy' }, 1500)
      })
      head.appendChild(label)
      head.appendChild(btn)

      const wrapper = document.createElement('div')
      wrapper.className = 'code-block'
      pre.parentNode.insertBefore(wrapper, pre)
      wrapper.appendChild(head)
      wrapper.appendChild(pre)
    })
  }, [isUser, message.streaming, html])

  function copy() {
    navigator.clipboard?.writeText(message.content || '').then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    })
  }
  function startEdit() { setDraft(message.content); setEditing(true) }
  function saveEdit() {
    const t = draft.trim()
    if (t && onEdit) onEdit(t)
    setEditing(false)
  }

  const isTechy = accent === 'techy'

  const userBg = isTechy
    ? 'bg-gradient-to-br from-violet-500/90 to-fuchsia-500/80 text-white'
    : accent === 'zinc' ? 'bg-zinc-700 text-white' : 'bg-blue-600 text-white'
  const avatarBg = isTechy ? 'bg-gradient-to-br from-violet-500 to-fuchsia-500' : 'bg-violet-600'
  const proseClass = isTechy
    ? 'prose prose-sm prose-invert max-w-none prose-p:text-zinc-200 prose-headings:text-zinc-100 prose-strong:text-white prose-a:text-fuchsia-400'
    : 'prose prose-sm dark:prose-invert max-w-none'
  const bodyText = isTechy ? 'text-zinc-200' : 'text-gray-900 dark:text-gray-100'
  const cursorColor = isTechy ? 'bg-fuchsia-400' : 'bg-gray-500 dark:bg-gray-400'

  const actionBtn =
    'inline-flex items-center gap-1.5 text-[11px] px-2 py-1 transition-all ' +
    (isTechy
      ? 'text-zinc-500 hover:text-zinc-200 hover:bg-white/5'
      : 'text-gray-400 hover:text-gray-700 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-800')

  // ── User message ────────────────────────────────────────────────────────────
  if (isUser) {
    if (editing) {
      return (
        <div className="flex justify-end animate-send-in">
          <div className="w-full max-w-[80%] flex flex-col gap-2">
            <textarea
              value={draft}
              autoFocus
              rows={3}
              onChange={(e) => setDraft(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); saveEdit() }
                if (e.key === 'Escape') setEditing(false)
              }}
              className="w-full resize-none bg-white/[0.05] border border-fuchsia-500/40 px-3 py-2 text-[15px] text-zinc-100 outline-none focus:border-fuchsia-500/70"
            />
            <div className="flex justify-end gap-2">
              <button onClick={() => setEditing(false)} className="text-xs px-3 py-1.5 text-zinc-400 hover:text-zinc-100 hover:bg-white/5 transition-colors">
                Cancel
              </button>
              <button onClick={saveEdit} className="text-xs px-3 py-1.5 bg-gradient-to-br from-violet-500 to-fuchsia-500 text-white hover:shadow-[0_0_16px_-2px_rgba(217,70,239,0.6)] active:scale-95 transition-all">
                Save &amp; send
              </button>
            </div>
          </div>
        </div>
      )
    }
    return (
      <div className="group flex justify-end items-start gap-2 animate-send-in">
        {!streaming && onEdit && (
          <button
            onClick={startEdit}
            aria-label="Edit message"
            className="shrink-0 mt-1 w-7 h-7 flex items-center justify-center text-zinc-600 opacity-0 group-hover:opacity-100 hover:text-fuchsia-400 transition-all"
          >
            <svg className="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M12 20h9M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z"/>
            </svg>
          </button>
        )}
        <div className={`max-w-[70%] sm:max-w-[60%] ${userBg} px-4 py-2.5 text-[15px] leading-relaxed whitespace-pre-wrap break-words shadow-lg ${isTechy ? 'shadow-purple-500/10' : ''}`}>
          {message.content}
        </div>
      </div>
    )
  }

  // ── Assistant message ───────────────────────────────────────────────────────
  return (
    <div className="group flex items-start gap-3 animate-recv-in">
      <div className={`shrink-0 w-7 h-7 mt-0.5 ${avatarBg} flex items-center justify-center text-white select-none transition-shadow ${message.streaming && isTechy ? 'shadow-[0_0_14px_2px_rgba(217,70,239,0.55)]' : 'shadow-md'}`}>
        {isTechy ? <ZathasMark className="w-[18px] h-[18px]" /> : <span className="text-xs font-bold">Z</span>}
      </div>
      <div className="flex-1 min-w-0 text-[15px] leading-relaxed">
        {message.streaming ? (
          <p className={`whitespace-pre-wrap break-words ${bodyText}`}>
            {message.content}
            <span className={`inline-block w-0.5 h-[1em] ${cursorColor} animate-cursor ml-0.5 align-middle`} />
          </p>
        ) : (
          <>
            <div ref={proseRef} className={proseClass} dangerouslySetInnerHTML={{ __html: html || '' }} />
            {message.content && (
              <div className="mt-2 flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-all">
                <button onClick={copy} aria-label="Copy message" className={actionBtn}>
                  {copied ? (
                    <>
                      <svg className="w-3 h-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M20 6L9 17l-5-5"/></svg>
                      Copied
                    </>
                  ) : (
                    <>
                      <svg className="w-3 h-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>
                      Copy
                    </>
                  )}
                </button>
                {isLast && !streaming && onRegenerate && (
                  <button onClick={onRegenerate} aria-label="Regenerate response" className={actionBtn}>
                    <svg className="w-3 h-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                      <path d="M23 4v6h-6M1 20v-6h6"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/>
                    </svg>
                    Regenerate
                  </button>
                )}
              </div>
            )}
          </>
        )}
      </div>
    </div>
  )
}
