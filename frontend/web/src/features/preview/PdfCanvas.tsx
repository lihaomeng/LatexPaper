import { useEffect, useRef, useState } from "react";
import { getDocument, GlobalWorkerOptions, type PDFDocumentProxy, type PDFDocumentLoadingTask } from "pdfjs-dist";
import workerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";
import { useDevicePixelRatio } from "../../shared/useDevicePixelRatio";
import { renderPageRaster, publishRaster } from "./pdfRaster";
import { usePdfPageRaster, type PublishedPage } from "./usePdfPageRaster";
import { ContinuousPdfPage } from "./ContinuousPdfPage";
import type { PdfTarget, PdfZoomMode } from "./model";
GlobalWorkerOptions.workerSrc = workerUrl;
export function PdfCanvas({ data, onReverse, onCommit, onReject, target, zoom, onZoomChange, zoomMode = "width", onZoomModeChange }: {
  data: Uint8Array;
  onReverse?: (page: number, x: number, y: number) => void;
  onCommit?: () => void;
  onReject?: (message: string) => void;
  target?: PdfTarget | null;
  zoom: number;
  onZoomChange?(zoom: number): void;
  zoomMode?: PdfZoomMode;
  onZoomModeChange?(mode: PdfZoomMode): void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const scroll = useRef<HTMLDivElement>(null);
  const [bounds, setBounds] = useState({ width: 0, height: 0 });
  const [pageText, setPageText] = useState("1");
  const [zoomText, setZoomText] = useState(String(zoom));
  useEffect(() => setZoomText(String(zoom)), [zoom]);
  useEffect(() => {
    const element = scroll.current;
    if (!element) return;
    let tick = 0;
    const measure = () => {
      cancelAnimationFrame(tick);
      tick = requestAnimationFrame(() => setBounds(previous => {
        const width = element.clientWidth, height = element.clientHeight;
        return previous.width === width && previous.height === height ? previous : { width, height };
      }));
    };
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    measure();
    return () => { observer.disconnect(); cancelAnimationFrame(tick); };
  }, []);
  const marker = useRef<HTMLSpanElement>(null);
  const published = useRef<PublishedPage | null>(null);
  const committedDocument = useRef<PDFDocumentProxy | null>(null);
  const committedTask = useRef<PDFDocumentLoadingTask | null>(null);
  const commitCallback = useRef(onCommit);
  const rejectCallback = useRef(onReject);
  commitCallback.current = onCommit;
  rejectCallback.current = onReject;
  const [sizes, setSizes] = useState<{ width: number; height: number }[]>([]);
  const [document, setDocument] = useState<PDFDocumentProxy | null>(null);
  const [page, setPage] = useState(1);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);
  const deviceRatio = useDevicePixelRatio();
  const displaySettings = useRef({ zoom, zoomMode, deviceRatio, ...bounds });
  displaySettings.current = { zoom, zoomMode, deviceRatio, ...bounds };
  const settingsKey = (settings: typeof displaySettings.current) =>
    [settings.zoom, settings.zoomMode, settings.deviceRatio, settings.width, settings.height].join(":");
  const pageScale = (pdfPage: { getViewport(options: { scale: number }): { width: number; height: number } },
    settings: typeof displaySettings.current) => {
    if (settings.zoomMode === "custom") return settings.zoom / 100;
    const base = pdfPage.getViewport({ scale: 1 });
    const fitWidth = Math.max(1, (settings.width || 600) - 32) / base.width;
    const fitHeight = Math.max(1, (settings.height || 800) - 32) / base.height;
    return Math.max(.1, Math.min(3, settings.zoomMode === "page" ? Math.min(fitWidth, fitHeight) : fitWidth));
  };
  const [frame, setFrame] = useState<{ page: number; scale: number; width: number; height: number } | null>(null);

  useEffect(() => {
    const task = getDocument({ data: data.slice() });
    let active = true;
    let committed = false;
    let candidateRender: { cancel(): void; promise: Promise<void> } | undefined;
    setLoading(true);
    void task.promise.then(async candidate => {
      const firstPage = await candidate.getPage(1);
      const pageSizes: { width: number; height: number }[] = [];
      for (let number = 1; number <= candidate.numPages; number++) {
        if (!active) return;
        const pdfPage = number === 1 ? firstPage : await candidate.getPage(number);
        const viewport = pdfPage.getViewport({ scale: 1 });
        pageSizes.push({ width: viewport.width, height: viewport.height });
      }
      // Do not publish a low-resolution first frame. If zoom/DPI changed while
      // loading, finish a new raster at the latest settings before committing.
      while (active) {
        const settings = displaySettings.current;
        const scale = pageScale(firstPage, settings);
        const raster = renderPageRaster(firstPage, scale, settings.deviceRatio);
        candidateRender = raster.task;
        try {
          await raster.task.promise;
          if (!active || !canvas.current) return;
          if (settingsKey(settings) !== settingsKey(displaySettings.current)) continue;
          publishRaster(canvas.current, raster);
          published.current = { document: candidate, number: 1, scale, deviceRatio: settings.deviceRatio };
          setFrame({ page: 1, scale, width: raster.viewport.width, height: raster.viewport.height });
          break;
        } finally { raster.canvas.width = 0; raster.canvas.height = 0; }
      }
      if (!active || !canvas.current) return;
      const previousTask = committedTask.current;
      committedDocument.current = candidate;
      committedTask.current = task;
      committed = true;
      setSizes(pageSizes);
      setDocument(candidate);
      setPage(1);
      if (scroll.current) scroll.current.scrollTop = 0;
      setError("");
      setLoading(false);
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
  const firstScale = sizes[0] ? pageScale({ getViewport: () => sizes[0] }, displaySettings.current) : zoom / 100;
  const pageError = usePdfPageRaster({ document, number: 1, scale: firstScale, deviceRatio, canvas,
    preserve: true, published, isCurrent: () => committedDocument.current === document,
    onPublished: value => setFrame({ page: 1, ...value }) });
  const goToPage = (number: number) => {
    if (!document) return;
    const next = Math.max(1, Math.min(document.numPages, number));
    scroll.current?.querySelector<HTMLElement>(`[data-pdf-page="${next}"]`)
      ?.scrollIntoView({ block: "start", inline: "nearest" });
    setPage(next);
  };
  useEffect(() => {
    if (target && document) marker.current?.scrollIntoView({ block: "center", inline: "center" });
  }, [document, target, zoom, zoomMode, bounds.width, bounds.height, frame?.scale]);
  useEffect(() => {
    const element = scroll.current;
    if (!element || !document) return;
    let tick = 0;
    const update = () => {
      cancelAnimationFrame(tick);
      tick = requestAnimationFrame(() => {
        const top = element.getBoundingClientRect().top + 32;
        let current = 1;
        for (const item of Array.from(element.querySelectorAll<HTMLElement>("[data-pdf-page]"))) {
          if (item.getBoundingClientRect().top <= top) current = Number(item.dataset.pdfPage);
          else break;
        }
        setPage(current);
      });
    };
    element.addEventListener("scroll", update, { passive: true });
    update();
    return () => { cancelAnimationFrame(tick); element.removeEventListener("scroll", update); };
  }, [document, zoom, zoomMode, bounds.width, bounds.height]);

  useEffect(() => { setPageText(String(page)); }, [page]);
  const effectiveZoom = Math.round((frame?.scale ?? zoom / 100) * 100);
  const changeZoom = (value: number) => {
    onZoomModeChange?.("custom");
    onZoomChange?.(Math.min(300, Math.max(50, value)));
  };
  const commitZoom = () => {
    const value = Number(zoomText);
    const next = zoomText.trim() && Number.isFinite(value) ? Math.round(Math.min(300, Math.max(50, value))) : zoom;
    changeZoom(next); setZoomText(String(next));
  };
  const jump = () => {
    const requested = Number(pageText);
    const next = document && Number.isInteger(requested)
      ? Math.max(1, Math.min(document.numPages, requested)) : page;
    goToPage(next); setPageText(String(next));
  };
  return <div className="pdf-viewer">
    {(loading || error || pageError) && <div className={"pdf-render-status " + (error || pageError ? "error" : "loading")} role="status">
      {error ? `新预览渲染失败，继续显示上一份成功 PDF：${error}` : pageError ? `页面渲染失败：${pageError}` : "正在验证并渲染新 PDF，当前预览保持不变…"}
    </div>}
    <div className="pdf-controls">
      <div className="page-controls">
        <button aria-label="上一页" title="上一页" disabled={page <= 1} onClick={() => goToPage(page - 1)}>‹</button>
        <input className="page-number" aria-label="跳转到 PDF 页码" inputMode="numeric" value={pageText}
          disabled={!document} onChange={event => setPageText(event.target.value)} onBlur={jump}
          onKeyDown={event => { if (event.key === "Enter") { jump(); event.currentTarget.blur(); }
            if (event.key === "Escape") setPageText(String(page)); }} />
        <span className="page-total">/ {document?.numPages ?? "—"}</span>
        <button aria-label="下一页" title="下一页" disabled={!document || page >= document.numPages}
          onClick={() => goToPage(page + 1)}>›</button>
      </div>
      <div className="zoom-controls">
        <button aria-label="缩小 PDF" title="缩小" disabled={effectiveZoom <= 50} onClick={() => changeZoom(effectiveZoom - 25)}>−</button>
        <select aria-label="PDF 缩放模式" value={zoomMode}
          onChange={event => onZoomModeChange?.(event.target.value as PdfZoomMode)}>
          <option value="width">适合宽度</option><option value="page">整页显示</option>
          <option value="custom">{zoomMode === "custom" ? zoom + "%" : "自定义比例"}</option>
        </select>
        {zoomMode === "custom" && <input className="zoom-number" type="number" min={50} max={300} step={10}
          aria-label="PDF 缩放百分比" value={zoomText} onChange={event => setZoomText(event.target.value)}
          onBlur={commitZoom} onKeyDown={event => {
            if (event.key === "Enter") event.currentTarget.blur();
            if (event.key === "Escape") setZoomText(String(zoom));
          }} />}
        <button aria-label="放大 PDF" title="放大" disabled={effectiveZoom >= 300} onClick={() => changeZoom(effectiveZoom + 25)}>+</button>
      </div>
    </div><div className="pdf-scroll pdf-continuous" ref={scroll} aria-label="连续 PDF 页面">
      <div className="pdf-page" data-pdf-page="1"><canvas ref={canvas} aria-label="PDF 第 1 页"
        title={onReverse ? "双击跳转到源码" : undefined}
        onDoubleClick={event => {
          const rect = event.currentTarget.getBoundingClientRect();
          if (!frame || rect.width <= 0 || rect.height <= 0) return;
          onReverse?.(1, (event.clientX - rect.left) * frame.width / rect.width / frame.scale,
            (event.clientY - rect.top) * frame.height / rect.height / frame.scale);
        }} />
        {frame && target?.page === 1 && <span ref={marker} className="synctex-marker" aria-label="源码定位位置"
          style={{ left: target.x * frame.scale, top: target.y * frame.scale }} />}
      </div>
      {document && sizes.slice(1).map((size, index) => {
        const number = index + 2;
        const scale = pageScale({ getViewport: () => size }, displaySettings.current);
        return <div className="pdf-page" data-pdf-page={number} key={number}
          style={{ width: size.width * scale, height: size.height * scale }}>
          <ContinuousPdfPage document={document} number={number} scale={scale} deviceRatio={deviceRatio}
            width={size.width * scale} height={size.height * scale} root={scroll} onReverse={onReverse} />
          {target?.page === number && <span ref={marker} className="synctex-marker" aria-label="源码定位位置"
            style={{ left: target.x * scale, top: target.y * scale }} />}
        </div>;
      })}
    </div></div>;
}
