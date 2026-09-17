import { useState } from "react";
export function OutlinePanel({ entries, activeLine, onSelect }: {
  entries: { title: string; line: number; level: number }[]; activeLine: number | null;
  onSelect(line: number): void;
}) {
  const [expanded, setExpanded] = useState<boolean | null>(null);
  const open = expanded ?? entries.length > 0;
  return <section className={"outline-panel " + (open ? "expanded" : "")} aria-label="文档大纲">
    <button className="panel-heading outline-toggle" aria-expanded={open} onClick={() => setExpanded(!open)}>
      <span><span className="down-chevron">{open ? "⌄" : "›"}</span>文档大纲</span>
      <small>{entries.length || "暂无章节"}</small>
    </button>
    {open && <div className="outline-list">
      {entries.map(item => <button key={item.line} className={"outline-item " + (activeLine === item.line ? "selected" : "")}
        style={{ paddingLeft: 12 + Math.max(0, item.level - 1) * 12 }} title={item.title}
        onClick={() => onSelect(item.line)}>
        <span className="outline-marker">{item.level <= 1 ? "⌄" : "·"}</span><span className="truncate">{item.title}</span>
      </button>)}
      {!entries.length && <p className="empty-small muted">当前文件没有可导航的章节。</p>}
    </div>}
  </section>;
}
