import {
  validateDocumentOpenResponse, validateDocumentSaveResponse,
  validateWorkspaceClosedResponse, validateWorkspaceStateResponse,
  validateWorkspaceTrashListResponse,
  validateWorkspacePollChangesResponse,
  type DocumentOpenResponseResult, type DocumentSaveResponseResult,
  type WorkspaceStateResponseResult,
  type WorkspaceTrashListResponseResult,
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
  async pollChanges(workspaceId: string, signal?: AbortSignal): Promise<boolean> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.pollChanges', params: { workspaceId } }, signal);
    if (!validateWorkspacePollChangesResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result.changed;
  }
  async listTrash(workspaceId: string, signal?: AbortSignal): Promise<WorkspaceTrashListResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.listTrash', params: { workspaceId } }, signal);
    if (!validateWorkspaceTrashListResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async restoreTrash(workspaceId: string, trashId: string,
      signal?: AbortSignal): Promise<WorkspaceStateResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'workspace.restoreTrash',
      params: { workspaceId, trashId } }, signal);
    if (!validateWorkspaceStateResponse(response) || response.method !== 'workspace.restoreTrash')
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
  async saveDocumentAs(fileId: string, content: string, utf8Bom: boolean,
      signal?: AbortSignal): Promise<DocumentSaveResponseResult> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'document.saveAs',
      params: { fileId, content, utf8Bom } }, signal);
    if (!validateDocumentSaveResponse(response) || response.method !== 'document.saveAs')
      throw new Error('INVALID_RESPONSE');
    return response.result;
  }
}
