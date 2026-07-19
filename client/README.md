# Connect Four: Neon Duel Client

A modern Connect Four game built with C++23 and raylib 6. It supports local
two-player play and private online rooms through the deployed Cloudflare relay.

```powershell
.\scripts\build.ps1
.\dist\windows\connect_four.exe
```

Click a column, use `A`/`D` plus Enter, or press `1`-`7` to drop. `U` undoes
the latest local move, `R` starts a rematch while preserving scores, and `M`
resets the local match. `Esc` exits gracefully.

## Online play

Press `O` in the game to open the online lobby:

- Host: click Create room (or press `F2`), then send the displayed
  six-character code to a friend. In the match, `C` copies that code.
- Enter a display name before creating or joining. The first seat is Host
  (coral); the second is Guest (gold).
- Friend: click the code field, type or paste the code with `Ctrl+V`, then
  click Join or press Enter.
- The relay assigns coral to the host and gold to the friend. The server checks
  every move, turn, win, and rematch vote.
- The match waits until both players are connected. Opponent aim appears as a
  translucent marker, and interrupted clients reconnect to their original seat.
- The online badge displays measured relay latency: green is healthy, gold is
  elevated, and coral indicates a poor or interrupted connection.
- Press `E` for the reaction wheel, then choose reaction `1`-`4`.
- Press `S` for persistent master, disc-effect, and celebration volume controls.
- After a completed round, press `P` for an animated replay. Space pauses,
  `[`/`]` change speed, Home restarts, and `P` exits.
- Press `F1` to return to local play. In online mode, `R` casts a rematch vote;
  both players must vote before the next game begins.

Private rooms expire after 24 hours of inactivity.

## Code structure

- `src/main.cpp` is the minimal process entry point.
- `src/app/game_application.cpp` coordinates input, game state, networking, animation,
  and rendering.
- `src/audio/audio_manager.*` owns the audio device, generated sounds, persistent volume
  settings, and cleanup.
- `src/ui/font_assets.*` owns UI font loading, fallback selection, and cleanup.
- `src/storage/session_store.*` owns reconnect-session persistence and validation.
- `src/app/app_types.hpp` contains shared visual state and layout/theme constants.
- `src/ui/ui_primitives.*` contains reusable drawing and board-hit-test helpers.
- `src/ui/overlays.*` owns online-lobby, reaction, and sound-panel rendering.
- `src/core/connect_four.hpp` remains the independent, tested game-rules model.
- `src/network/online_client.hpp` remains the transport and relay-protocol client.
