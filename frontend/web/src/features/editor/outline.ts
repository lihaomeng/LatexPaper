export interface OutlineEntry { title: string; line: number; level: number }
/** Bounded structural navigation, not a TeX parser. Comments/verbatim are ignored. */
export function extractOutline(source: string): OutlineEntry[] {
  const entries: OutlineEntry[] = [];
  const levels: Record<string, number> = { part: 0, chapter: 0, section: 1, subsection: 2, subsubsection: 3 };
  let verbatim = false;
  for (const [index, raw] of source.slice(0, 1_000_000).split("\n").entries()) {
    let line = "";
    let escaped = false;
    for (const character of raw) {
      if (character === "%" && !escaped) break;
      line += character;
      escaped = character === "\\" ? !escaped : false;
    }
    if (/\\end\{(?:verbatim|lstlisting|minted)\}/.test(line)) { verbatim = false; continue; }
    if (verbatim) continue;
    if (/\\begin\{(?:verbatim|lstlisting|minted)\}/.test(line)) { verbatim = true; continue; }
    const match = /\\(part|chapter|section|subsection|subsubsection)\*?\s*(?:\[[^\]]*\])?\s*\{/.exec(line);
    if (!match) continue;
    let depth = 1;
    let title = "";
    for (let cursor = match.index + match[0].length; cursor < line.length; cursor++) {
      const character = line[cursor];
      if (character === "{") depth++;
      if (character === "}") depth--;
      if (depth === 0) break;
      title += character;
    }
    if (depth === 0 && title.trim()) entries.push({ title: title.trim(), line: index + 1, level: levels[match[1]] });
    if (entries.length >= 500) break;
  }
  return entries;
}

