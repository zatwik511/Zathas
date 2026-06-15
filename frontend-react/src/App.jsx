import { useState, useRef, useEffect } from 'react'
import ChatMessage from './components/ChatMessage'
import ChatInput from './components/ChatInput'
import ZathasMark from './components/ZathasMark'
import Scanline from './components/Scanline'
import Sidebar from './components/Sidebar'
import { useChat } from './hooks/useChat'
import { useConversations, genId } from './hooks/useConversations'

const GITHUB_URL = 'https://github.com/zatwik511/Zathas-AI'

const EXAMPLE_PROMPTS = [
  { label: 'How were you built?',  text: 'How were you built? Walk me through your tech stack.' },
  { label: 'Explain transformers', text: 'Explain how transformer models work, simply.' },
  { label: 'Write some C++',       text: 'Write a small C++ function that reverses a linked list, with comments.' },
  { label: 'Plan my week',         text: 'Help me plan a productive week as a software engineering student.' },
]

export default function App() {
  const { conversations, upsert, remove, rename } = useConversations()
  const { messages, streaming, sendMessage, setMessages, stop, regenerate, editMessage } = useChat('/api/chat')
  const [activeId, setActiveId] = useState(genId)
  const [doc, setDoc] = useState(null)
  const [sidebarOpen, setSidebarOpen] = useState(
    () => !window.matchMedia('(max-width: 639px)').matches   // closed by default on mobile
  )
  const [showScrollBtn, setShowScrollBtn] = useState(false)
  const bottomRef = useRef(null)
  const mainRef = useRef(null)
  const atBottomRef = useRef(true)
  const titledRef = useRef(new Set())   // conversation ids already given an AI title
  const skipPersistRef = useRef(false)  // skip the persist effect when merely loading a chat
  const lastLenRef = useRef(0)          // last message count we persisted at
  const hasMessages = messages.length > 0

  // Auto-scroll on new content — but only if the user is already near the bottom.
  useEffect(() => {
    if (atBottomRef.current) bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])

  function onMainScroll() {
    const el = mainRef.current
    if (!el) return
    const near = el.scrollHeight - el.scrollTop - el.clientHeight < 80
    atBottomRef.current = near
    setShowScrollBtn(!near && messages.length > 0)
  }

  function scrollToBottom() {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
    atBottomRef.current = true
    setShowScrollBtn(false)
  }

  // Keyboard shortcuts: Esc stops a running generation; Ctrl/Cmd+Shift+O = new chat.
  useEffect(() => {
    function onKey(e) {
      if (e.key === 'Escape' && streaming) { stop() }
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && (e.key === 'o' || e.key === 'O')) {
        e.preventDefault(); newChat()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [streaming, stop]) // eslint-disable-line react-hooks/exhaustive-deps

  // Persist the active conversation — but only when a message is ADDED or a reply
  // finishes streaming, never on every token (which would thrash localStorage).
  useEffect(() => {
    if (messages.length === 0) return
    if (skipPersistRef.current) { skipPersistRef.current = false; lastLenRef.current = messages.length; return }
    const lengthChanged = messages.length !== lastLenRef.current
    lastLenRef.current = messages.length
    if (!lengthChanged && streaming) return   // mid-stream token — skip
    upsert({
      id: activeId,
      title: '',
      createdAt: Date.now(),
      updatedAt: Date.now(),
      messages: messages.map(({ role, content }) => ({ role, content })),
    })
  }, [messages, streaming, activeId, upsert])

  // After the first completed exchange, fetch a short AI title (once per chat).
  useEffect(() => {
    if (streaming || titledRef.current.has(activeId)) return
    const firstUser = messages.find(m => m.role === 'user')
    const firstAssistant = messages.find(m => m.role === 'assistant' && m.content)
    if (!firstUser || !firstAssistant) return
    titledRef.current.add(activeId)
    const id = activeId
    fetch('/api/title', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: firstUser.content, reply: firstAssistant.content }),
    })
      .then(r => (r.ok ? r.json() : null))
      .then(d => { if (d?.title) rename(id, d.title) })
      .catch(() => {})
  }, [messages, streaming, activeId, rename])

  function closeSidebarMobile() {
    if (window.matchMedia('(max-width: 639px)').matches) setSidebarOpen(false)
  }

  function newChat() {
    setActiveId(genId())
    setMessages([])
    setDoc(null)
    closeSidebarMobile()
  }

  function selectChat(id) {
    const c = conversations.find(x => x.id === id)
    if (!c) return
    if (c.title) titledRef.current.add(id)   // don't re-title an existing chat
    skipPersistRef.current = true            // viewing shouldn't bump its timestamp
    setActiveId(id)
    setMessages(c.messages.map(m => ({ ...m })))
    setDoc(null)
    closeSidebarMobile()
  }

  function deleteChat(id) {
    remove(id)
    if (id === activeId) newChat()
  }

  function exportChat() {
    const md = messages
      .map(m => `**${m.role === 'user' ? 'You' : 'Zathas'}:**\n\n${m.content}`)
      .join('\n\n---\n\n')
    const blob = new Blob([md], { type: 'text/markdown' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `zathas-${new Date().toISOString().slice(0, 10)}.md`
    a.click()
    URL.revokeObjectURL(url)
  }

  return (
    <div className="bg-techy h-full flex text-zinc-100 selection:bg-purple-500/30">
      <Scanline />
      <Sidebar
        conversations={conversations}
        activeId={activeId}
        open={sidebarOpen}
        onToggle={() => setSidebarOpen(o => !o)}
        onNew={newChat}
        onSelect={selectChat}
        onDelete={deleteChat}
      />

      <div className="relative flex-1 flex flex-col min-w-0">
        {/* ── Header ─────────────────────────────────────────────────────────── */}
        <header className="shrink-0 z-10 flex items-center justify-between px-4 sm:px-5 py-3.5 border-b border-white/5 backdrop-blur-sm">
          <div className="flex items-center gap-2">
            {!sidebarOpen && (
              <button
                onClick={() => setSidebarOpen(true)}
                aria-label="Open sidebar"
                className="w-8 h-8 flex items-center justify-center text-zinc-400 hover:text-zinc-100 hover:bg-white/5 transition-colors"
              >
                <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                  <rect x="3" y="4" width="18" height="16"/><path d="M9 4v16"/>
                </svg>
              </button>
            )}
            <a href="/" className="flex items-center gap-2.5 group">
              <span className="relative flex h-7 w-7 items-center justify-center bg-gradient-to-br from-violet-500 to-fuchsia-500 text-white shadow-lg shadow-purple-500/20">
                <ZathasMark className="w-[18px] h-[18px]" />
                <span className="absolute -bottom-0.5 -right-0.5 h-2 w-2 bg-emerald-400 ring-2 ring-[#08010f]" />
              </span>
              <span className="font-heading text-sm tracking-wide text-zinc-200 group-hover:text-white transition-colors">
                ZATHAS
              </span>
            </a>
          </div>

          <nav className="flex items-center gap-1.5">
            {hasMessages && (
              <button
                onClick={exportChat}
                className="text-xs px-3 py-1.5 text-zinc-400 hover:text-zinc-100 hover:bg-white/5 transition-colors"
              >
                Export
              </button>
            )}
            <a href="/about" className="text-xs px-3 py-1.5 text-zinc-400 hover:text-zinc-100 hover:bg-white/5 transition-colors">
              About
            </a>
            <a href={GITHUB_URL} target="_blank" rel="noopener noreferrer" aria-label="GitHub repository"
               className="flex items-center justify-center w-8 h-8 text-zinc-400 hover:text-zinc-100 hover:bg-white/5 transition-colors">
              <svg className="w-4 h-4" viewBox="0 0 24 24" fill="currentColor">
                <path d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.531 1.032 1.531 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.203 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.02 10.02 0 0022 12.017C22 6.484 17.523 2 12 2z"/>
              </svg>
            </a>
          </nav>
        </header>

        {/* ── Main ───────────────────────────────────────────────────────────── */}
        <main ref={mainRef} onScroll={onMainScroll} className="flex-1 overflow-y-auto scroll-slim">
          {!hasMessages ? (
            <Hero onPick={(text) => sendMessage(text, doc?.id)} />
          ) : (
            <div className="max-w-3xl mx-auto px-4 py-8 space-y-7">
              {messages.map((msg, i) => (
                <ChatMessage
                  key={i}
                  message={msg}
                  accent="techy"
                  isLast={i === messages.length - 1}
                  streaming={streaming}
                  onRegenerate={() => regenerate(doc?.id)}
                  onEdit={(newText) => editMessage(i, newText, doc?.id)}
                />
              ))}
              <div ref={bottomRef} />
            </div>
          )}
        </main>

        {/* Scroll-to-bottom button */}
        {showScrollBtn && (
          <button
            onClick={scrollToBottom}
            aria-label="Scroll to bottom"
            className="absolute left-1/2 -translate-x-1/2 bottom-[108px] z-10 w-9 h-9 flex items-center justify-center border border-white/15 bg-[#0c0118]/90 backdrop-blur-sm text-zinc-300 hover:text-white hover:border-fuchsia-500/50 hover:shadow-[0_0_18px_-4px_rgba(217,70,239,0.6)] transition-all animate-fade-up"
          >
            <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M12 5v14M5 12l7 7 7-7"/>
            </svg>
          </button>
        )}

        {/* ── Input ──────────────────────────────────────────────────────────── */}
        <ChatInput
          accent="techy"
          onSend={(text) => sendMessage(text, doc?.id)}
          disabled={streaming}
          streaming={streaming}
          onStop={stop}
          doc={doc}
          onDocChange={setDoc}
        />
        <p className="shrink-0 text-center text-[10px] text-zinc-600 pb-2 px-4">
          Zathas can make mistakes. Double-check important information.
        </p>
      </div>
    </div>
  )
}

/* ── Hero / landing empty state ─────────────────────────────────────────────── */
function Hero({ onPick }) {
  return (
    <div className="min-h-full flex flex-col items-center justify-center px-6 py-12 text-center">
      {/* Glowing orb with rotating reactor frames */}
      <div className="relative mb-8 animate-fade-up">
        <div className="relative animate-float">
          <div className="absolute -inset-5 border border-violet-500/25 animate-spin-slow" />
          <div className="absolute -inset-3 border border-fuchsia-500/20 animate-spin-rev" />
          <div className="absolute inset-0 blur-2xl bg-gradient-to-br from-violet-500 to-fuchsia-500 opacity-50 animate-glow-pulse" />
          <div className="relative flex h-16 w-16 items-center justify-center bg-gradient-to-br from-violet-500 to-fuchsia-500 text-white shadow-xl shadow-purple-500/30">
            <ZathasMark className="w-9 h-9" />
          </div>
        </div>
      </div>

      <h1 className="font-heading text-2xl sm:text-3xl tracking-tight text-gradient text-glow mb-4 leading-snug animate-flicker-in">
        Hi, I'm Zathas.
      </h1>
      <p className="max-w-md text-sm sm:text-[15px] leading-relaxed text-zinc-400 mb-8 animate-fade-up" style={{ animationDelay: '0.5s' }}>
        A self-hosted AI assistant — built from scratch in C++ with llama.cpp and a
        React frontend. Ask me anything.
      </p>

      <div className="grid grid-cols-1 sm:grid-cols-2 gap-2.5 w-full max-w-xl">
        {EXAMPLE_PROMPTS.map((p, i) => (
          <button
            key={p.label}
            onClick={() => onPick(p.text)}
            style={{ animationDelay: `${0.6 + i * 0.09}s` }}
            className="group relative text-left px-4 py-3 border border-white/[0.08] bg-white/[0.03] hover:bg-white/[0.06] hover:border-fuchsia-500/40 hover:shadow-[0_0_22px_-6px_rgba(217,70,239,0.55)] active:scale-[0.98] transition-all animate-fade-up"
          >
            <span className="text-sm text-zinc-300 group-hover:text-white transition-colors">
              {p.label}
            </span>
            <svg className="inline-block w-3.5 h-3.5 ml-1.5 -mt-0.5 text-zinc-600 group-hover:text-fuchsia-400 group-hover:translate-x-0.5 transition-all" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M5 12h14M12 5l7 7-7 7"/>
            </svg>
          </button>
        ))}
      </div>

      <p className="mt-10 font-mono text-[11px] tracking-wide text-zinc-600 animate-fade-up" style={{ animationDelay: '1s' }}>
        C++ · llama.cpp · cloud inference · React
      </p>
    </div>
  )
}
