import { useEffect, useRef, useState, type RefObject } from "react";
import type { PDFDocumentProxy } from "pdfjs-dist";
import { publishRaster, renderPageRaster } from "./pdfRaster";

// Keep page geometry in the document flow, but retain rasters only near the viewport.
export function ContinuousPdfPage({ document, number, scale, deviceRatio, width, height, root, onReverse }: {
  document: PDFDocumentProxy; number: number; scale: number; deviceRatio: number;
  width: number; height: number; root: RefObject<HTMLDivElement | null>;
  onReverse?: (page: number, x: number, y: number) => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [visible, setVisible] = useState(false);
  const [error, setError] = useState("");
  useEffect(() => {
    if (!canvas.current) return;
    const observer = new IntersectionObserver(entries => setVisible(entries[0].isIntersecting),
      { root: root.current, rootMargin: "600px 0px" });
    observer.observe(canvas.current);
    return () => observer.disconnect();
  }, [root]);
  useEffect(() => {
    const element = canvas.current;
    if (!element) return;
    if (!visible) { element.width = 0; element.height = 0; return; }
    let cancelled = false;
    let raster: ReturnType<typeof renderPageRaster> | undefined;
    setError("");
    void document.getPage(number).then(async page => {
      if (cancelled) return;
      raster = renderPageRaster(page, scale, deviceRatio);
      try {
        await raster.task.promise;
        if (!cancelled) publishRaster(element, raster);
      } finally { raster.canvas.width = 0; raster.canvas.height = 0; }
    }).catch(failure => {
      if (!cancelled && (failure as Error).name !== "RenderingCancelledException")
        setError((failure as Error).message || "PDF_RENDER_FAILED");
    });
    return () => { cancelled = true; raster?.task.cancel(); element.width = 0; element.height = 0; };
  }, [document, number, scale, deviceRatio, visible]);
  return <><canvas ref={canvas} aria-label={`PDF 第 ${number} 页`} style={{ width, height }}
    title={onReverse ? "双击跳转到源码" : undefined}
    onDoubleClick={event => {
      const rect = event.currentTarget.getBoundingClientRect();
      if (rect.width <= 0 || rect.height <= 0) return;
      onReverse?.(number, (event.clientX - rect.left) * width / rect.width / scale,
        (event.clientY - rect.top) * height / rect.height / scale);
    }} />{error && <span className="pdf-page-error" role="status">第 {number} 页渲染失败：{error}</span>}</>;
}
