export type IconName = "file" | "folder" | "search" | "plus" | "close" | "chevron" | "undo" | "redo" | "split" | "code" | "save" | "pdf" | "settings" | "check" | "wrap" | "info" | "leaf" | "play" | "warning" | "terminal";
const paths: Record<IconName, string> = {
  file: "M6 3h8l4 4v14H6z M14 3v5h4 M9 12h6 M9 16h6",
  folder: "M3 6h7l2 2h9v12H3z",
  search: "M16 16l5 5 M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0",
  plus: "M12 5v14 M5 12h14", close: "M6 6l12 12 M18 6L6 18",
  chevron: "M9 5l7 7-7 7", undo: "M8 5L3 10l5 5 M3 10h11a6 6 0 0 1 0 12",
  redo: "M16 5l5 5-5 5 M21 10H10a6 6 0 0 0 0 12",
  split: "M3 4h18v16H3z M12 4v16", code: "M8 6l-6 6 6 6 M16 6l6 6-6 6 M14 3l-4 18",
  save: "M4 3h13l4 4v14H3V3z M7 3v7h10V3 M7 21v-7h10v7",
  pdf: "M6 3h8l4 4v14H6z M14 3v5h4 M8 13h8 M8 16h6",
  settings: "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M12 2v3 M12 19v3 M2 12h3 M19 12h3 M5 5l2 2 M17 17l2 2 M19 5l-2 2 M7 17l-2 2",
  check: "M4 12l5 5L20 6", wrap: "M3 5h18 M3 10h14a4 4 0 0 1 0 8h-5 M15 15l-3 3 3 3 M3 16h4",
  info: "M12 8h.01 M12 11v6 M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0",
  leaf: "M19 3C9 2 3 8 5 15c2 6 11 6 13 0 1-3-1-7 1-12z M5 21c0-6 6-11 10-13",
  play: "M7 4l14 8-14 8z", warning: "M12 3L1 21h22z M12 9v5 M12 17h.01",
  terminal: "M4 6l5 5-5 5 M12 17h8",
};
export function Icon({ name, size = 16 }: { name: IconName; size?: number }) {
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6"
    strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d={paths[name]} /></svg>;
}

