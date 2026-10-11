// Every word the hub page can show, in one place - the vocabulary sweep reads
// allCopy(), and the strings are the Qt hub's own (SelectorWindow.cpp,
// NameFurnitureCard.cpp) wherever the Qt hub had one.

export const copy = {
  wordmark: 'FurnifyMe',
  title: 'Your furniture',
  search: 'Search',
  clearSearch: 'Clear search',
  sortRecent: 'Recent',
  sortName: 'Name',
  newFurniture: '+ New furniture',
  scenesHeading: 'Scenes',
  newScene: '+ New scene',
  rename: 'Rename',
  delete: 'Delete',
  deleteArmed: 'Delete — click again',
  neverSaved: 'Never saved',
  openCard: (name: string) => `Open ${name}`,
  renameField: (name: string) => `New name for ${name}`,
  noMatch: (query: string) => `Nothing matches "${query}"`,
  usedInAll: (listed: string) => `Used in ${listed}`,
  usedInPair: (head: string, last: string) => `${head} and ${last}`,
  usedInMore: (listed: string, more: number) => `Used in ${listed} and ${more} more`,
  usedInOne: 'Used in 1 scene',
  usedInCount: (n: number) => `Used in ${n} scenes`,
  nameTitle: 'Name this furniture',
  nameBody: 'You can rename it later from the library.',
  cancel: 'Cancel',
  create: 'Create',
  escKey: 'Esc',
  enterKey: 'Enter',
  minimize: 'Minimize',
  maximize: 'Maximize',
  restore: 'Restore',
  close: 'Close',
};

/** Every string above, templates filled with a neutral word, for the sweep. */
export function allCopy(): string[] {
  const out: string[] = [];
  for (const value of Object.values(copy)) {
    if (typeof value === 'string') out.push(value);
    else out.push((value as (...a: unknown[]) => string)('x', 'x'));
  }
  return out;
}
