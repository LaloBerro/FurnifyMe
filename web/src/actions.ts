// `use:chip` - a control that never takes keyboard focus (CLAUDE.md F17, the
// user's "NoFocus on chips"): out of the tab order, and a press that does not
// move focus, so a click on a sort chip leaves the caret in the search field
// and an armed Delete cannot be confirmed by a stray Enter. Typing fields do
// not wear it; they keep their focus.
export function chip(node: HTMLElement) {
  node.tabIndex = -1;
  const keep = (event: MouseEvent) => event.preventDefault();
  node.addEventListener('mousedown', keep);
  return {
    destroy() {
      node.removeEventListener('mousedown', keep);
    },
  };
}
