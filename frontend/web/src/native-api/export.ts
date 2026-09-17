import {
  validateExportSelectDestinationResponse,
  validateExportProjectResponse,
  type ExportSelectDestinationResponseResult,
  type ExportProjectResponseResult,
} from '../../.generated/rpc/protocol';
import { SystemRpcClient } from './system';

export class ExportRpcClient {
  private sequence = 0;
  constructor(private readonly rpc: SystemRpcClient) {}
  private nextSequence(): number {
    if (this.sequence >= Number.MAX_SAFE_INTEGER) throw new Error('RESOURCE_EXHAUSTED');
    return ++this.sequence;
  }
  async selectDestination(signal?: AbortSignal): Promise<ExportSelectDestinationResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'export.selectDestination', params: {} }, signal);
    if (!validateExportSelectDestinationResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async exportProject(destinationToken: string, generation: number,
      files: readonly { fileId: string; content: string }[], signal?: AbortSignal): Promise<ExportProjectResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'export.project',
      params: { destinationToken, generation,
        files: files.map(file => ({ fileId: file.fileId, content: file.content })) } }, signal);
    if (!validateExportProjectResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
}