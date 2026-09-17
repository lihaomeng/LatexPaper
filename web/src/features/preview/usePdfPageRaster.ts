import { useEffect, useRef, useState, type RefObject } from "react";
import type { PDFDocumentProxy } from "pdfjs-dist";
import { publishRaster, renderPageRaster } from "./pdfRaster";

export type PublishedPage = { document: PDFDocumentProxy; number: number; scale: number; deviceRatio: number };
export function usePdfPageRaster({ document, number, scale, deviceRatio, canvas, enabled = true,
  preserve = false, published, isCurrent, onPublished }: {
  document: PDFDocumentProxy | null; number: number; scale: number; deviceRatio: number;
  canvas: RefObject<HTMLCanvasElement | null>; enabled?: boolean; preserve?: boolean;
  published?: RefObject<PublishedPage | null>; isCurrent?: () => boolean;
  onPublished?: (frame: { scale: number; width: number; height: number }) => void;
}) {
  const [error, setError] = useState("");
  const callbacks = useRef({ isCurrent, onPublished });
  callbacks.current = { isCurrent, onPublished };
  useEffect(() => {
    const element = canvas.current;
    if (!element || !document) return;
    if (!enabled) {
      if (!preserve) { element.width = 0; element.height = 0; }
      return;
    }
    const seed = published?.current;
    if (seed?.document === document && seed.number === number && seed.scale === scale && seed.deviceRatio === deviceRatio) {
      setError(""); return;
    }
    let cancelled = false;
    let raster: ReturnType<typeof renderPageRaster> | undefined;
    const validate = callbacks.current.isCurrent;
    const current = () => !cancelled && (validate?.() ?? true);
    setError("");
    void document.getPage(number).then(async page => {
      if (!current()) return;
      raster = renderPageRaster(page, scale, deviceRatio);
      try {
        await raster.task.promise;
        if (!current()) return;
        publishRaster(element, raster);
        if (published) published.current = { document, number, scale, deviceRatio };
        callbacks.current.onPublished?.({ scale, width: raster.viewport.width, height: raster.viewport.height });
      } finally { raster.canvas.width = 0; raster.canvas.height = 0; }
    }).catch(failure => {
      if (current() && (failure as Error).name !== "RenderingCancelledException")
        setError((failure as Error).message || "PDF_RENDER_FAILED");
    });
    return () => {
      cancelled = true; raster?.task.cancel();
      if (!preserve) { element.width = 0; element.height = 0; }
    };
  }, [document, number, scale, deviceRatio, canvas, enabled, preserve, published]);
  return error;
}
