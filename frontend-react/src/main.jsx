import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import 'highlight.js/styles/atom-one-dark.css'
import App from './App.jsx'
import About from './pages/About.jsx'

// ── Optional pages ────────────────────────────────────────────────────────────
// Any .jsx dropped into src/pages/extra/ is mounted automatically at the route
// matching its filename — Extra.jsx serves /extra. This mirrors the server's
// optional modules (see src/modules/module.h) so a deployment can add its own
// screens without editing this file. The directory is absent by default.
const extraPages = import.meta.glob('./pages/extra/*.jsx', { eager: true })

const routes = {}
for (const [file, mod] of Object.entries(extraPages)) {
  const name = file.split('/').pop().replace(/\.jsx$/, '')
  if (mod.default) routes['/' + name.toLowerCase()] = mod.default
}

const path = window.location.pathname
const Page = routes[path] ?? (path === '/about' ? About : App)

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <Page />
  </StrictMode>
)
