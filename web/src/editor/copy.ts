// Every word the editor's chrome page can show - the vocabulary sweep reads
// allCopy(). The Qt originals: Toast.cpp, UnsavedCloseCard.cpp.
export const copy = {
  undo: 'Undo',
  unsaved: 'unsaved changes',
  note: 'Note',
  failure: 'Failure',
  nameWithUnsaved: (name: string) => `${name} — unsaved changes`,
};

export function allCopy(): string[] {
  return Object.values(copy).map((v) => (typeof v === 'string' ? v : (v as (s: string) => string)('x')));
}
