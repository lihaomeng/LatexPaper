import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

test('CSP permits Monaco style attributes without relaxing scripts or stylesheets', () => {
  const html = readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  const native = readFileSync(new URL('../../../backend/latexlocalservice/app/platform/cef/kcefruntime.cpp', import.meta.url), 'utf8');
  for (const source of [html, native]) {
    assert.ok(source.includes("style-src-attr 'unsafe-inline';"));
    assert.ok(source.includes("script-src 'self';"));
    assert.ok(source.includes("worker-src 'self';"));
    assert.ok(source.includes("object-src 'none';"));
    assert.ok(!source.includes("style-src 'self' 'unsafe-inline'"));
    assert.ok(!source.includes("script-src 'self' 'unsafe-inline'"));
    assert.ok(!source.includes("'unsafe-eval'"));
  }
  assert.ok(html.includes("style-src 'self' 'nonce-__LIGHTOVERLEAF_STYLE_NONCE__';"));
  assert.ok(native.includes("style-src 'self' 'nonce-"));
  assert.ok(native.includes("connect-src 'none';"));
});
