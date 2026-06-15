import typography from '@tailwindcss/typography'

/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,jsx}'],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: {
        // Electrolize for body/UI, Zen Dots for display headings & brand.
        sans:    ['Electrolize', 'ui-sans-serif', 'system-ui', 'sans-serif'],
        heading: ['"Zen Dots"', 'Electrolize', 'sans-serif'],
        mono:    ['ui-monospace', 'SFMono-Regular', 'Menlo', 'monospace'],
      },
      colors: {
        // Sci-fi purple accent
        accent: {
          DEFAULT: '#a855f7',   // purple-500
          glow:    '#c084fc',   // purple-400
          deep:    '#7c3aed',   // violet-600
          fuchsia: '#d946ef',   // fuchsia-500
        },
      },
      keyframes: {
        'fade-up': {
          '0%':   { opacity: '0', transform: 'translateY(8px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
        'fade-in': {
          '0%':   { opacity: '0' },
          '100%': { opacity: '1' },
        },
        'glow-pulse': {
          '0%, 100%': { opacity: '0.5' },
          '50%':      { opacity: '0.85' },
        },
        'float': {
          '0%, 100%': { transform: 'translateY(0)' },
          '50%':      { transform: 'translateY(-6px)' },
        },
        'send-in': {
          '0%':   { opacity: '0', transform: 'translateX(26px) translateY(4px)' },
          '100%': { opacity: '1', transform: 'translateX(0) translateY(0)' },
        },
        'recv-in': {
          '0%':   { opacity: '0', transform: 'translateX(-16px)' },
          '100%': { opacity: '1', transform: 'translateX(0)' },
        },
        'flicker-in': {
          '0%':   { opacity: '0' },
          '10%':  { opacity: '0.55' },
          '15%':  { opacity: '0.15' },
          '25%':  { opacity: '0.9' },
          '30%':  { opacity: '0.35' },
          '60%':  { opacity: '1' },
          '63%':  { opacity: '0.7' },
          '100%': { opacity: '1' },
        },
        'scan': {
          '0%':   { transform: 'translateY(-15vh)' },
          '100%': { transform: 'translateY(105vh)' },
        },
        'spin-slow': { '0%': { transform: 'rotate(0deg)' },  '100%': { transform: 'rotate(360deg)' } },
        'spin-rev':  { '0%': { transform: 'rotate(0deg)' },  '100%': { transform: 'rotate(-360deg)' } },
      },
      animation: {
        'fade-up':    'fade-up 0.5s cubic-bezier(0.16,1,0.3,1) both',
        'fade-in':    'fade-in 0.6s ease both',
        'glow-pulse': 'glow-pulse 4s ease-in-out infinite',
        'float':      'float 6s ease-in-out infinite',
        'send-in':    'send-in 0.4s cubic-bezier(0.16,1,0.3,1) both',
        'recv-in':    'recv-in 0.45s cubic-bezier(0.16,1,0.3,1) both',
        'flicker-in': 'flicker-in 0.9s ease both',
        'scan':       'scan 7s linear infinite',
        'spin-slow':  'spin-slow 16s linear infinite',
        'spin-rev':   'spin-rev 22s linear infinite',
      },
      typography: {
        DEFAULT: {
          css: {
            pre: { padding: 0, margin: 0, backgroundColor: 'transparent' },
            'pre code': { padding: 0 },
            'code::before': { content: 'none' },
            'code::after': { content: 'none' },
          }
        },
        invert: {
          css: {
            pre: { backgroundColor: 'transparent' },
          }
        }
      }
    }
  },
  plugins: [typography]
}
