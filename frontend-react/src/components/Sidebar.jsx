// Conversation history sidebar — localStorage-backed list, sci-fi styled.
// Docked on desktop (collapsible), slide-over drawer on mobile.
export default function Sidebar({ conversations, activeId, open, onToggle, onNew, onSelect, onDelete }) {
  return (
    <>
      {/* Mobile backdrop */}
      {open && (
        <div className="sm:hidden fixed inset-0 bg-black/60 z-20" onClick={onToggle} aria-hidden="true" />
      )}

      <aside
        className={`${open ? 'w-64' : 'w-0'} fixed sm:relative left-0 top-0 z-30 h-full shrink-0 overflow-hidden
                    bg-[#0c0118]/95 sm:bg-[#0c0118]/70 backdrop-blur-md border-r border-white/10
                    transition-[width] duration-200`}
      >
        <div className="w-64 h-full flex flex-col">
          {/* Header row */}
          <div className="flex items-center justify-between px-3 py-3 border-b border-white/10">
            <span className="font-heading text-[13px] tracking-wide text-zinc-300">ZATHAS</span>
            <button
              onClick={onToggle}
              aria-label="Collapse sidebar"
              className="w-7 h-7 flex items-center justify-center text-zinc-500 hover:text-zinc-200 hover:bg-white/5 transition-colors"
            >
              <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <rect x="3" y="4" width="18" height="16"/><path d="M9 4v16"/>
              </svg>
            </button>
          </div>

          {/* New chat */}
          <div className="p-2">
            <button
              onClick={onNew}
              className="w-full flex items-center gap-2 px-3 py-2 text-sm text-zinc-200 border border-white/10 bg-white/[0.03] hover:bg-white/[0.07] hover:border-fuchsia-500/40 active:scale-[0.99] transition-all"
            >
              <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 5v14M5 12h14"/>
              </svg>
              New chat
            </button>
          </div>

          {/* Conversation list */}
          <div className="flex-1 overflow-y-auto scroll-slim px-2 pb-2 space-y-0.5">
            {conversations.length === 0 ? (
              <p className="text-[11px] text-zinc-600 px-2 py-3">No conversations yet.</p>
            ) : (
              conversations.map((c) => (
                <div
                  key={c.id}
                  className={`group flex items-center border-l-2 transition-colors ${
                    c.id === activeId
                      ? 'bg-fuchsia-500/15 border-fuchsia-500'
                      : 'border-transparent hover:bg-white/[0.04]'
                  }`}
                >
                  <button
                    onClick={() => onSelect(c.id)}
                    className="flex-1 min-w-0 text-left px-2.5 py-2 text-[13px] text-zinc-300 truncate"
                    title={c.title || 'New conversation'}
                  >
                    {c.title || (
                      <span className="text-zinc-500 italic">Untitled…</span>
                    )}
                  </button>
                  <button
                    onClick={() => onDelete(c.id)}
                    aria-label="Delete conversation"
                    className="shrink-0 w-7 h-7 mr-1 flex items-center justify-center text-zinc-600 opacity-0 group-hover:opacity-100 hover:text-red-400 transition-all"
                  >
                    <svg className="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                      <path d="M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14"/>
                    </svg>
                  </button>
                </div>
              ))
            )}
          </div>
        </div>
      </aside>
    </>
  )
}
