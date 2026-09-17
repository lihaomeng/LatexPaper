import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { validatePing } from "../.generated/rpc/contract.ts";
import { FakeNativeApi } from "../src/native-api/index.ts";
const fixtures = JSON.parse(readFileSync(new URL("../../../tests/fixtures/ping.json", import.meta.url), "utf8"));
for (const fixture of fixtures) test(fixture.name, () => {
  assert.equal(validatePing(fixture.request), fixture.accepted);
});
test("Fake NativeApi obeys contract", async () => {
  await new FakeNativeApi().ping(fixtures[0].request);
  await assert.rejects(new FakeNativeApi().ping(fixtures.find((f: { accepted: boolean }) => !f.accepted).request),
    /INVALID_ARGUMENT/);
});

