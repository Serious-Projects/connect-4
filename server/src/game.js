import { sanitizePlayerName } from "./protocol.js";

export const ROWS = 6;
export const COLUMNS = 7;

export function createGame() {
  return {
    board: Array(ROWS * COLUMNS).fill(0),
    turn: 1,
    winner: 0,
    draw: false,
    moves: 0,
    round: 1,
    startingPlayer: 1,
    scores: { 1: 0, 2: 0, draws: 0 },
    history: [],
    lastReplay: [],
    players: { 1: null, 2: null },
    lastSeen: { 1: 0, 2: 0 },
    names: { 1: "Player one", 2: "Player two" },
    rematchVotes: [],
    winningCells: [],
    updatedAt: Date.now(),
  };
}

export function normalizeGame(value) {
  const fresh = createGame();
  if (!value || typeof value !== "object" || Array.isArray(value)) return fresh;

  const scores =
    value.scores && typeof value.scores === "object" ? value.scores : {};
  const players =
    value.players && typeof value.players === "object" ? value.players : {};
  const lastSeen =
    value.lastSeen && typeof value.lastSeen === "object" ? value.lastSeen : {};
  const names =
    value.names && typeof value.names === "object" ? value.names : {};
  const cleanMoves = (moves) => {
    if (!Array.isArray(moves)) return [];
    if (moves.length > ROWS * COLUMNS) return [];
    return moves.every(
      (move) =>
        move &&
        Number.isInteger(move.row) &&
        move.row >= 0 &&
        move.row < ROWS &&
        Number.isInteger(move.column) &&
        move.column >= 0 &&
        move.column < COLUMNS &&
        (move.player === 1 || move.player === 2),
    )
      ? moves.map((move) => ({ ...move }))
      : [];
  };
  const emptySuppliedBoard =
    Array.isArray(value.board) &&
    value.board.length === ROWS * COLUMNS &&
    value.board.every((cell) => cell === 0);
  const startingPlayer =
    value.startingPlayer === 2 ||
    (value.startingPlayer !== 1 && emptySuppliedBoard && value.turn === 2)
      ? 2
      : 1;
  const history = cleanMoves(value.history);
  const canonical = createGame();
  canonical.startingPlayer = startingPlayer;
  canonical.turn = startingPlayer;
  let historyValid = true;
  for (const move of history) {
    const result = applyMove(canonical, move.player, move.column);
    if (!result.ok || result.row !== move.row || result.player !== move.player) {
      historyValid = false;
      break;
    }
  }
  const suppliedBoardValid =
    Array.isArray(value.board) &&
    value.board.length === ROWS * COLUMNS &&
    value.board.every((cell) => cell === 0 || cell === 1 || cell === 2);
  if (!suppliedBoardValid || !historyValid ||
      canonical.board.some((cell, index) => cell !== value.board[index])) {
    canonical.board = [...fresh.board];
    canonical.turn = startingPlayer;
    canonical.winner = 0;
    canonical.draw = false;
    canonical.moves = 0;
    canonical.history = [];
    canonical.winningCells = [];
  }

  return {
    ...fresh,
    ...value,
    board: canonical.board,
    turn: canonical.turn,
    winner: canonical.winner,
    draw: canonical.draw,
    moves: canonical.moves,
    round: Number.isInteger(value.round) && value.round > 0 ? value.round : 1,
    startingPlayer,
    scores: {
      1: Number.isInteger(scores[1]) && scores[1] >= 0 ? scores[1] : 0,
      2: Number.isInteger(scores[2]) && scores[2] >= 0 ? scores[2] : 0,
      draws:
        Number.isInteger(scores.draws) && scores.draws >= 0 ? scores.draws : 0,
    },
    history: canonical.history,
    lastReplay: cleanMoves(value.lastReplay),
    players: {
      1: typeof players[1] === "string" ? players[1] : null,
      2: typeof players[2] === "string" ? players[2] : null,
    },
    lastSeen: {
      1: Number.isFinite(lastSeen[1]) && lastSeen[1] > 0 ? lastSeen[1] : 0,
      2: Number.isFinite(lastSeen[2]) && lastSeen[2] > 0 ? lastSeen[2] : 0,
    },
    names: {
      1:
        typeof names[1] === "string" && names[1]
          ? sanitizePlayerName(names[1]) || fresh.names[1]
          : fresh.names[1],
      2:
        typeof names[2] === "string" && names[2]
          ? sanitizePlayerName(names[2]) || fresh.names[2]
          : fresh.names[2],
    },
    rematchVotes: (canonical.winner || canonical.draw) && Array.isArray(value.rematchVotes)
      ? [
          ...new Set(
            value.rematchVotes.filter((seat) => seat === 1 || seat === 2),
          ),
        ]
      : [],
    winningCells: canonical.winningCells,
    updatedAt: Number.isFinite(value.updatedAt) ? value.updatedAt : Date.now(),
  };
}

export function applyMove(game, player, column) {
  game.scores ??= { 1: 0, 2: 0, draws: 0 };
  game.history ??= [];
  if (!Number.isInteger(column) || column < 0 || column >= COLUMNS)
    return { ok: false, error: "invalid_column" };
  if (game.winner || game.draw) return { ok: false, error: "round_over" };
  if (game.turn !== player) return { ok: false, error: "not_your_turn" };

  let row = -1;
  for (let candidate = ROWS - 1; candidate >= 0; --candidate) {
    if (game.board[candidate * COLUMNS + column] === 0) {
      row = candidate;
      break;
    }
  }
  if (row < 0) return { ok: false, error: "column_full" };

  game.board[row * COLUMNS + column] = player;
  game.history.push({ row, column, player });
  game.moves += 1;
  game.updatedAt = Date.now();
  game.winningCells = findWinningCells(game.board, row, column, player);
  if (game.winningCells.length === 4) {
    game.winner = player;
    game.scores[player] += 1;
  } else if (game.moves === ROWS * COLUMNS) {
    game.draw = true;
    game.scores.draws += 1;
  } else game.turn = player === 1 ? 2 : 1;
  return { ok: true, row, column, player };
}

export function findWinningCells(board, row, column, player) {
  const directions = [
    [0, 1],
    [1, 0],
    [1, 1],
    [1, -1],
  ];
  for (const [dr, dc] of directions) {
    const cells = [[row, column]];
    for (const sign of [-1, 1]) {
      let r = row + dr * sign;
      let c = column + dc * sign;
      while (
        r >= 0 &&
        r < ROWS &&
        c >= 0 &&
        c < COLUMNS &&
        board[r * COLUMNS + c] === player
      ) {
        if (sign < 0) cells.unshift([r, c]);
        else cells.push([r, c]);
        r += dr * sign;
        c += dc * sign;
      }
    }
    if (cells.length >= 4) return cells.slice(0, 4);
  }
  return [];
}

export function resetRound(game) {
  game.lastReplay = [...(game.history ?? [])];
  game.startingPlayer = game.startingPlayer === 1 ? 2 : 1;
  game.board = Array(ROWS * COLUMNS).fill(0);
  game.turn = game.startingPlayer;
  game.winner = 0;
  game.draw = false;
  game.moves = 0;
  game.history = [];
  game.round += 1;
  game.rematchVotes = [];
  game.winningCells = [];
  game.updatedAt = Date.now();
}

export function publicGame(game) {
  const scores = game.scores ?? { 1: 0, 2: 0, draws: 0 };
  return {
    board: game.board,
    turn: game.turn,
    winner: game.winner,
    draw: game.draw,
    moves: game.moves,
    round: game.round,
    coralScore: scores[1] ?? 0,
    goldScore: scores[2] ?? 0,
    drawScore: scores.draws ?? 0,
    history: game.history ?? [],
    lastReplay: game.lastReplay ?? [],
    names: game.names,
    winningCells: game.winningCells,
    rematchVotes: game.rematchVotes.length,
  };
}
