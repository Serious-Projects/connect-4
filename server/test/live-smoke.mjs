import assert from "node:assert/strict";
import WebSocket from "ws";

const origin = "https://connect-four-relay.ayushdedhia25.workers.dev";
const websocketOrigin = origin.replace("https://", "wss://");

class Inbox {
  constructor(socket) {
    this.messages = [];
    this.waiters = [];
    socket.on("message", (raw) => {
      const message = JSON.parse(raw.toString());
      const waiter = this.waiters.find(({ predicate }) => predicate(message));
      if (waiter) {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        clearTimeout(waiter.timer);
        waiter.resolve(message);
      } else this.messages.push(message);
    });
  }

  wait(predicate, label, timeout = 8000) {
    const index = this.messages.findIndex(predicate);
    if (index >= 0) return Promise.resolve(this.messages.splice(index, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter = { predicate, resolve };
      waiter.timer = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        reject(new Error(`Timed out waiting for ${label}`));
      }, timeout);
      this.waiters.push(waiter);
    });
  }
}

async function connect(room, name, token = "") {
  const socket = new WebSocket(
    `${websocketOrigin}/rooms/${room}?name=${encodeURIComponent(name)}${token ? `&token=${encodeURIComponent(token)}` : ""}`,
  );
  const inbox = new Inbox(socket);
  await new Promise((resolve, reject) => {
    socket.once("open", resolve);
    socket.once("error", reject);
  });
  const welcome = await inbox.wait((message) => message.type === "welcome", "welcome");
  return { socket, inbox, welcome };
}

async function expectMove(client, column) {
  return client.inbox.wait(
    (message) => message.type === "move" && message.move.column === column,
    `move in column ${column}`,
  );
}

let health;
for (let attempt = 0; attempt < 15; ++attempt) {
  health = await fetch(`${origin}/health?attempt=${attempt}`, { cache: "no-store" }).then((response) => response.json());
  if (health.version === 8) break;
  await new Promise((resolve) => setTimeout(resolve, 2000));
}
assert.equal(health.version, 8);
const created = await fetch(`${origin}/rooms`, { method: "POST" }).then((response) => response.json());
assert.match(created.room, /^[A-HJ-NP-Z2-9]{6}$/);

const joined = await Promise.all([connect(created.room, "J@ay"), connect(created.room, "Developer")]);
const host = joined.find((client) => client.welcome.seat === 1);
const guest = joined.find((client) => client.welcome.seat === 2);
assert.ok(host && guest);
host.socket.send(JSON.stringify({ type: "state" }));
const joinedState = await host.inbox.wait((message) => message.type === "state", "joined state");
assert.deepEqual(Object.values(joinedState.game.names).sort(), ["Developer", "Jay"]);

const spectator = await connect(created.room, "Watcher");
assert.equal(spectator.welcome.seat, 0);
const spectatorClosed = new Promise((resolve) => spectator.socket.once("close", (code) => resolve(code)));
spectator.socket.send("x".repeat(5000));
assert.equal(await spectatorClosed, 1009);

host.socket.send(JSON.stringify({ type: "reaction", reaction: "fire" }));
await Promise.all([
  host.inbox.wait((message) => message.type === "reaction" && message.reaction === "fire", "host reaction"),
  guest.inbox.wait((message) => message.type === "reaction" && message.reaction === "fire", "guest reaction"),
]);
host.socket.send(JSON.stringify({ type: "reaction", reaction: "wow" }));
assert.equal((await host.inbox.wait((message) => message.error === "reaction_cooldown", "reaction cooldown")).error,
             "reaction_cooldown");

host.socket.send("null");
assert.equal((await host.inbox.wait((message) => message.error === "invalid_message", "invalid-message error")).error,
             "invalid_message");
host.socket.send(JSON.stringify({ type: "rematch" }));
assert.equal((await host.inbox.wait((message) => message.error === "round_not_over", "early-rematch error")).error,
             "round_not_over");

const sequence = [0, 1, 0, 1, 0, 1, 0];
for (let index = 0; index < sequence.length; ++index) {
  const actor = index % 2 === 0 ? host : guest;
  actor.socket.send(JSON.stringify({ type: "move", column: sequence[index] }));
  if (index === 0) actor.socket.send(JSON.stringify({ type: "move", column: 6 }));
  const [hostMove, guestMove] = await Promise.all([expectMove(host, sequence[index]), expectMove(guest, sequence[index])]);
  assert.deepEqual(hostMove.game.board, guestMove.game.board);
  if (index === 0)
    assert.equal((await host.inbox.wait((message) => message.error === "not_your_turn", "duplicate move rejection")).error,
                 "not_your_turn");
}
// The winning move was consumed by expectMove, so use the room's read-only snapshot.
const snapshot = await fetch(`${origin}/rooms/${created.room}`).then((response) => response.json());
assert.equal(snapshot.game.winner, 1);
assert.equal(snapshot.game.history.length, 7);

host.socket.send(JSON.stringify({ type: "rematch" }));
await host.inbox.wait((message) => message.type === "state" && message.game.rematchVotes === 1, "first rematch vote");
guest.socket.send(JSON.stringify({ type: "rematch" }));
const reset = await host.inbox.wait(
  (message) => message.type === "state" && message.game.history.length === 0,
  "rematch reset",
);
assert.equal(reset.game.lastReplay.length, 7);
assert.equal(reset.game.turn, 2);

const guestToken = guest.welcome.token;
guest.socket.close(1000, "reconnect test");
await host.inbox.wait(
  (message) => message.type === "presence" && !message.connected.includes(2),
  "guest disconnect presence",
);
const returnedGuest = await connect(created.room, "Returned", guestToken);
assert.equal(returnedGuest.welcome.seat, 2);
assert.equal(returnedGuest.welcome.game.names[2], "Returned");
returnedGuest.socket.close(1000, "test complete");

for (const client of joined) client.socket.close(1000, "test complete");
console.log(`Live relay v8 smoke passed in room ${created.room}.`);
