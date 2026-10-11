import type { Bridge, Rect } from './bridge';

// The page's root takes no pointer; only elements marked data-hit do. C++ cannot
// ask the page "is there a card here?" synchronously, so the page TELLS it,
// whenever layout changes, which rectangles take the pointer - and C++ answers
// every press from that list on its own side, with no hop to the browser.
export function reportHitsOnChange(bridge: Bridge) {
  let scheduled = false;
  let last = '';

  const send = () => {
    scheduled = false;
    const rects: Rect[] = [];
    for (const el of document.querySelectorAll<HTMLElement>('[data-hit]')) {
      if (el.dataset.hit === 'off') continue;
      const r = el.getBoundingClientRect();
      if (r.width <= 0 || r.height <= 0) continue;
      // Outward to whole pixels, so an edge pixel the user can see is never
      // handed to the viewport.
      const x = Math.floor(r.left);
      const y = Math.floor(r.top);
      rects.push({ x, y, w: Math.ceil(r.right) - x, h: Math.ceil(r.bottom) - y });
    }
    const key = JSON.stringify(rects);
    if (key === last) return;
    last = key;
    bridge.reportHitRects(rects);
  };

  const schedule = () => {
    if (scheduled) return;
    scheduled = true;
    requestAnimationFrame(send);
  };

  const resize = new ResizeObserver(schedule);
  const observeAll = () => {
    for (const el of document.querySelectorAll<HTMLElement>('[data-hit]')) resize.observe(el);
  };
  new MutationObserver(() => {
    observeAll();
    schedule();
  }).observe(document.body, { subtree: true, childList: true, attributes: true });
  window.addEventListener('resize', schedule);
  // Transitions move a card without resizing it or mutating the DOM.
  document.addEventListener('transitionend', schedule, true);
  observeAll();
  schedule();
  return schedule;
}
