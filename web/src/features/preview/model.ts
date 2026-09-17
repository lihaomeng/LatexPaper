import type { PreviewState } from './presentation';
export interface BuildView {
  state: PreviewState;
  output: string;
  diagnostics: { fileId: string; line: number; message: string; severity?: string }[];
  artifactId?: string;
  pdf?: Uint8Array;
  syncTexAvailable?: boolean;
  generation?: number;
  phase?: "snapshot" | "detect" | "compile" | "artifact" | "render" | "complete";
  candidate?: { generation: number; artifactId: string; pdf: Uint8Array; syncTexAvailable: boolean };
}
export interface PdfTarget { page: number; x: number; y: number; revision: number }
