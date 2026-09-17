import { useEffect, useRef, useState } from "react";
import { getDocument, GlobalWorkerOptions, type PDFDocumentProxy, type PDFDocumentLoadingTask } from "pdfjs-dist";
import workerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";
import { useDevicePixelRatio } from "../../shared/useDevicePixelRatio";
import { renderPageRaster, publishRaster } from "./pdfRaster";
import type { PdfTarget } from "./model";
GlobalWorkerOptions.workerSrc = workerUrl;
export function PdfCanvas({ data, onReverse, onCommit, onReject, target, zoom, onZoomChange }: {
  data: Uint8Array;
  onReverse?: (page: number, x: number, y: number) => void;
  onCommit?: () => void;
  onReject?: (message: string) => void;
  target?: PdfTarget | null;
  zoom: number;
  onZoomChange?(zoom: number): void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const marker = useRef<HTMLSpanElement>(null);
  const committedDocument = useRef<PDFDocumentProxy | null>(null);
  const committedTask = useRef<PDFDocumentLoadingTask | null>(null);
  const commitCallback = useRef(onCommit);
  const rejectCallback = useRef(onReject);
  commitCallback.current = onCommit;
  rejectCallback.current = onReject;
  const [document, setDocument] = useState<PDFDocumentProxy | null>(null);
  const [page, setPage] = useState(1);
  const [rendered, setRendered] = useState(0);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);
  const scale = zoom / 100;
  const deviceRatio = useDevicePixelRatio();
  const displaySettings = useRef({ scale, deviceRatio });
  displaySettings.current = { scale, deviceRatio };
  const [frame, setFrame] = useState<{ page: number; scale: number; width: number; height: number } | null>(null);

  useEffect(() => {
    const task = getDocument({ data: data.slice() });
    let active = true;
    let committed = false;
    let candidateRender: { cancel(): void; promise: Promise<void> } | undefined;
    setLoading(true);
    void task.promise.then(async candidate => {
      const firstPage = await candidate.getPage(1);
      // Do not publish a low-resolution first frame. If zoom/DPI changed while
      // loading, finish a new raster at the latest settings before committing.
      while (active) {
        const settings = displaySettings.current;
        const raster = renderPageRaster(firstPage, settings.scale, settings.deviceRatio);
        candidateRender = raster.task;
        try {
          await raster.task.promise;
          if (!active || !canvas.current) return;
          if (settings.scale !== displaySettings.current.scale ||
              settings.deviceRatio !== displaySettings.current.deviceRatio) continue;
          publishRaster(canvas.current, raster);
          setFrame({ page: 1, scale: settings.scale, width: raster.viewport.width, height: raster.viewport.height });
          break;
        } finally { raster.canvas.width = 0; raster.canvas.height = 0; }
      }
      if (!active || !canvas.current) return;
      const previousTask = committedTask.current;
      committedDocument.current = candidate;
      committedTask.current = task;
      committed = true;
      setDocument(candidate);
      setPage(1);
      setError("");
      setLoading(false);
      setRendered(value => value + 1);
      commitCallback.current?.();
      if (previousTask && previousTask !== task) void previousTask.destroy();
    }).catch(failure => {
      if (!active || (failure as Error).name === "RenderingCancelledException") return;
      const message = (failure as Error).message || "PDF_RENDER_FAILED";
      setLoading(false);
      setError(message);
      rejectCallback.current?.(message);
    });
    return () => {
      active = false;
      candidateRender?.cancel();
      if (!committed) void task.destroy();
    };
  }, [data]);

  useEffect(() => () => { if (committedTask.current) void committedTask.current.destroy(); }, []);
  useEffect(() => {
    if (target && document) setPage(Math.min(document.numPages, Math.max(1, target.page)));
  }, [document, target]);
  useEffect(() => {
    if (!document || !canvas.current) return;
    let cancelled = false;
    let render: { cancel(): void; promise: Promise<void> } | undefined;
    void document.getPage(page).then(async pdfPage => {
      if (cancelled || committedDocument.current !== document || !canvas.current) return;
      const raster = renderPageRaster(pdfPage, scale, deviceRatio);
      render = raster.task;
      try {
        await render.promise;
        if (cancelled || committedDocument.current !== document || !canvas.current) return;
        publishRaster(canvas.current, raster);
        setFrame({ page, scale, width: raster.viewport.width, height: raster.viewport.height });
      } finally { raster.canvas.width = 0; raster.canvas.height = 0; }
      setError("");
      setRendered(value => value + 1);
    }).catch(failure => {
      if (!cancelled && (failure as Error).name !== "RenderingCancelledException")
        setError((failure as Error).message || "PDF_RENDER_FAILED");
    });
    return () => { cancelled = true; render?.cancel(); };
  }, [document, page, scale, deviceRatio]);
  useEffect(() => {
    if (target?.page === page) marker.current?.scrollIntoView({ block: "center", inline: "center" });
  }, [page, rendered, scale, target]);

  if (!document && error) return <div className="preview-empty"><p role="alert">PDF.js 无法显示此产物：{error}</p></div>;
  return <div className="pdf-viewer">
    {(loading || error) && <div className={"pdf-render-status " + (error ? "error" : "loading")} role="status">
      {error ? `新预览渲染失败，继续显示上一份成功 PDF：${error}` : "正在验证并渲染新 PDF，当前预览保持不变…"}
    </div>}
    <div className="pdf-controls">
      <button disabled={page <= 1} onClick={() => setPage(value => value - 1)}>上一页</button>
      <span>{page} / {document?.numPages ?? "—"}</span>
      <button disabled={!document || page >= document.numPages} onClick={() => setPage(value => value + 1)}>下一页</button>
      <span className="zoom-controls"><button disabled={zoom <= 50} onClick={() => onZoomChange?.(Math.max(50, zoom - 25))}>−</button>
        <button title="恢复 100%" onClick={() => onZoomChange?.(100)}>{zoom}%</button>
        <button disabled={zoom >= 300} onClick={() => onZoomChange?.(Math.min(300, zoom + 25))}>＋</button></span>
    </div><div className="pdf-scroll"><div className="pdf-page"><canvas ref={canvas} aria-label={"PDF 第 " + page + " 页"}
      title={onReverse ? "双击跳转到源码" : undefined}
      onDoubleClick={event => { const rect = event.currentTarget.getBoundingClientRect();
        if (!frame || frame.page !== page || rect.width <= 0 || rect.height <= 0) return;
        // SyncTeX uses logical page units, never backing-store/device pixels.
        const ratioX = frame.width / rect.width / frame.scale;
        const ratioY = frame.height / rect.height / frame.scale;
        onReverse?.(frame.page, (event.clientX - rect.left) * ratioX, (event.clientY - rect.top) * ratioY); }} />
      {frame && target?.page === frame.page && frame.page === page && <span ref={marker} className="synctex-marker" aria-label="源码定位位置"
        style={{ left: target.x * frame.scale, top: target.y * frame.scale }} />}</div></div></div>;
}