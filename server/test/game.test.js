import test from "node:test";
import assert from "node:assert/strict";
import { applyMove, createGame, normalizeGame, publicGame, resetRound } from "../src/game.js";

test("alternates turns and rejects out-of-turn moves", () => {
  const game = createGame();
  assert.equal(applyMove(game, 1, 3).ok, true);
  assert.equal(game.turn, 2);
  assert.equal(applyMove(game, 1, 2).error, "not_your_turn");
});

test("rejects invalid and full columns", () => {
  const game = createGame();
  assert.equal(applyMove(game, 1, -1).error, "invalid_column");
  for (let i = 0; i < 6; ++i) assert.equal(applyMove(game, game.turn, 0).ok, true);
  assert.equal(applyMove(game, game.turn, 0).error, "column_full");
});

test("detects horizontal victory with exact line cells", () => {
  const game = createGame();
  for (const column of [0, 0, 1, 1, 2, 2, 3])
    assert.equal(applyMove(game, game.turn, column).ok, true);
  assert.equal(game.winner, 1);
  assert.equal(game.scores[1], 1);
  assert.equal(game.history.length, 7);
  assert.deepEqual(game.history.at(-1), { row: 5, column: 3, player: 1 });
  assert.deepEqual(game.winningCells, [[5, 0], [5, 1], [5, 2], [5, 3]]);
  assert.equal(applyMove(game, 2, 4).error, "round_over");
});

test("detects vertical and diagonal victories", () => {
  const vertical = createGame();
  for (const column of [0, 1, 0, 1, 0, 1, 0]) applyMove(vertical, vertical.turn, column);
  assert.equal(vertical.winner, 1);

  const diagonal = createGame();
  for (const column of [0, 1, 1, 2, 4, 2, 2, 3, 4, 3, 5, 3, 3])
    applyMove(diagonal, diagonal.turn, column);
  assert.equal(diagonal.winner, 1);
});

test("rematch alternates the starting player", () => {
  const game = createGame();
  game.scores[1] = 2;
  game.scores[2] = 1;
  applyMove(game, game.turn, 4);
  resetRound(game);
  assert.equal(game.turn, 2);
  assert.equal(game.round, 2);
  assert.equal(game.moves, 0);
  assert.deepEqual(game.scores, { 1: 2, 2: 1, draws: 0 });
  assert.deepEqual(game.history, []);
  assert.deepEqual(game.lastReplay, [{ row: 5, column: 4, player: 1 }]);
  assert.deepEqual(
    { coral: publicGame(game).coralScore, gold: publicGame(game).goldScore, draws: publicGame(game).drawScore },
    { coral: 2, gold: 1, draws: 0 }
  );
});

test("upgrades old persisted rooms with authoritative scores", () => {
  const game = createGame();
  delete game.scores;
  for (const column of [0, 0, 1, 1, 2, 2, 3]) applyMove(game, game.turn, column);
  assert.deepEqual(game.scores, { 1: 1, 2: 0, draws: 0 });
});

test("detects a full-board draw and scores it once", () => {
  const game = createGame();
  const drawSequence = [4,1,3,5,2,3,3,2,1,1,4,4,0,0,5,6,1,3,6,1,1,5,3,2,6,2,2,2,3,0,0,6,4,0,6,0,5,5,6,4,5,4];
  for (const column of drawSequence) assert.equal(applyMove(game, game.turn, column).ok, true);
  assert.equal(game.draw, true);
  assert.equal(game.winner, 0);
  assert.equal(game.moves, 42);
  assert.equal(game.scores.draws, 1);
  assert.equal(applyMove(game, game.turn, 0).error, "round_over");
  assert.equal(game.scores.draws, 1);
});

test("normalizes legacy and malformed persisted rooms safely", () => {
  const legacy = normalizeGame({ board: Array(42).fill(0), turn: 2, players: null, names: null });
  assert.equal(legacy.turn, 2);
  assert.deepEqual(legacy.players, { 1: null, 2: null });
  assert.deepEqual(legacy.names, { 1: "Player one", 2: "Player two" });
  assert.deepEqual(legacy.rematchVotes, []);

  const malformed = normalizeGame({ board: [99], scores: { 1: -4 }, history: [{ row: 99, column: 0, player: 1 }] });
  assert.equal(malformed.board.length, 42);
  assert.equal(malformed.board.every(cell => cell === 0), true);
  assert.deepEqual(malformed.scores, { 1: 0, 2: 0, draws: 0 });
  assert.deepEqual(malformed.history, []);

  const completed = createGame();
  for (const column of [0, 0, 1, 1, 2, 2, 3])
    applyMove(completed, completed.turn, column);
  completed.winner = 2;
  completed.winningCells = [];
  const repaired = normalizeGame(completed);
  assert.equal(repaired.winner, 1);
  assert.deepEqual(repaired.winningCells, [[5, 0], [5, 1], [5, 2], [5, 3]]);

  const mismatched = { ...completed, board: Array(42).fill(0) };
  const reset = normalizeGame(mismatched);
  assert.equal(reset.moves, 0);
  assert.equal(reset.winner, 0);
  assert.deepEqual(reset.history, []);
});

test("normalizes seat last-seen timestamps", () => {
  const fresh = normalizeGame({});
  assert.deepEqual(fresh.lastSeen, { 1: 0, 2: 0 });

  const legacy = normalizeGame({ board: Array(42).fill(0), turn: 1 });
  assert.deepEqual(legacy.lastSeen, { 1: 0, 2: 0 });

  const seen = normalizeGame({ lastSeen: { 1: 1234, 2: "bogus" } });
  assert.deepEqual(seen.lastSeen, { 1: 1234, 2: 0 });

  const invalid = normalizeGame({ lastSeen: { 1: -5, 2: Infinity } });
  assert.deepEqual(invalid.lastSeen, { 1: 0, 2: 0 });
});

test("preserves board, history and outcome invariants across randomized games", () => {
  let seed = 0xc04f0;
  const random = () => (seed = (seed * 1664525 + 1013904223) >>> 0);
  for (let trial = 0; trial < 2500; ++trial) {
    const game = createGame();
    while (!game.winner && !game.draw) applyMove(game, game.turn, random() % 7);
    assert.equal(game.moves, game.board.filter(Boolean).length);
    assert.equal(game.history.length, game.moves);
    assert.equal(game.winner !== 0 || game.draw, true);
    assert.equal(game.winner ? game.winningCells.length : 0, game.winningCells.length);
    assert.equal(game.draw ? game.moves : true, game.draw ? 42 : true);
    assert.equal(game.scores[1] + game.scores[2] + game.scores.draws, 1);
    const restored = normalizeGame(structuredClone(game));
    assert.deepEqual(restored.board, game.board);
    assert.deepEqual(restored.history, game.history);
    assert.equal(restored.turn, game.turn);
    assert.equal(restored.winner, game.winner);
    assert.equal(restored.draw, game.draw);
    assert.deepEqual(restored.winningCells, game.winningCells);
  }
});
