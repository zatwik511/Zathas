import ZathasMark from '../components/ZathasMark'
import Scanline from '../components/Scanline'

const GITHUB_URL = 'https://github.com/zatwik511/Zathas'
const EMAIL = 'sa7wik@gmail.com'

const STACK = [
 { name: 'C++17', role: 'Core server engine' },
 { name: 'llama.cpp', role: 'LLM inference (GGUF)' },
 { name: 'cpp-httplib', role: 'Embedded HTTP server' },
 { name: 'React + Vite', role: 'Frontend UI' },
 { name: 'Tailwind CSS', role: 'Styling' },
 { name: 'SQLite', role: 'Persistence' },
 { name: 'SSE', role: 'Live token streaming' },
 { name: 'Marked + hljs', role: 'Markdown & code rendering' },
]

const FEATURES = [
 { t: 'Streaming replies', d: 'Responses stream in token-by-token over Server-Sent Events, so you see the answer as it’s written.' },
 { t: 'Document uploads', d: 'Attach a PDF or text file and ask questions about it — the text is extracted server-side and added to context.' },
 { t: 'Rich markdown', d: 'Answers render as formatted markdown with syntax-highlighted code blocks.' },
 { t: 'Export anytime', d: 'Download any conversation as a clean markdown file with a single click.' },
]

const STEPS = [
 { n: '01', t: 'Request', d: 'Your message hits the C++ server over HTTP, which assembles the context — system prompt, conversation history, and any uploaded document.' },
 { n: '02', t: 'Inference', d: 'The server queries the language model and begins receiving the response.' },
 { n: '03', t: 'Stream', d: 'Tokens are streamed back over Server-Sent Events and rendered live in the React UI as markdown.' },
]

export default function About() {
 return (
 <div className="bg-techy h-full overflow-y-auto scroll-slim text-zinc-100 selection:bg-purple-500/30">
 <Scanline />
 {/* Header */}
 <header className="sticky top-0 z-10 flex items-center justify-between px-5 py-3.5 border-b border-white/5 backdrop-blur-md bg-[#08010f]/60">
 <a href="/" className="flex items-center gap-2.5 group">
 <span className="flex h-7 w-7 items-center justify-center bg-gradient-to-br from-violet-500 to-fuchsia-500 text-white shadow-lg shadow-purple-500/20"><ZathasMark className="w-[18px] h-[18px]" /></span>
 <span className="font-heading text-sm tracking-wide text-zinc-200 group-hover:text-white transition-colors">ZATHAS</span>
 </a>
 <a href="/" className="text-xs px-3.5 py-1.5 bg-white/5 text-zinc-300 hover:bg-white/10 hover:text-white transition-colors">
 Open chat →
 </a>
 </header>

 <main className="max-w-3xl mx-auto px-6 py-16 sm:py-20">
 {/* Hero */}
 <section className="animate-fade-up">
 <p className="font-mono text-xs tracking-widest text-fuchsia-400/80 mb-4">// ABOUT THE PROJECT</p>
 <h1 className="font-heading text-2xl sm:text-4xl tracking-tight text-gradient text-glow mb-5 leading-snug">
 An AI, built from<br />the ground up.
 </h1>
 <p className="text-zinc-400 text-[15px] leading-relaxed max-w-xl">
 Zathas is an AI chatbot written from scratch in C++ — not a wrapper around an API,
 but a full inference server with a streaming response pipeline and a polished React
 frontend on top.
 </p>
 </section>

 {/* Features */}
 <section className="mt-16 animate-fade-up">
 <h2 className="font-mono text-xs tracking-widest text-zinc-500 mb-5">// FEATURES</h2>
 <div className="grid sm:grid-cols-2 gap-4">
 {FEATURES.map((f) => (
 <div key={f.t} className=" border border-white/[0.08] bg-white/[0.03] p-5">
 <h3 className="font-semibold text-zinc-100 mb-2">{f.t}</h3>
 <p className="text-sm text-zinc-400 leading-relaxed">{f.d}</p>
 </div>
 ))}
 </div>
 </section>

 {/* How it works */}
 <section className="mt-16 animate-fade-up">
 <h2 className="font-mono text-xs tracking-widest text-zinc-500 mb-5">// HOW IT WORKS</h2>
 <div className="space-y-3">
 {STEPS.map((s) => (
 <div key={s.n} className="flex gap-4 border border-white/[0.06] bg-white/[0.02] p-4">
 <span className="font-mono text-sm text-violet-400/80 shrink-0 pt-0.5">{s.n}</span>
 <div>
 <h3 className="text-sm font-semibold text-zinc-100 mb-1">{s.t}</h3>
 <p className="text-sm text-zinc-400 leading-relaxed">{s.d}</p>
 </div>
 </div>
 ))}
 </div>
 </section>

 {/* Stack */}
 <section className="mt-16 animate-fade-up">
 <h2 className="font-mono text-xs tracking-widest text-zinc-500 mb-5">// STACK</h2>
 <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
 {STACK.map((s) => (
 <div key={s.name} className=" border border-white/[0.06] bg-white/[0.02] p-3.5 hover:border-white/15 hover:bg-white/[0.05] transition-all">
 <p className="font-mono text-[13px] font-medium text-zinc-100">{s.name}</p>
 <p className="text-[11px] text-zinc-500 leading-snug mt-1">{s.role}</p>
 </div>
 ))}
 </div>
 </section>

 {/* Creator */}
 <section className="mt-16 animate-fade-up">
 <h2 className="font-mono text-xs tracking-widest text-zinc-500 mb-5">// CREATOR</h2>
 <div className=" border border-white/[0.08] bg-gradient-to-br from-white/[0.05] to-transparent p-6">
 <h3 className="text-lg font-semibold text-zinc-100">Satwik Bhatnagar</h3>
 <p className="text-sm text-zinc-400 leading-relaxed mt-2 max-w-xl">
 Built Zathas as a deep-dive into how language models actually run — implementing a
 full inference server, token streaming, and a polished chat UI from scratch in C++
 and React, rather than just calling an API.
 </p>
 <div className="flex flex-wrap gap-2.5 mt-5">
 <a href={GITHUB_URL} target="_blank" rel="noopener noreferrer"
 className="inline-flex items-center gap-2 text-xs px-3.5 py-2 bg-white/5 text-zinc-200 hover:bg-white/10 transition-colors">
 <svg className="w-3.5 h-3.5" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.531 1.032 1.531 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.203 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.02 10.02 0 0022 12.017C22 6.484 17.523 2 12 2z"/></svg>
 GitHub
 </a>
 <a href={`mailto:${EMAIL}`}
 className="inline-flex items-center gap-2 text-xs px-3.5 py-2 bg-white/5 text-zinc-200 hover:bg-white/10 transition-colors">
 <svg className="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="2" y="4" width="20" height="16" rx="2"/><path d="M22 7l-10 5L2 7"/></svg>
 Email
 </a>
 </div>
 </div>
 </section>

 {/* CTA */}
 <section className="mt-16 text-center animate-fade-up">
 <a href="/" className="inline-flex items-center gap-2 px-6 py-3 bg-gradient-to-br from-violet-500 to-fuchsia-500 text-white text-sm font-medium shadow-lg shadow-purple-500/25 hover:from-violet-400 hover:to-fuchsia-400 hover:shadow-[0_0_28px_-4px_rgba(217,70,239,0.7)] active:scale-95 transition-all">
 Start a conversation
 <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
 </a>
 <p className="mt-8 font-mono text-[11px] tracking-wide text-zinc-600">
 zathas · self-hosted · open source
 </p>
 </section>
 </main>
 </div>
 )
}
