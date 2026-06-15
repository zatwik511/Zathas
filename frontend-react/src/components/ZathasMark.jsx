// Zathas sci-fi emblem — a hexagonal "AI core": hexagon frame, inner crystal
// diamond, circuit connectors, and a glowing center node. Renders in currentColor
// so it inherits text color (white on the gradient brand square).
export default function ZathasMark({ className = '' }) {
  return (
    <svg viewBox="0 0 24 24" className={className} fill="none"
         stroke="currentColor" strokeWidth="1.5"
         strokeLinejoin="round" strokeLinecap="round" aria-hidden="true">
      {/* outer hexagon frame */}
      <path d="M12 2.6 L19.4 7 L19.4 17 L12 21.4 L4.6 17 L4.6 7 Z" />
      {/* inner crystal core */}
      <path d="M12 8 L15.4 12 L12 16 L8.6 12 Z"
            fill="currentColor" fillOpacity="0.18" />
      {/* circuit connectors linking core to the hexagon's top & bottom points */}
      <path d="M12 2.6 V8 M12 16 V21.4" strokeOpacity="0.5" />
      {/* glowing center node */}
      <circle cx="12" cy="12" r="1.15" fill="currentColor" stroke="none" />
    </svg>
  )
}
