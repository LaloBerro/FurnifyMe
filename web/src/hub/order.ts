import type { HubEntryData } from './api';

// The hub's view rules, the Qt hub's own: Recent is newest edit first with a
// never-saved entry last, Name is case-insensitive, and the id breaks every
// tie so equal keys keep one order. The search is trimmed and
// case-insensitive, and an empty one keeps everything.
export function ordered(entries: HubEntryData[], byName: boolean): HubEntryData[] {
  const byId = (a: HubEntryData, b: HubEntryData) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0);
  return [...entries].sort((a, b) => {
    if (byName) {
      const c = a.name.localeCompare(b.name, undefined, { sensitivity: 'accent' });
      return c !== 0 ? c : byId(a, b);
    }
    if ((a.edited > 0) !== (b.edited > 0)) return a.edited > 0 ? -1 : 1;
    if (a.edited !== b.edited) return b.edited - a.edited;
    return byId(a, b);
  });
}

export function matches(name: string, query: string): boolean {
  const needle = query.trim().toLowerCase();
  return needle === '' || name.toLowerCase().includes(needle);
}
