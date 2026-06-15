// Subtle CRT-style scanline sweep. Fixed full-screen overlay, pointer-events
// none, animated with transform only (GPU-composited — no layout/paint churn).
export default function Scanline() {
  return (
    <div className="pointer-events-none fixed inset-0 overflow-hidden z-[1]" aria-hidden="true">
      <div className="absolute inset-x-0 h-24 bg-gradient-to-b from-transparent via-fuchsia-400/[0.06] to-transparent animate-scan" />
    </div>
  )
}
