import { DurableObject } from "cloudflare:workers";
import {
  applyMove,
  createGame,
  normalizeGame,
  publicGame,
  resetRound,
} from "./game.js";
import { decodeClientMessage, sanitizePlayerName } from "./protocol.js";

const JSON_HEADERS = {
  "content-type": "application/json; charset=utf-8",
  "access-control-allow-origin": "*",
  "cache-control": "no-store",
};
const ROOM_TTL_MS = 24 * 60 * 60 * 1000;
const MAX_CONNECTIONS_PER_ROOM = 8;
const MAX_MESSAGE_BYTES = 4096;
const MESSAGE_WINDOW_MS = 10_000;
const MAX_MESSAGES_PER_WINDOW = 80;
const ROOM_CREATION_WINDOW_MS = 60 * 60 * 1000;
const MAX_ROOMS_PER_WINDOW = 20;
const REACTIONS = new Set(["thumbs", "fire", "laugh", "wow"]);
const REACTION_COOLDOWN_MS = 900;
const CURSOR_WINDOW_MS = 1000;
const MAX_CURSORS_PER_WINDOW = 30;
const SEAT_RECLAIM_GRACE_MS = 2 * 60 * 1000;

function json(data, status = 200) {
  return new Response(JSON.stringify(data), { status, headers: JSON_HEADERS });
}

function roomCode() {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const bytes = crypto.getRandomValues(new Uint8Array(6));
  return Array.from(bytes, (value) => alphabet[value % alphabet.length]).join(
    "",
  );
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS")
      return new Response(null, {
        headers: {
          ...JSON_HEADERS,
          "access-control-allow-methods": "GET, POST, OPTIONS",
        },
      });
    if (url.pathname === "/" || url.pathname === "/health")
      return json({ ok: true, service: "connect-four-relay", version: 8 });
    if (url.pathname === "/rooms" && request.method === "POST") {
      const clientKey =
        request.headers.get("CF-Connecting-IP") || "local-client";
      const limiterId = env.ROOM_LIMITER.idFromName(clientKey);
      const limitResponse = await env.ROOM_LIMITER.get(limiterId).fetch(
        "https://limiter.internal/check",
        { method: "POST" },
      );
      if (!limitResponse.ok)
        return json({ ok: false, error: "room_creation_rate_limited" }, 429);
      for (let attempt = 0; attempt < 5; ++attempt) {
        const code = roomCode();
        const id = env.GAME_ROOMS.idFromName(code);
        const reserved = await env.GAME_ROOMS.get(id).fetch(
          "https://room.internal/reserve",
          { method: "POST" },
        );
        if (reserved.ok) return json({ ok: true, room: code }, 201);
      }
      return json({ ok: false, error: "room_allocation_failed" }, 503);
    }

    const match = url.pathname.match(/^\/rooms\/([A-Z2-9]{6})$/i);
    if (!match) return json({ ok: false, error: "not_found" }, 404);
    const code = match[1].toUpperCase();
    const id = env.GAME_ROOMS.idFromName(code);
    return env.GAME_ROOMS.get(id).fetch(request);
  },
};

export class RoomCreationLimiter extends DurableObject {
  async fetch() {
    const now = Date.now();
    let window = (await this.ctx.storage.get("window")) ?? {
      startedAt: now,
      count: 0,
    };
    if (now - window.startedAt >= ROOM_CREATION_WINDOW_MS)
      window = { startedAt: now, count: 0 };
    if (window.count >= MAX_ROOMS_PER_WINDOW)
      return json({ ok: false, error: "rate_limited" }, 429);
    window.count += 1;
    await this.ctx.storage.put("window", window);
    await this.ctx.storage.setAlarm(window.startedAt + ROOM_CREATION_WINDOW_MS);
    return json({ ok: true });
  }

  async alarm() {
    await this.ctx.storage.deleteAll();
  }
}

export class GameRoom extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.game = createGame();
    this.reserved = false;
    this.ready = ctx.blockConcurrencyWhile(async () => {
      this.game = normalizeGame(await ctx.storage.get("game"));
      this.reserved = (await ctx.storage.get("reserved")) ?? false;
    });
    ctx.setWebSocketAutoResponse(
      new WebSocketRequestResponsePair("ping", "pong"),
    );
  }

  async fetch(request) {
    await this.ready;
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/reserve") {
      if (this.reserved)
        return json({ ok: false, error: "already_reserved" }, 409);
      this.reserved = true;
      await this.ctx.storage.put("reserved", true);
      await this.ctx.storage.put("reservedAt", Date.now());
      await this.ctx.storage.setAlarm(Date.now() + ROOM_TTL_MS);
      return json({ ok: true }, 201);
    }
    if (!this.reserved)
      return json({ ok: false, error: "room_not_found" }, 404);
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket")
      return json({
        ok: true,
        game: publicGame(this.game),
        connected: this.ctx.getWebSockets().length,
      });
    let token =
      url.searchParams.get("token")?.slice(0, 80) || crypto.randomUUID();
    // Sockets this token already holds are closed below and replaced by this
    // connection, so they must not count against the room capacity.
    const liveOtherConnections = this.ctx
      .getWebSockets()
      .filter((socket) => socket.deserializeAttachment()?.token !== token);
    if (liveOtherConnections.length >= MAX_CONNECTIONS_PER_ROOM)
      return json({ ok: false, error: "room_connection_limit" }, 429);

    const requestedName = sanitizePlayerName(url.searchParams.get("name"));
    let seat = this.seatForToken(token);
    let identityChanged = false;
    if (seat && requestedName && this.game.names[seat] !== requestedName) {
      this.game.names[seat] = requestedName;
      identityChanged = true;
    }
    if (!seat) {
      const now = Date.now();
      const connected = new Set(this.connectedSeats());
      this.game.lastSeen ??= { 1: 0, 2: 0 };
      // A never-claimed seat is free immediately. A seat whose player merely
      // dropped (network blip, app restart) stays reserved for its original
      // token during the grace period so a newcomer cannot hijack it; the
      // original token always reclaims its exact seat above.
      const claimable = (candidate) =>
        !connected.has(candidate) &&
        (!this.game.players[candidate] ||
          now - (this.game.lastSeen[candidate] || 0) >= SEAT_RECLAIM_GRACE_MS);
      if (claimable(1)) seat = 1;
      else if (claimable(2)) seat = 2;
      else seat = 0;
      if (seat) {
        this.game.players[seat] = token;
        if (requestedName) this.game.names[seat] = requestedName;
        this.game.rematchVotes = this.game.rematchVotes.filter(
          (vote) => vote !== seat,
        );
        this.game.updatedAt = Date.now();
        identityChanged = true;
      }
    }
    if (seat) {
      this.game.lastSeen ??= { 1: 0, 2: 0 };
      this.game.lastSeen[seat] = Date.now();
      identityChanged = true;
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    for (const existing of this.ctx.getWebSockets()) {
      if (existing.deserializeAttachment()?.token === token) {
        try {
          existing.close(1000, "Reconnected from another window");
        } catch {}
      }
    }
    server.serializeAttachment({
      connectionId: crypto.randomUUID(),
      token,
      seat,
      rateStartedAt: Date.now(),
      rateCount: 0,
      lastReactionAt: 0,
    });
    this.ctx.acceptWebSocket(server);
    // Accept before yielding so another simultaneous fetch observes this seat
    // as occupied. Persistence remains guaranteed by the Durable Object task.
    if (identityChanged) this.ctx.waitUntil(this.save());
    server.send(
      JSON.stringify({
        type: "welcome",
        token,
        seat,
        game: publicGame(this.game),
        connected: this.connectedSeats(),
      }),
    );
    this.broadcast(
      {
        type: "presence",
        connected: this.connectedSeats(),
        game: publicGame(this.game),
      },
      server,
    );
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(socket, message) {
    await this.ready;
    if (
      (typeof message === "string"
        ? new TextEncoder().encode(message).byteLength
        : message.byteLength) > MAX_MESSAGE_BYTES
    ) {
      socket.close(1009, "Message too large");
      return;
    }
    const attachment = socket.deserializeAttachment() || {};
    const now = Date.now();
    const decoded = decodeClientMessage(message);
    // Cursor previews are high-frequency and purely cosmetic: excess ones are
    // dropped silently so an enthusiastic mouse never severs the connection.
    // Everything else counts toward the hard budget that closes abusers.
    if (decoded.ok && decoded.data.type === "cursor") {
      if (now - (attachment.cursorStartedAt || 0) >= CURSOR_WINDOW_MS) {
        attachment.cursorStartedAt = now;
        attachment.cursorCount = 0;
      }
      attachment.cursorCount = (attachment.cursorCount || 0) + 1;
      socket.serializeAttachment(attachment);
      if (attachment.cursorCount > MAX_CURSORS_PER_WINDOW) return;
    } else {
      if (now - (attachment.rateStartedAt || 0) >= MESSAGE_WINDOW_MS) {
        attachment.rateStartedAt = now;
        attachment.rateCount = 0;
      }
      attachment.rateCount = (attachment.rateCount || 0) + 1;
      socket.serializeAttachment(attachment);
      if (attachment.rateCount > MAX_MESSAGES_PER_WINDOW) {
        socket.close(1008, "Message rate exceeded");
        return;
      }
    }
    if (!decoded.ok) return this.error(socket, decoded.error);
    const { data } = decoded;
    const { seat } = attachment;
    if (data.type === "move") {
      if (!seat) return this.error(socket, "spectators_cannot_move");
      if (!this.bothPlayersConnected())
        return this.error(socket, "waiting_for_opponent");
      const result = applyMove(this.game, seat, data.column);
      if (!result.ok) return this.error(socket, result.error);
      await this.save();
      return this.broadcast({
        type: "move",
        move: result,
        game: publicGame(this.game),
      });
    }
    if (data.type === "rematch") {
      if (!seat) return this.error(socket, "spectators_cannot_vote");
      if (!this.bothPlayersConnected())
        return this.error(socket, "waiting_for_opponent");
      if (!this.game.winner && !this.game.draw)
        return this.error(socket, "round_not_over");
      if (!this.game.rematchVotes.includes(seat))
        this.game.rematchVotes.push(seat);
      if (
        this.game.rematchVotes.includes(1) &&
        this.game.rematchVotes.includes(2)
      )
        resetRound(this.game);
      await this.save();
      return this.broadcast({
        type: "state",
        game: publicGame(this.game),
        connected: this.connectedSeats(),
      });
    }
    if (data.type === "cursor") {
      if (!seat) return;
      if (!this.bothPlayersConnected() || seat !== this.game.turn) return;
      if (!Number.isInteger(data.column) || data.column < 0 || data.column > 6)
        return;
      return this.broadcast(
        { type: "cursor", seat, column: data.column },
        socket,
      );
    }
    if (data.type === "reaction") {
      if (!seat || !REACTIONS.has(data.reaction)) return;
      if (now - (attachment.lastReactionAt || 0) < REACTION_COOLDOWN_MS)
        return this.error(socket, "reaction_cooldown");
      attachment.lastReactionAt = now;
      socket.serializeAttachment(attachment);
      return this.broadcast({
        type: "reaction",
        seat,
        reaction: data.reaction,
      });
    }
    if (data.type === "state")
      return socket.send(
        JSON.stringify({
          type: "state",
          game: publicGame(this.game),
          connected: this.connectedSeats(),
        }),
      );
    this.error(socket, "unknown_message_type");
  }

  async webSocketClose(socket) {
    await this.ready;
    const attachment = socket.deserializeAttachment() || {};
    if (
      attachment.seat &&
      this.game.players[attachment.seat] === attachment.token
    ) {
      this.game.lastSeen ??= { 1: 0, 2: 0 };
      this.game.lastSeen[attachment.seat] = Date.now();
      this.ctx.waitUntil(this.save());
    }
    this.broadcast({
      type: "presence",
      connected: this.connectedSeats(attachment.connectionId, attachment.seat),
      game: publicGame(this.game),
    });
  }

  webSocketError(socket) {
    try {
      socket.close(1011, "WebSocket error");
    } catch {}
  }

  seatForToken(token) {
    if (this.game.players[1] === token) return 1;
    if (this.game.players[2] === token) return 2;
    return 0;
  }

  connectedSeats(exceptConnectionId = null, legacyExceptSeat = 0) {
    let skippedLegacySocket = false;
    const seats = new Set();
    for (const socket of this.ctx.getWebSockets()) {
      const attachment = socket.deserializeAttachment() || {};
      if (exceptConnectionId && attachment.connectionId === exceptConnectionId)
        continue;
      if (!exceptConnectionId && legacyExceptSeat && !skippedLegacySocket && attachment.seat === legacyExceptSeat) {
        skippedLegacySocket = true;
        continue;
      }
      if (attachment.seat) seats.add(attachment.seat);
    }
    return [...seats].sort();
  }

  bothPlayersConnected() {
    const seats = this.connectedSeats();
    return seats.includes(1) && seats.includes(2);
  }

  broadcast(payload, except = null) {
    const encoded = JSON.stringify(payload);
    for (const socket of this.ctx.getWebSockets()) {
      if (socket === except) continue;
      try {
        socket.send(encoded);
      } catch {}
    }
  }

  error(socket, error) {
    try {
      socket.send(JSON.stringify({ type: "error", error }));
    } catch {}
  }

  save() {
    return Promise.all([
      this.ctx.storage.put("game", this.game),
      this.ctx.storage.setAlarm(Date.now() + ROOM_TTL_MS),
    ]);
  }

  async alarm() {
    if (this.ctx.getWebSockets().length > 0) {
      await this.ctx.storage.setAlarm(Date.now() + ROOM_TTL_MS);
      return;
    }
    // The in-memory game is recreated with a fresh timestamp on every wake, so
    // expiry must be judged from persisted state only. A room that was
    // reserved but never played has no stored game and expires from its
    // reservation time instead of rescheduling itself forever.
    const storedGame = await this.ctx.storage.get("game");
    const reservedAt = (await this.ctx.storage.get("reservedAt")) ?? 0;
    const lastActivity = storedGame
      ? Number.isFinite(storedGame.updatedAt)
        ? storedGame.updatedAt
        : 0
      : reservedAt;
    const remaining = lastActivity + ROOM_TTL_MS - Date.now();
    if (remaining > 0) {
      await this.ctx.storage.setAlarm(Date.now() + remaining);
      return;
    }
    await this.ctx.storage.deleteAll();
    this.game = createGame();
    this.reserved = false;
  }
}
