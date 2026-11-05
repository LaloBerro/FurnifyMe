// Theme::Spec, as ThemeBridge hands it over: a map of CSS custom properties
// written onto :root, so every page reads the user's live look - and reads it
// again on every Theme change, which no page may cache across.
//
// A theme's shell numbers (--shell-*, Phase 3i) are sent only by themes that
// have them, so a property the last map set and this one does not is REMOVED:
// left in place, Violet's 42 px chips would outlive a switch to Graphite.
const applied = new Set<string>();

export function applyTokens(tokens: Record<string, string>): void {
  const root = document.documentElement.style;
  for (const name of applied) if (!(name in tokens)) root.removeProperty(name);
  applied.clear();
  for (const [name, value] of Object.entries(tokens)) {
    root.setProperty(name, value);
    applied.add(name);
  }
  // The theme in use (T-072): its whole style lives in themes.css under
  // :root[data-theme=...]. Graphite when a host says nothing.
  document.documentElement.dataset.theme = tokens['--theme'] || 'graphite';
}
