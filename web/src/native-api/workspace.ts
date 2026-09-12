import {
  validateDocumentOpenResponse, validateDocumentSaveResponse,
  validateWorkspaceClosedResponse, validateWorkspaceStateResponse,
  type DocumentOpenResponseResult, type DocumentSaveResponseResult,
  type WorkspaceStateResponseResult,
} from '../../.generated/rpc/protocol';
import { SystemRpcClient } from './system';

export class WorkspaceRpcClient {
  private sequence = 0;
  constructor(private readonly rpc: SystemRpcClient) {}
  private nextSequence(): number {
    if (this.sequence >= Number.MAX_SAFE_INTEGER) throw new Error('RESOURCE_EXHAUSTED');
    return ++this.sequence;
  }
  async manageFile(workspaceId: string, operation: 'create' | 'rename' | 'remove', fileId: string,
      destination = ''): Promise<WorkspaceStateResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.manageFile',
      params: { workspaceId, operation, fileId, destination } });
    if (!validateWorkspaceStateResponse(response) || response.method !== 'workspace.manageFile')
      throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async manageDirectory(workspaceId: string, operation: 'create' | 'rename' | 'remove',
      directoryId: string, destination = ''): Promise<WorkspaceStateResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.manageDirectory',
      params: { workspaceId, operation, directoryId, destination } });
    if (!validateWorkspaceStateResponse(response) || response.method !== 'workspace.manageDirectory')
      throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async open(signal?: AbortSignal): Promise<WorkspaceStateResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.open', params: {} }, signal);
    if (!validateWorkspaceStateResponse(response) || response.method !== 'workspace.open')
      throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async getState(signal?: AbortSignal): Promise<WorkspaceStateResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.getState', params: {} }, signal);
    if (!validateWorkspaceStateResponse(response) || response.method !== 'workspace.getState')
      throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async refresh(signal?: AbortSignal): Promise<WorkspaceStateResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.refresh', params: {} }, signal);
    if (!validateWorkspaceStateResponse(response) || response.method !== 'workspace.refresh')
      throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async close(signal?: AbortSignal): Promise<void> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.close', params: {} }, signal);
    if (!validateWorkspaceClosedResponse(response)) throw new Error('INVALID_RESPONSE');
  }
  async openDocument(fileId: string, signal?: AbortSignal): Promise<DocumentOpenResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'document.open', params: { fileId } }, signal);
    if (!validateDocumentOpenResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async saveDocument(fileId: string, content: string, expectedRevision: string,
      signal?: AbortSignal): Promise<DocumentSaveResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'document.save',
      params: { fileId, content, expectedRevision } }, signal);
    if (!validateDocumentSaveResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
}
