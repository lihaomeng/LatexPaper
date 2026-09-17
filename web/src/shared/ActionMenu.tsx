import { useEffect, useRef, type ReactNode } from "react";

export function ActionMenu({ label, children, className = "", title }: {
  label: ReactNode; children: ReactNode; className?: string; title: string;
}) {
  const root = useRef<HTMLDetailsElement>(null);
  useEffect(() => {
    const dismiss = (event: PointerEvent) => {
      if (event.target instanceof Node && !root.current?.contains(event.target))
        root.current?.removeAttribute("open");
    };
    document.addEventListener("pointerdown", dismiss);
    return () => document.removeEventListener("pointerdown", dismiss);
  }, []);
  return <details ref={root} className={"action-menu " + className}
    onKeyDown={event => {
      if (event.key === "Escape") {
        root.current?.removeAttribute("open");
        root.current?.querySelector("summary")?.focus();
      }
    }}>
    <summary title={title} aria-label={title}>{label}</summary>
    <div className="action-menu-content" aria-label={title} onClick={event => {
      if ((event.target as Element).closest("button:not(:disabled)"))
        root.current?.removeAttribute("open");
    }}>{children}</div>
  </details>;
}
