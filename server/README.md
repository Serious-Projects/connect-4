# Connect Four Relay

Authoritative Cloudflare Worker and SQLite-backed Durable Object for private
two-player Connect Four rooms.

```powershell
npm install
npm test
npx wrangler deploy
```

After deployment, `npm run test:live` exercises two real WebSocket clients,
seat assignment, malformed input, reactions, moves, wins, rematches, presence,
and reconnect-token recovery against the production relay.

The service exposes `GET /health`, `POST /rooms`, and WebSocket connections at
`/rooms/{CODE}?name={NAME}&token={RECONNECT_TOKEN}`. The server owns the board,
turn validation, wins, draws, presence, reconnection seats, and rematches.
