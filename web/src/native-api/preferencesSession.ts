import {
  validatePreferencesGetResponse,
  validateSessionHistoryResponse,
  validateSessionRestoreResponse,
  validateSessionSaveResponse,
  type PreferencesGetResponseResult,
  type SessionState,
} from '../../.generated/rpc/protocol';
import { SystemRpcClient } from './system';

export type Preferences = PreferencesGetResponseResult;
export type SessionSnapshot = SessionState;

export class PreferencesSessionRpcClient {
  private sequence = 0;
  constructor(private readonly rpc: SystemRpcClient) {}
  private nextSequence(): number {
    if (this.sequence >= Number.MAX_SAFE_INTEGER) throw new Error('RESOURCE_EXHAUSTED');
    return ++this.sequence;
  }
  async getPreferences(signal?: AbortSignal): Promise<Preferences> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'preferences.get', params: {} }, signal);
    if (!validatePreferencesGetResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async updatePreferences(value: Preferences, signal?: AbortSignal): Promise<Preferences> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'preferences.update', params: value }, signal);
    if (!validatePreferencesGetResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
  async restoreSession(signal?: AbortSignal): Promise<{ found: boolean; state: SessionSnapshot }> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'session.restore', params: {} }, signal);
    if (!validateSessionRestoreResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result as { found: boolean; state: SessionSnapshot };
  }
  async saveSession(value: SessionSnapshot, signal?: AbortSignal): Promise<boolean> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'session.save', params: value }, signal);
    if (!validateSessionSaveResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result.saved;
  }
  async history(signal?: AbortSignal): Promise<string[]> {
    const response = await this.rpc.request({ version: 2, id: crypto.randomUUID(),
      clientSequence: this.nextSequence(), method: 'session.history', params: {} }, signal);
    if (!validateSessionHistoryResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result.roots;
  }
}
