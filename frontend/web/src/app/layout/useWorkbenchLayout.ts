import { useEffect, useState } from "react";
export type LayoutPreset = "edit" | "split" | "read";
import type { PdfZoomMode } from "../../features/preview";
type Layout = {
  sidebar: boolean; preview: boolean; sidebarWidth: number; ratio: number;
  preset: LayoutPreset; zoom: number; zoomMode: PdfZoomMode;
};
const storageKey = "lightoverleaf.layout.v3";
const defaults: Layout = { sidebar: true, preview: true, sidebarWidth: 260, ratio: .5,
  preset: "split", zoom: 100, zoomMode: "width" };
const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));
function readLayout(): Layout {
  try {
    const current = localStorage.getItem(storageKey);
    const stored: unknown = JSON.parse(current ?? localStorage.getItem("lightoverleaf.layout.v2") ?? "null");
    if (!stored || typeof stored !== "object") return defaults;
    const v = stored as Partial<Layout>;
    return {
      sidebar: typeof v.sidebar === "boolean" ? v.sidebar : true,
      preview: typeof v.preview === "boolean" ? v.preview : true,
      sidebarWidth: Number.isFinite(v.sidebarWidth) ? clamp(v.sidebarWidth!, 220, 380) : 260,
      ratio: Number.isFinite(v.ratio) ? clamp(v.ratio!, .2, .8) : .5,
      preset: v.preset === "edit" || v.preset === "read" ? v.preset : "split",
      zoom: Number.isFinite(v.zoom) ? clamp(v.zoom!, 50, 300) : 100,
      zoomMode: current && (v.zoomMode === "page" || v.zoomMode === "custom") ? v.zoomMode : "width",
    };
  } catch { return defaults; }
}
export function useWorkbenchLayout() {
  const [layout, setLayout] = useState(readLayout);
  const [width, setWidth] = useState(window.innerWidth);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const drawer = width < 1100;
  useEffect(() => {
    const resize = () => setWidth(window.innerWidth);
    window.addEventListener("resize", resize);
    return () => window.removeEventListener("resize", resize);
  }, []);
  useEffect(() => {
    const timer = window.setTimeout(() => {
      try { localStorage.setItem(storageKey, JSON.stringify(layout)); } catch { /* Layout is optional. */ }
    }, 200);
    return () => window.clearTimeout(timer);
  }, [layout]);
  const sidebar = drawer ? drawerOpen : layout.sidebar;
  const available = Math.max(300, width - 48 - (sidebar && !drawer ? layout.sidebarWidth + 4 : 0) - 4);
  const maxEditorWidth = Math.max(300, available - 300);
  const editorWidth = clamp(Math.round(available * layout.ratio), 300, maxEditorWidth);
  const compact = width < 760;
  return {
    sidebar, drawer, sidebarWidth: layout.sidebarWidth,
    setSidebar: (value: boolean) => drawer ? setDrawerOpen(value) : setLayout(v => ({ ...v, sidebar: value })),
    setSidebarWidth: (value: number) => setLayout(v => ({ ...v, sidebarWidth: Math.round(clamp(value, 220, 380)) })),
    editorWidth, maxEditorWidth,
    setEditorWidth: (value: number) => setLayout(v => ({ ...v, ratio: clamp(value / available, .2, .8) })),
    preview: layout.preview,
    setPreview: (value: boolean) => setLayout(v => ({ ...v, preview: value })),
    editorVisible: !(compact && layout.preview && layout.preset === "read"),
    previewVisible: layout.preview && (!compact || layout.preset === "read"),
    previewZoom: layout.zoom,
    setPreviewZoom: (value: number) => setLayout(v => ({ ...v, zoom: clamp(value, 50, 300) })),
    zoomMode: layout.zoomMode,
    setZoomMode: (value: PdfZoomMode) => setLayout(v => ({ ...v, zoomMode: value })),
    preset: layout.preset,
    setPreset: (value: LayoutPreset) => setLayout(v => ({
      ...v, preset: value, preview: true, ratio: value === "edit" ? .68 : value === "read" ? .32 : .5,
    })),
  };
}
