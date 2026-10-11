// The vocabulary law for pages: gui_smoke's usesBannedWord(), ported rule for
// rule. Case-insensitive substring match; "round" and "flatten" only where no
// letter or digit precedes them (so "background" passes); a "Join" followed by
// "t" is the app's own word "Joint" and passes.
export const bannedWords = [
  'Fuse', 'Solid', 'OCCT', 'mm3', '(s)', 'Merge', 'Join', 'bevel',
  'round', 'flatten', 'symmetry', 'kernel', 'engine',
];

const needsBoundary = (word: string) => ['round', 'flatten'].includes(word.toLowerCase());

export function usesBannedWord(text: string, word: string): boolean {
  const hay = text.toLowerCase();
  const needle = word.toLowerCase();
  for (let at = hay.indexOf(needle); at >= 0; at = hay.indexOf(needle, at + 1)) {
    if (needle === 'join' && hay[at + needle.length] === 't') continue;
    if (!needsBoundary(word)) return true;
    if (at === 0 || !/[\p{L}\p{N}]/u.test(text[at - 1])) return true;
  }
  return false;
}

/** Every banned word found in any of `texts`, as the matched text spells it. */
export function bannedWordsIn(texts: string[]): string[] {
  const found: string[] = [];
  for (const text of texts) {
    for (const word of bannedWords) {
      if (!usesBannedWord(text, word)) continue;
      const at = text.toLowerCase().indexOf(word.toLowerCase());
      found.push(text.slice(at, at + word.length));
    }
  }
  return found;
}
