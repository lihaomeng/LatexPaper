import { useState } from "react";
export function Splitter({ label, min, max, value, onChange }: {
  label: string; min: number; max: number; value: number; onChange(value: number): void;
}) {
  const [drag, setDrag] = useState<{ x: number; value: number } | null>(null);
  const update = (next: number) => onChange(Math.min(max, Math.max(min, next)));
  return <div className={"splitter " + (drag ? "dragging" : "")} role="separator" tabIndex={0} aria-label={label}
    aria-orientation="vertical" aria-valuemin={min} aria-valuemax={max} aria-valuenow={Math.round(value)}
    onPointerDown={event => { event.currentTarget.setPointerCapture(event.pointerId); setDrag({ x: event.clientX, value }); }}
    onPointerMove={event => { if (drag) update(drag.value + event.clientX - drag.x); }}
    onPointerUp={() => setDrag(null)} onLostPointerCapture={() => setDrag(null)}
    onKeyDown={event => {
      if (event.key === "ArrowLeft" || event.key === "ArrowRight") { event.preventDefault(); update(value + (event.key === "ArrowRight" ? 16 : -16)); }
    }} />;
}

