// jsdom lays nothing out, so it has no ResizeObserver; the page only uses it to
// re-report geometry, which a test asserts through the first report instead.
class NoResizeObserver {
  observe() {}
  unobserve() {}
  disconnect() {}
}
(globalThis as Record<string, unknown>).ResizeObserver ??= NoResizeObserver;
