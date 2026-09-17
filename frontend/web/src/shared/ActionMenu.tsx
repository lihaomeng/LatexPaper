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
  return <details name="workbench-actions" ref={root} className={"action-menu " + className}
    onBlur={event => {
      if (event.relatedTarget instanceof Node && !event.currentTarget.contains(event.relatedTarget))
        root.current?.removeAttribute("open");
    }}
    onKeyDown={event => {
      if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
        event.preventDefault();
        root.current?.setAttribute("open", "");
        const items = Array.from(root.current?.querySelectorAll<HTMLButtonElement>(".action-menu-content button:not(:disabled)") ?? []);
        const index = items.indexOf(event.target as HTMLButtonElement);
        const next = event.key === "Home" ? 0 : event.key === "End" ? items.length - 1 :
          event.key === "ArrowDown" ? (index + 1) % items.length : (index < 0 ? items.length - 1 : (index - 1 + items.length) % items.length);
        items[next]?.focus();
      }
      if (event.key === "Escape") {
        root.current?.removeAttribute("open");
        root.current?.querySelector("summary")?.focus();
      }
    }}>
    <summary title={title} aria-label={title}>{label}</summary>
    <div className="action-menu-content" aria-label={title} onClick={event => {
      if ((event.target as Element).closest("button:not(:disabled)")) {
        const restoreFocus = root.current?.contains(document.activeElement);
        root.current?.removeAttribute("open");
        if (restoreFocus) root.current?.querySelector("summary")?.focus();
      }
    }}>{children}</div>
  </details>;
}
