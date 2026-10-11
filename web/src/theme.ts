// Theme::Spec, as ThemeBridge hands it over: a map of CSS custom properties
// written onto :root, so every page reads the user's live look - and reads it
// again on every Theme change, which no page may cache across.
export function applyTokens(tokens: Record<string, string>): void {
  const root = document.documentElement.style;
  for (const [name, value] of Object.entries(tokens)) root.setProperty(name, value);
  // The theme in use (T-072): its whole style lives in themes.css under
  // :root[data-theme=...]. Graphite when a host says nothing.
  document.documentElement.dataset.theme = tokens['--theme'] || 'graphite';
}
