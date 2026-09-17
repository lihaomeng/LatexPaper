import { test } from 'node:test';
import assert from 'node:assert/strict';
import { rasterSize } from '../src/features/preview/pdfRaster.ts';

test('high DPI changes backing pixels, not logical page dimensions', () => {
  for (const ratio of [1, 1.25, 1.5, 2]) {
    const size = rasterSize(800, 1000, ratio);
    assert.equal(size.pixelWidth, 800 * ratio);
    assert.equal(size.pixelHeight, 1000 * ratio);
    assert.equal(size.scaleX, ratio);
    assert.equal(size.scaleY, ratio);
  }
});

test('large pages respect both pixel and dimension budgets', () => {
  for (const [width, height] of [[1800, 2600], [20000, 20000], [100000, 100]]) {
    const size = rasterSize(width, height, 4);
    assert.ok(size.pixelWidth * size.pixelHeight <= 16 * 1024 * 1024);
    assert.ok(size.pixelWidth <= 8192 && size.pixelHeight <= 8192);
    assert.ok(size.pixelWidth >= 1 && size.pixelHeight >= 1);
  }
});

test('invalid page size fails before allocating a canvas', () => {
  for (const dimension of [0, -1, NaN, Infinity])
    assert.throws(() => rasterSize(dimension, 800, 2), /PDF_INVALID_PAGE_SIZE/);
  assert.equal(rasterSize(800, 1000, NaN).pixelWidth, 800);
});
