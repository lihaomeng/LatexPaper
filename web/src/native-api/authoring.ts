import {
  validateSearchStartResponse, validateBuildDetectResponse, validateBuildStartResponse,
  validateBuildStatusResponse, validateBuildCancelResponse, type SearchStartResponseResult, type BuildDetectResponseResult,
  type BuildStartResponseResult,
  type BuildStatusResponseResult,
  validatePreviewReadResponse, validateNavigationForwardResponse, validateNavigationReverseResponse,
  type NavigationForwardResponseResult, type NavigationReverseResponseResult,
} from '../../.generated/rpc/protocol';
import { SystemRpcClient } from './system';

export class AuthoringRpcClient {
  private sequence = 0;
  constructor(private readonly rpc: SystemRpcClient) {}
  private nextSequence(): number {
    if (this.sequence >= Number.MAX_SAFE_INTEGER) throw new Error('RESOURCE_EXHAUSTED');
    return ++this.sequence;
  }
  async search(query: string, caseSensitive = false, maxResults = 200,
      signal?: AbortSignal): Promise<SearchStartResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'search.start',
      params: { query, caseSensitive, maxResults } }, signal);
    if (!validateSearchStartResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async detect(signal?: AbortSignal): Promise<BuildDetectResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'build.detect', params: {} }, signal);
    if (!validateBuildDetectResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async build(jobId: string, snapshotId: string, mainFileId: string,
      engine: 'pdflatex' | 'xelatex' | 'lualatex', timeoutMs = 120000,
      signal?: AbortSignal): Promise<BuildStartResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'build.start',
      params: { jobId, snapshotId, mainFileId, engine, timeoutMs } }, signal);
    if (!validateBuildStartResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async cancel(jobId: string, signal?: AbortSignal): Promise<boolean> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'build.cancel', params: { jobId } }, signal);
    if (!validateBuildCancelResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result.accepted;
  }
  async status(jobId: string, signal?: AbortSignal): Promise<BuildStatusResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'build.status', params: { jobId } }, signal);
    if (!validateBuildStatusResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async readPdf(artifactId: string, signal?: AbortSignal): Promise<Uint8Array> {
    const parts: Uint8Array[] = []; let offset = 0; let total = 1;
    while (offset < total) {
      const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
        clientSequence: this.nextSequence(), method: 'preview.read',
        params: { artifactId, offset, count: 524288 } }, signal);
      if (!validatePreviewReadResponse(response) || response.result.offset !== offset)
        throw new Error('INVALID_RESPONSE');
      const encoded = response.result.hex;
      if (encoded.length % 2 !== 0) throw new Error('INVALID_RESPONSE');
      const chunk = new Uint8Array(encoded.length / 2);
      for (let index = 0; index < chunk.length; index += 1) {
        const value = Number.parseInt(encoded.slice(index * 2, index * 2 + 2), 16);
        if (!Number.isFinite(value)) throw new Error('INVALID_RESPONSE');
        chunk[index] = value;
      }
      total = response.result.totalBytes; parts.push(chunk); offset += chunk.length;
      if (chunk.length === 0 && offset < total) throw new Error('INVALID_RESPONSE');
    }
    const bytes = new Uint8Array(total); let cursor = 0;
    for (const part of parts) { bytes.set(part, cursor); cursor += part.length; }
    return bytes;
  }
  async forward(artifactId: string, fileId: string, line: number, column: number,
      signal?: AbortSignal): Promise<NavigationForwardResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'navigation.forward',
      params: { artifactId, fileId, line, column } }, signal);
    if (!validateNavigationForwardResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async reverse(artifactId: string, page: number, x: number, y: number,
      signal?: AbortSignal): Promise<NavigationReverseResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'navigation.reverse',
      params: { artifactId, page, x: Math.max(0, Math.round(x)), y: Math.max(0, Math.round(y)) } }, signal);
    if (!validateNavigationReverseResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
}
