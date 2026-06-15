import { useState, useRef, useEffect } from 'react'

const MAX_BYTES = 25 * 1024 * 1024 // 25 MB

const ACCEPT = [
  'image/*', 'audio/*', 'video/*',
  '.pdf', '.txt', '.md', '.markdown', '.csv', '.tsv', '.json', '.xml',
  '.yaml', '.yml', '.log', '.docx', '.xlsx', '.pptx',
  '.js', '.jsx', '.ts', '.tsx', '.py', '.c', '.cpp', '.h', '.hpp',
  '.java', '.go', '.rs', '.rb', '.php', '.sh', '.html', '.css', '.sql',
].join(',')

export default function ChatInput({ onSend, disabled, accent = 'blue', doc, onDocChange, streaming = false, onStop }) {
  const [value, setValue] = useState('')
  const [uploading, setUploading] = useState(false)
  const [uploadPct, setUploadPct] = useState(0)
  const [uploadError, setUploadError] = useState('')
  const [recording, setRecording] = useState(false)
  const [transcribing, setTranscribing] = useState(false)
  const [dragOver, setDragOver] = useState(false)
  const textRef = useRef(null)
  const fileRef = useRef(null)
  const recorderRef = useRef(null)
  const chunksRef = useRef([])
  const streamRef = useRef(null)

  const isTechy = accent === 'techy'

  useEffect(() => {
    if (!disabled && !uploading) textRef.current?.focus()
  }, [disabled, uploading])

  function handleChange(e) {
    setValue(e.target.value)
    e.target.style.height = 'auto'
    e.target.style.height = Math.min(e.target.scrollHeight, 192) + 'px'
  }

  function handleKeyDown(e) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      submit()
    }
  }

  function submit() {
    const text = value.trim()
    if (!text || disabled || uploading) return
    onSend(text)
    setValue('')
    if (textRef.current) textRef.current.style.height = 'auto'
  }

  function showError(msg) {
    setUploadError(msg)
    setTimeout(() => setUploadError(''), 6000)
  }

  function handleFileChange(e) {
    const file = e.target.files?.[0]
    if (!file) return
    e.target.value = ''
    tryUpload(file)
  }

  function tryUpload(file) {
    setUploadError('')
    if (file.size > MAX_BYTES) { showError(`"${file.name}" is too large (max 25 MB).`); return }
    uploadFile(file)
  }

  function uploadFile(file) {
    setUploading(true)
    setUploadPct(0)
    const formData = new FormData()
    formData.append('file', file)
    const xhr = new XMLHttpRequest()
    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) setUploadPct(Math.round(e.loaded / e.total * 100))
    })
    xhr.open('POST', '/api/upload')
    xhr.onload = () => {
      setUploading(false)
      if (xhr.status === 200) {
        try {
          const data = JSON.parse(xhr.responseText)
          onDocChange?.({ id: data.doc_id, name: file.name, chars: data.char_count, kind: data.kind })
        } catch { showError('Upload succeeded but the response was malformed.') }
      } else {
        let msg = `Upload failed (${xhr.status}).`
        try { msg = JSON.parse(xhr.responseText).error || msg } catch { /* keep default */ }
        showError(msg)
      }
    }
    xhr.onerror = () => { setUploading(false); showError('Upload failed — connection error.') }
    xhr.send(formData)
  }

  // ── Voice input ───────────────────────────────────────────────────────────
  async function toggleMic() {
    if (recording) { stopRecording(); return }
    if (disabled || uploading || transcribing) return
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true })
      streamRef.current = stream
      const rec = new MediaRecorder(stream)
      chunksRef.current = []
      rec.ondataavailable = (e) => { if (e.data.size) chunksRef.current.push(e.data) }
      rec.onstop = async () => {
        streamRef.current?.getTracks().forEach((t) => t.stop())
        const blob = new Blob(chunksRef.current, { type: rec.mimeType || 'audio/webm' })
        if (blob.size > 0) await transcribe(blob)
      }
      recorderRef.current = rec
      rec.start()
      setRecording(true)
    } catch {
      showError('Microphone access denied or unavailable.')
    }
  }

  function stopRecording() {
    setRecording(false)
    try { recorderRef.current?.stop() } catch { /* ignore */ }
  }

  async function transcribe(blob) {
    setTranscribing(true)
    try {
      const fd = new FormData()
      const ext = blob.type.includes('ogg') ? 'ogg' : 'webm'
      fd.append('file', blob, `voice.${ext}`)
      const res = await fetch('/api/transcribe', { method: 'POST', body: fd })
      if (res.ok) {
        const data = await res.json()
        if (data.text) {
          setValue((v) => (v ? v + ' ' : '') + data.text.trim())
          textRef.current?.focus()
        }
      } else {
        let msg = 'Transcription failed.'
        try { msg = (await res.json()).error || msg } catch { /* keep default */ }
        showError(msg)
      }
    } catch {
      showError('Transcription failed — connection error.')
    } finally {
      setTranscribing(false)
    }
  }

  // ── Drag & drop / paste ───────────────────────────────────────────────────
  function onDrop(e) {
    e.preventDefault()
    setDragOver(false)
    const file = e.dataTransfer?.files?.[0]
    if (file) tryUpload(file)
  }
  function onDragOver(e) { e.preventDefault(); if (!dragOver) setDragOver(true) }
  function onDragLeave(e) { e.preventDefault(); setDragOver(false) }
  function onPaste(e) {
    const items = e.clipboardData?.items
    if (!items) return
    for (const it of items) {
      if (it.type.startsWith('image/')) {
        const file = it.getAsFile()
        if (file) { e.preventDefault(); tryUpload(file) }
        break
      }
    }
  }

  // ── Styling ───────────────────────────────────────────────────────────────
  const btnClass = isTechy
    ? 'bg-gradient-to-br from-violet-500 to-fuchsia-500 hover:from-violet-400 hover:to-fuchsia-400 shadow-lg shadow-purple-500/20'
    : accent === 'zinc' ? 'bg-zinc-700 hover:bg-zinc-600' : 'bg-blue-600 hover:bg-blue-700'
  const borderClass = isTechy ? 'border-white/5' : accent === 'zinc' ? 'border-zinc-800' : 'border-gray-200 dark:border-gray-700'
  const pillBg = isTechy
    ? 'bg-white/5 text-zinc-300 border border-white/10'
    : accent === 'zinc' ? 'bg-zinc-800 text-zinc-300' : 'bg-gray-200 dark:bg-gray-700 text-gray-700 dark:text-gray-300'
  const iconBtn = isTechy
    ? 'border-white/10 text-zinc-400 hover:bg-white/5 hover:text-zinc-200'
    : 'border-gray-200 dark:border-gray-700 hover:bg-gray-100 dark:hover:bg-gray-800 text-gray-500 dark:text-gray-400'
  const textareaClass = isTechy
    ? 'bg-white/[0.04] border border-white/10 text-zinc-100 placeholder:text-zinc-500 focus:border-purple-500/40 focus:bg-white/[0.06]'
    : 'bg-gray-100 dark:bg-gray-800 placeholder:text-gray-400 dark:placeholder:text-gray-600'

  return (
    <div
      className={`relative shrink-0 border-t ${borderClass} px-4 py-3`}
      onDragOver={onDragOver}
      onDragLeave={onDragLeave}
      onDrop={onDrop}
    >
      {/* Drag overlay */}
      {dragOver && (
        <div className="absolute inset-0 z-10 m-2 flex items-center justify-center border-2 border-dashed border-fuchsia-500/60 bg-[#08010f]/80 text-sm text-fuchsia-300 pointer-events-none">
          Drop a file to upload
        </div>
      )}

      <div className="flex flex-col gap-2 max-w-3xl mx-auto">
        {/* Upload error */}
        {uploadError && (
          <div className="self-start flex items-center gap-1.5 px-3 py-1.5 text-xs bg-red-500/15 text-red-400 border border-red-500/20 max-w-[340px]">
            <svg className="w-3 h-3 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/>
            </svg>
            <span>{uploadError}</span>
          </div>
        )}

        {/* Status pills: recording / transcribing / uploading / doc */}
        {recording && (
          <div className="self-start flex items-center gap-2 px-3 py-1.5 text-xs bg-red-500/15 text-red-300 border border-red-500/30">
            <span className="w-2 h-2 bg-red-500 animate-glow-pulse" />
            Recording… click the mic to stop
          </div>
        )}
        {transcribing && (
          <div className={`self-start flex items-center gap-2 px-3 py-1.5 text-xs ${pillBg}`}>
            <svg className="w-3 h-3 animate-spin" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>
            Transcribing…
          </div>
        )}
        {uploading && (
          <div className={`self-start flex items-center gap-2 px-3 py-1.5 text-xs ${pillBg}`}>
            <svg className="w-3 h-3 animate-spin" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>
            <span>{uploadPct}%</span>
          </div>
        )}
        {!uploading && doc && (
          <div className={`self-start flex items-center gap-1.5 px-3 py-1.5 text-xs ${pillBg} max-w-[260px]`}>
            <svg className="w-3 h-3 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M21.44 11.05l-9.19 9.19a6 6 0 0 1-8.49-8.49l9.19-9.19a4 4 0 0 1 5.66 5.66l-9.2 9.19a2 2 0 0 1-2.83-2.83l8.49-8.48"/>
            </svg>
            <span className="truncate">{doc.name}</span>
            <button onClick={() => onDocChange?.(null)} aria-label="Remove document" className="shrink-0 opacity-60 hover:opacity-100 transition-opacity ml-0.5">
              <svg className="w-3 h-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round"><path d="M18 6L6 18M6 6l12 12"/></svg>
            </button>
          </div>
        )}

        {/* Input row */}
        <div className="flex items-end gap-2">
          <input ref={fileRef} type="file" accept={ACCEPT} onChange={handleFileChange} className="hidden" />

          {/* Attach */}
          <button
            onClick={() => fileRef.current?.click()}
            disabled={uploading}
            aria-label="Attach file"
            className={`shrink-0 w-9 h-9 border disabled:opacity-40 flex items-center justify-center transition-colors ${iconBtn}`}
          >
            <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M21.44 11.05l-9.19 9.19a6 6 0 0 1-8.49-8.49l9.19-9.19a4 4 0 0 1 5.66 5.66l-9.2 9.19a2 2 0 0 1-2.83-2.83l8.49-8.48"/>
            </svg>
          </button>

          {/* Mic / voice */}
          <button
            onClick={toggleMic}
            disabled={uploading || transcribing}
            aria-label={recording ? 'Stop recording' : 'Voice input'}
            className={`shrink-0 w-9 h-9 border disabled:opacity-40 flex items-center justify-center transition-all ${
              recording ? 'border-red-500/50 bg-red-500/20 text-red-400 animate-glow-pulse' : iconBtn
            }`}
          >
            <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <rect x="9" y="2" width="6" height="12" rx="3"/><path d="M5 10a7 7 0 0 0 14 0M12 19v3"/>
            </svg>
          </button>

          <textarea
            ref={textRef}
            value={value}
            onChange={handleChange}
            onKeyDown={handleKeyDown}
            onPaste={onPaste}
            disabled={disabled || uploading}
            placeholder="Message Zathas..."
            rows={1}
            className={`flex-1 resize-none px-4 py-2.5 text-[15px] leading-relaxed outline-none disabled:opacity-50 transition-colors ${textareaClass}`}
          />

          {streaming ? (
            <button
              onClick={onStop}
              aria-label="Stop generating"
              className={`shrink-0 w-9 h-9 flex items-center justify-center transition-all active:scale-90 hover:shadow-[0_0_18px_-2px_rgba(217,70,239,0.7)] ${btnClass}`}
            >
              <svg className="w-3.5 h-3.5 text-white" viewBox="0 0 24 24" fill="currentColor"><rect x="5" y="5" width="14" height="14" /></svg>
            </button>
          ) : (
            <button
              onClick={submit}
              disabled={disabled || uploading || !value.trim()}
              aria-label="Send"
              className={`shrink-0 w-9 h-9 disabled:opacity-40 disabled:cursor-not-allowed flex items-center justify-center transition-all active:scale-90 enabled:hover:shadow-[0_0_18px_-2px_rgba(217,70,239,0.7)] ${btnClass}`}
            >
              <svg className="w-4 h-4 text-white" viewBox="0 0 24 24" fill="currentColor">
                <path d="M3.478 2.405a.75.75 0 00-.926.94l2.432 7.905H13.5a.75.75 0 010 1.5H4.984l-2.432 7.905a.75.75 0 00.926.94 60.519 60.519 0 0018.445-8.986.75.75 0 000-1.218A60.517 60.517 0 003.478 2.405z"/>
              </svg>
            </button>
          )}
        </div>
      </div>
    </div>
  )
}
