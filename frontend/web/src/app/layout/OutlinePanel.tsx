import { useId, useState } from "react";
import { Icon } from "../../shared/Icon";
export function OutlinePanel({ entries, activeLine, onSelect }: {
  entries: { title: string; line: number; level: number }[]; activeLine: number | null;
  onSelect(line: number): void;
}) {
  const contentId = useId();
  const [expanded, setExpanded] = useState<boolean | null>(null);
  const [query, setQuery] = useState("");
  const currentLine = entries.filter(item => item.line <= (activeLine ?? 0)).at(-1)?.line;
  const visible = entries.filter(item => item.title.toLowerCase().includes(query.trim().toLowerCase()));
  const open = expanded ?? entries.length > 0;
  return <section className={"outline-panel " + (open ? "expanded" : "")} aria-label="文档大纲">
    <button className="panel-heading outline-toggle" aria-expanded={open} aria-controls={contentId} onClick={() => setExpanded(!open)}>
      <span className="outline-heading-label"><span className={open ? "section-chevron expanded" : "section-chevron"}><Icon name="chevron" size={14} /></span>文档大纲</span>
      <small>{entries.length || "暂无章节"}</small>
    </button>
    {open && <div id={contentId} className="outline-list">
      {(entries.length > 5 || query) && <input className="outline-filter" aria-label="筛选章节" placeholder="查找章节…" value={query} onChange={event => setQuery(event.target.value)} />}
      {visible.map(item => <button key={item.line} className={"outline-item " + (currentLine === item.line ? "selected" : "")}
        aria-current={currentLine === item.line ? "location" : undefined}
        style={{ paddingLeft: 12 + Math.max(0, item.level - 1) * 12 }} title={item.title}
        onClick={() => onSelect(item.line)}>
        <span className="outline-marker" aria-hidden="true">{item.level <= 1 ? "§" : "·"}</span><span className="truncate">{item.title}</span>
      </button>)}
      {entries.length > 0 && !visible.length && <p className="empty-small muted">没有匹配的章节。</p>}
      {!entries.length && <p className="empty-small muted">当前文件没有可导航的章节。</p>}
    </div>}
  </section>;
}
