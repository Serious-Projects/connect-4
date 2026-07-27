import test from "node:test";
import assert from "node:assert/strict";
import {
  decodeClientMessage,
  playerNameForNewIdentity,
  sanitizePlayerName,
} from "../src/protocol.js";

test("rejects malformed JSON and non-object messages without throwing", () => {
  assert.deepEqual(decodeClientMessage("{"), {
    ok: false,
    error: "invalid_json",
  });
  for (const message of ["null", "[]", "42", "true", '"move"'])
    assert.deepEqual(decodeClientMessage(message), {
      ok: false,
      error: "invalid_message",
    });
  assert.deepEqual(decodeClientMessage('{"type":"move","column":3}'), {
    ok: true,
    data: { type: "move", column: 3 },
  });
});

test("decodes binary WebSocket messages", () => {
  const bytes = new TextEncoder().encode('{"type":"state"}');
  assert.deepEqual(decodeClientMessage(bytes), {
    ok: true,
    data: { type: "state" },
  });
});

test("sanitizes names by code point and removes control characters", () => {
  assert.equal(sanitizePlayerName("  Jay\n\u0000  "), "Jay");
  assert.equal(sanitizePlayerName("J@a#y"), "Jay");
  assert.equal(sanitizePlayerName("abcdefghijklmnopq"), "abcdefghijklmnop");
  assert.equal(sanitizePlayerName("😀😀😀"), "");
});

test("a new seat identity never inherits the previous player's name", () => {
  assert.equal(playerNameForNewIdentity("Ayush", 1), "Ayush");
  assert.equal(playerNameForNewIdentity("", 1), "Player one");
  assert.equal(playerNameForNewIdentity("", 2), "Player two");
});
