# Connect Four: Neon Duel

A modern native Connect Four game with a C++23/raylib client and an
authoritative Cloudflare Worker relay for private online matches.

The Windows build bundles its interface and emoji fonts, so it does not depend
on fonts installed on the player's computer. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for font attribution.

## Repository layout

```text
client/            Native Windows client and logic tests
  assets/fonts/    Bundled OFL-licensed interface and emoji fonts
  src/app/         Application orchestration and shared view state
  src/audio/       Audio device, generated sounds, and settings
  src/core/        Platform-independent Connect Four rules
  src/network/     Relay protocol and WebSocket client
  src/storage/     Local reconnect-session persistence
  src/ui/          Fonts, drawing primitives, and overlays
  tests/           Native logic and replay tests
server/            Cloudflare Worker and Durable Object relay
scripts/           Repository build scripts
build/             Generated test artifacts (ignored)
dist/              Packaged game binaries (ignored)
```

## Build and test

Install MSYS2 UCRT64 with GCC and raylib, then ensure `g++.exe` is available on
`PATH`. Alternatively, pass the UCRT64 directory through `-ToolchainPath` or
the `MSYS2_UCRT64` environment variable.

```powershell
.\scripts\build.ps1
.\dist\windows\connect_four.exe
.\scripts\test-live-client.ps1

cd .\server
npm test
npm run test:live
```

`npm test` is local and deterministic. `npm run test:live` creates a temporary
room against the deployed relay and exercises a complete two-player session.
`test-live-client.ps1` verifies the native WinHTTP connection and bounded,
stale-event-free disconnect path against that same relay.

See [client/README.md](client/README.md) for controls and
[server/README.md](server/README.md) for the relay API.
