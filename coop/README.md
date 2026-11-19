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
Each game records the exact sprites it draws for its own character, plus that character's colours, and sends them to everyone. Other games redraw exactly those sprites. This works with any character mod, however it's built internally.
- If **you have the same character mod installed** (it doesn't need to be the character you're playing), you see that player's modded character with their colours.
- If you **don't** have it, you see the original character in its original colours.
- Up to 3 other players get their own colours (spare palette lines 10–15). With more players, the extra ones use your game's colours for their character.

### Version check
Everyone needs the same co-op mod version. If versions differ, the host turns the joining player away and their screen says why.

---

## Building (no compiler needed on your PC)

GitHub builds the game for you. Each build downloads the **newest stable** Sonic 3 A.I.R. release from Eukaryot's repository and applies the co-op changes on top. Stable is what character mods are made for.

1. Your GitHub repository needs just two things: the `coop` folder, and the workflow file at `.github/workflows/` (any name ending in `.yml`).
2. Open the **Actions** tab. If GitHub asks, click "I understand my workflows, go ahead and enable them".
3. Pick **Build Co-op (Windows)** → **Run workflow**. It also runs automatically whenever you change files in the repository.
4. When it finishes (about 30 min the first time, faster after that), download **Sonic3AIR-Coop-Windows** from the bottom of the run page. That zip is the game. `GAME VERSION.txt` inside says which release it's built on.

### Updating to a new Sonic 3 A.I.R. version
Just run the workflow again; it picks up the newest stable release by itself. If a game update moved one of the few spots the co-op changes hook into, the "Apply co-op changes" step fails and says exactly which file and spot. In **Run workflow** you can also type a specific version, like `v26.03.28.0-stable`, or `master` for the newest test version.

---

## Files

| Path | What it is |
|---|---|
| `coop/engine/CoopNet.h/.cpp` | The engine addition. It has no Sonic-specific code: it passes messages between players and can record which sprites were drawn for a given object. |
| `coop/apply_coop.py` | Copies those files into the game source and adds a few marked lines (`// CoopNet addition`) to 4 existing engine files and the Visual Studio project. Each edit looks for a short piece of the original text, so it keeps working across game versions. |
| `coop/mod/S3AIR Online Coop/` | The co-op script mod (all gameplay sync lives here). |
| `coop/coop.json`, `coop/README.md` | Example config and this guide. |
| `.github/workflows/build-coop-windows.yml` | The GitHub build. |

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
Net.setCaptureTag(u8 index, u64 spriteTag)                              (index 0..3, tag 0 = off)
Net.getCapturedSprites(u32 address, u8 maxCount) -> u8 count            (sprites drawn last frame with those tags, 0x20 bytes each)
```
Player 0 is always the host. The host passes messages between joining players automatically.

---

## Roadmap

| Phase | Feature | Status |
|---|---|---|
| 1 | Host/join, everyone in the host's stage, seeing each other (including modded characters and their colours) | **Done** |
| 2 | Enemy kills synced: an enemy one player kills is gone for everyone (by its level slot, not its position) | Next |
| 2 | Teleport to another player from the pause menu | Next |
| 3 | Boss health shared; the host keeps the real hit count | Planned |
| 3 | Dying during a boss fight: watch the others until the boss is beaten | Planned |
| 3 | Options: pull everyone to the boss when one player arrives, or wait for everyone before it starts | Planned |
| 4 | Special stages: everyone enters together; a waiting screen until all finish; the emerald counts if **anyone** wins | Planned |
| 5 | Tails can carry other players | Planned |

## Testing status
- **Networking:** tested over real UDP with 3 players: joining, player list, messages to everyone or to one player, the host passing messages along, version mismatch rejection, and players leaving. All passed.
- **Builds:** the co-op changes apply to both stable 26.03.28.0 and the newest test version, and stable plus co-op builds and links.
- **Mods together:** the co-op mod compiles and starts its connection alongside Reimu Rei-mu'd and Amy Galore Plus, in any load order. Two game copies connect.
- **Played in a real level (version 0.1):** both players in the same stage, in the right positions, with the right animations.
- **Not yet seen in a real level:** replaying modded character sprites and sending each player's colours (new in 0.3).
