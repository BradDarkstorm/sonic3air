# Sonic 3 A.I.R. — Online Co-op Edition

Online co-op for 2–8 players over a direct connection. Everyone plays in the host's stage, each with their own character, and everyone can see each other.

This is **phase 1** (the foundation). See "Roadmap" for what comes next.

---

## Playing

### 1. Set up `coop.json` (next to `Sonic3AIR.exe`)

**Host:**
```json
{ "Mode": "host", "Port": 28850, "PlayerName": "Sonic" }
```

**Everyone else:**
```json
{ "Mode": "join", "Address": "HOST'S IP ADDRESS", "Port": 28850, "PlayerName": "Knuckles" }
```

- Use the host's **public IP** (the host can look it up at whatismyip.com), or their LAN IP if you're in the same house.
- Use plain IP addresses; website-style names like `myhost.example.com` don't work yet.
- **The host must forward UDP port 28850** on their router to their PC. Joining players don't need to forward anything.
- If you'd rather not forward ports, tools like ZeroTier, Tailscale or Radmin VPN make a virtual LAN. Then use the host's address from that tool.

### 2. Start the game
- The co-op mod is already installed and turned on in this copy. It uses its own `savedata` folder, so your normal Sonic 3 A.I.R. saves are untouched.
- The first time you start it, it asks for the Sonic 3 & Knuckles ROM, like the normal game does. If you have it on Steam, it finds the ROM automatically.
- The connection starts as soon as the game starts. Status and player names show in the top-left corner while you're in a level.

### 3. Play
- Everyone starts a game (any save slot or no save, any character).
- Joining players get moved to the host's stage automatically, and follow when the host moves on to the next act or zone.
- If you finish act 1 before the host, you stay in act 2; you won't get pulled back. You can't run ahead into a new zone, though.
- Mod settings (Options → Mods → Online Co-op): player names, off-screen markers, solid or see-through players.

### Modded characters
Other players are drawn through the game's own character-mod hook (`Standalone.getModdedAnimationSpriteKey`). If a character mod uses that hook and **you have the same mod enabled**, you'll see that player's modded character.

### Version check
Everyone needs the same co-op mod version. If versions differ, the host turns the joining player away and their screen says why.

---

## Building (no compiler needed on your PC)

GitHub builds the game for you:

1. Fork https://github.com/Eukaryot/sonic3air on GitHub.
2. Add this project's files to your fork (see "Files" below). The easiest way is the GitHub website: **Add file → Upload files**, and drag the folders in.
3. Open the **Actions** tab in your fork. If GitHub asks, click "I understand my workflows, go ahead and enable them".
4. Pick **Build Co-op (Windows)** → **Run workflow**.
5. When it finishes (about 30 min the first time, faster after that), download **Sonic3AIR-Coop-Windows** from the bottom of the run page. That zip is the game.

### Updating to a new Sonic 3 A.I.R. version
On your fork's GitHub page, click **Sync fork**, then run the workflow again. The engine changes only touch 2 lines in existing game files, so syncing almost never conflicts. If a game update changes something the mod relies on, only the mod's scripts need fixing.

---

## Files

| Path | What it is |
|---|---|
| `Oxygen/oxygenengine/source/oxygen/network/coopnet/CoopNet.h/.cpp` | **New.** The networking layer. It has no Sonic-specific code; it just passes messages between players. |
| `Oxygen/oxygenengine/source/oxygen/application/Application.cpp` | **2 lines added** (marked `// CoopNet addition`): keeps the connection running. |
| `Oxygen/oxygenengine/source/oxygen/simulation/bindings/LemonScriptBindings.cpp` | **4 lines added**: lets mod scripts use the `Net.*` functions. |
| `Oxygen/oxygenengine/build/_vstudio/oxygen.vcxproj` (+ `.filters`) | Adds the new files to the Windows build. |
| `.github/workflows/build-coop-windows.yml` | The GitHub build. |
| `coop/mod/S3AIR Online Coop/` | The co-op script mod (all gameplay sync lives here). |
| `coop/coop.json`, `coop/README.md` | Example config and this guide. |

### `Net.*` functions available to scripts
```
Net.host(u16 port) -> bool             Net.join(string address, u16 port) -> bool
Net.startFromConfig() -> bool          Net.leave()
Net.setSignature(string)               Net.setPlayerName(string)
Net.getState() -> u8                   Net.isHost() / Net.isOnline() -> bool
Net.getLocalPlayerId() -> u8           Net.getNumPlayers() / Net.getMaxPlayers() -> u8
Net.isPlayerConnected(u8) -> bool      Net.getPlayerName(u8) -> string
Net.getStatusText() -> string
Net.send(u8 target, u32 address, u16 bytes, bool reliable) -> bool      (target 0xff = everyone)
Net.receive(u32 address, u16 maxBytes) -> u16 size (0 = nothing)      Net.getLastSender() -> u8
```
Player 0 is always the host. The host passes messages between joining players automatically.

---

## Roadmap

| Phase | Feature | Status |
|---|---|---|
| 1 | Host/join, everyone in the host's stage, seeing each other (including modded characters) | **Done** |
| 2 | Enemy kills synced: an enemy one player kills is gone for everyone (by its level slot, not its position) | Next |
| 2 | Teleport to another player from the pause menu | Next |
| 3 | Boss health shared; the host keeps the real hit count | Planned |
| 3 | Dying during a boss fight: watch the others until the boss is beaten | Planned |
| 3 | Options: pull everyone to the boss when one player arrives, or wait for everyone before it starts | Planned |
| 4 | Special stages: everyone enters together; a waiting screen until all finish; the emerald counts if **anyone** wins | Planned |
| 5 | Tails can carry other players | Planned |

## Testing status (phase 1)
- The networking layer was tested over real UDP with 3 players: joining, player list, messages to everyone or to one player, passing messages through the host, version mismatch rejection, and players leaving. All passed.
- The patched game builds and links. The co-op mod compiles in the game, and two game copies connect through `coop.json` (host log: "Player 2 'JoinKnux' joined", later "left").
- **Not yet tested in a real level.** Drawing other players and following the host's stage need an actual playthrough with the ROM. Please report anything that looks off, such as players drawn in the wrong spot or wrong animation frames.
