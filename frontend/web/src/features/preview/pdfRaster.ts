import type { PDFPageProxy } from 'pdfjs-dist';

// Per surface: at most 64 MiB RGBA, excluding PDF.js internal resources.
const maxPixels = 16 * 1024 * 1024;
const maxDimension = 8192;

export function rasterSize(width: number, height: number, deviceRatio: number) {
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0)
    throw new Error('PDF_INVALID_PAGE_SIZE');
  const ratio = Number.isFinite(deviceRatio) && deviceRatio > 0 ? deviceRatio : 1;
  const outputScale = Math.min(ratio, maxDimension / width, maxDimension / height,
    Math.sqrt(maxPixels / width / height));
  const pixelWidth = Math.max(1, Math.floor(width * outputScale));
  const pixelHeight = Math.max(1, Math.floor(height * outputScale));
  return { pixelWidth, pixelHeight, scaleX: pixelWidth / width, scaleY: pixelHeight / height };
}

export function renderPageRaster(page: PDFPageProxy, scale: number, deviceRatio: number) {
  const viewport = page.getViewport({ scale });
  const size = rasterSize(viewport.width, viewport.height, deviceRatio);
  const canvas = document.createElement('canvas');
  canvas.width = size.pixelWidth;
  canvas.height = size.pixelHeight;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('PDF_CANVAS_UNAVAILABLE');
  const task = page.render({ canvas, canvasContext: context, viewport,
    transform: [size.scaleX, 0, 0, size.scaleY, 0, 0] });
  return { canvas, viewport, task };
}

export function publishRaster(visible: HTMLCanvasElement, raster: ReturnType<typeof renderPageRaster>) {
  const context = visible.getContext('2d');
  if (!context) throw new Error('PDF_CANVAS_UNAVAILABLE');
  visible.width = raster.canvas.width;
  visible.height = raster.canvas.height;
  visible.style.width = `${raster.viewport.width}px`;
  visible.style.height = `${raster.viewport.height}px`;
  context.drawImage(raster.canvas, 0, 0);
}
