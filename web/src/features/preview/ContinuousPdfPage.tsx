import { useEffect, useRef, useState, type RefObject } from "react";
import type { PDFDocumentProxy } from "pdfjs-dist";
import { usePdfPageRaster } from "./usePdfPageRaster";

// Keep page geometry in the document flow, but retain rasters only near the viewport.
export function ContinuousPdfPage({ document, number, scale, deviceRatio, width, height, root, onReverse }: {
  document: PDFDocumentProxy; number: number; scale: number; deviceRatio: number;
  width: number; height: number; root: RefObject<HTMLDivElement | null>;
  onReverse?: (page: number, x: number, y: number) => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [visible, setVisible] = useState(false);
  const error = usePdfPageRaster({ document, number, scale, deviceRatio, canvas, enabled: visible });
  useEffect(() => {
    if (!canvas.current) return;
    const observer = new IntersectionObserver(entries => setVisible(entries[0].isIntersecting),
      { root: root.current, rootMargin: "600px 0px" });
    observer.observe(canvas.current);
    return () => observer.disconnect();
  }, [root]);
  return <><canvas ref={canvas} aria-label={`PDF 第 ${number} 页`} style={{ width, height }}
    title={onReverse ? "双击跳转到源码" : undefined}
    onDoubleClick={event => {
      const rect = event.currentTarget.getBoundingClientRect();
      if (rect.width <= 0 || rect.height <= 0) return;
      onReverse?.(number, (event.clientX - rect.left) * width / rect.width / scale,
        (event.clientY - rect.top) * height / rect.height / scale);
    }} />{error && <span className="pdf-page-error" role="status">第 {number} 页渲染失败：{error}</span>}</>;
}
