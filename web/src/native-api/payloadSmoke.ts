import { WorkspaceRpcClient } from './workspace';

const payloadBytes = 4 * 1024 * 1024;

/** CLI-only probe: exact document limit travels Renderer -> native -> Renderer. */
export async function payloadSmokeProbe(client: WorkspaceRpcClient): Promise<void> {
  await client.open();
  try {
    const content = 'x'.repeat(payloadBytes);
    await client.saveDocumentAs('payload-limit.tex', content, false);
    const loaded = await client.openDocument('payload-limit.tex');
    if (loaded.content.length !== payloadBytes || loaded.content !== content)
      throw new Error('PAYLOAD_SMOKE_MISMATCH');
  } finally {
    await client.close();
  }
}
