import assert from 'node:assert/strict';
import { test } from 'node:test';
import { AuthoringRpcClient } from '../src/native-api/authoring.ts';
import { SystemRpcClient, type SystemTransport } from '../src/native-api/system.ts';

test('authoring RPC keeps search, detection, build and cancellation contracts separate', async () => {
  const methods: string[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    methods.push(request.method);
    if (request.method === 'build.start') {
      assert.equal(request.params.scopeId, 'job-1');
      assert.equal(request.params.generation, 1);
      assert.deepEqual(request.params.overlayFiles,
        [{ fileId: 'main.tex', content: '\\documentclass{article}' }]);
    }
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence, ok: true,
      method: request.method };
    const result = request.method === 'search.start' ? { hits: [{ fileId: 'main.tex', line: 2,
      column: 1, preview: '中文' }], truncated: false }
      : request.method === 'build.detect' ? { toolchains: [{ toolchainId: 'tex', displayName: 'TeX',
        engines: ['pdflatex'] }] }
      : request.method === 'build.start' ? { jobId: request.params.jobId, terminal: 'succeeded',
        exitCode: 0, output: '', outputTruncated: false, diagnostics: [],
        artifactId: 'artifact-1', syncTexAvailable: true, generation: request.params.generation, phase: 'artifact' }
      : request.method === 'build.status' ? { jobId: request.params.jobId, state: 'running',
        output: 'partial', outputTruncated: false, generation: 1, phase: 'compile' }
      : { accepted: true };
    success(JSON.stringify({ ...base, result })); return () => {};
  } };
  const client = new AuthoringRpcClient(new SystemRpcClient(transport));
  assert.equal((await client.search('中文')).hits[0].line, 2);
  assert.equal((await client.detect()).toolchains[0].engines[0], 'pdflatex');
  assert.equal((await client.build('job-1', 'snapshot-1', 'main.tex', 'pdflatex', 3000,
    [{ fileId: 'main.tex', content: '\\documentclass{article}' }])).terminal, 'succeeded');
  assert.equal((await client.status('job-1')).output, 'partial');
  assert.equal(await client.cancel('job-1'), true);
  assert.deepEqual(methods, ['search.start', 'build.detect', 'build.start', 'build.status', 'build.cancel']);
});

test('authoring downloads bounded PDF chunks and maps navigation', async () => {
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence, ok: true, method: request.method };
    const result = request.method === 'preview.read' ? { offset: 0, totalBytes: 5, hex: '255044462d' }
      : request.method === 'navigation.forward' ? { page: 2, x: 12, y: 30 }
      : { fileId: 'main.tex', line: 9, column: 1 };
    success(JSON.stringify({ ...base, result })); return () => {};
  } };
  const client = new AuthoringRpcClient(new SystemRpcClient(transport));
  assert.equal(new TextDecoder().decode(await client.readPdf('artifact-1')), '%PDF-');
  assert.equal((await client.forward('artifact-1', 'main.tex', 9, 1)).page, 2);
  assert.equal((await client.reverse('artifact-1', 2, 12, 30)).line, 9);
});

test('authoring RPC rejects a response for another method', async () => {
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence,
      ok: true, method: 'build.detect', result: { toolchains: [] } }));
    return () => {};
  } };
  await assert.rejects(new AuthoringRpcClient(new SystemRpcClient(transport)).search('x'), /INVALID_RESPONSE/);
});
