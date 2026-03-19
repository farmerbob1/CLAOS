# CLAOS DOOM: Descent into Chaos
## Game Design Document

---

## Concept

**CLAOS DOOM** (working title: "Descent into Chaos") is a Doom-style first-person shooter that runs natively on CLAOS. It's not a Doom port — it's an original game built from scratch on the CLAOS BSP engine, with a twist: **Claude is integrated into the gameplay.** Claude can generate level descriptions, narrate the story, give hints, and even modify the game world via Lua while you play.

This is the ultimate CLAOS demo — a playable FPS running on a from-scratch OS, rendered by a from-scratch 3D engine, scripted in Lua, with an AI narrator providing real-time commentary.

---

## Story

You are a system administrator trapped inside CLAOS after a catastrophic kernel panic. The OS has become corrupted, and you must fight through corrupted memory sectors, debug hostile processes, and reach the kernel core to trigger a clean reboot.

Claude serves as your "system voice" — an AI guide who can see the OS state and provides commentary, warnings, and hints. But Claude's connection is unstable — sometimes the signal degrades and Claude's messages become garbled or delayed.

**Setting:** The inside of a computer. Levels are themed around OS concepts:
- Memory corridors (long hallways of register banks)
- Process chambers (rooms where hostile tasks spawn)
- Network tunnels (connected areas with data flowing through)
- The Kernel Core (final level — the heart of CLAOS)

---

## Gameplay

### Core Loop

1. Explore the level
2. Find keycard/access tokens to unlock doors
3. Fight corrupted processes (enemies)
4. Find the exit sector to advance to the next level
5. Claude provides real-time commentary and hints

### Player

```lua
player = {
    health = 100,      -- max 100
    armor = 0,          -- max 100, absorbs 50% damage
    ammo = {
        bits = 50,      -- pistol ammo (common)
        packets = 0,    -- shotgun ammo
        cycles = 0,     -- chaingun ammo
        cores = 0,      -- rocket launcher ammo (rare)
    },
    weapons = {
        fist = true,    -- always available
        pistol = true,  -- start with this
        shotgun = false,
        chaingun = false,
        debugger = false,  -- rocket launcher equivalent
    },
    current_weapon = "pistol",
    keys = {
        blue = false,   -- blue access token
        red = false,    -- red access token
        yellow = false, -- yellow access token
    },
    speed = FP(3.0),         -- units per second
    turn_speed = FP(2.5),    -- radians per second
    eye_height = FP(41),     -- 41 units (player is 56 tall)
    radius = FP(16),         -- collision radius
}
```

### Weapons

| Weapon | Ammo Type | Damage | Rate of Fire | Notes |
|--------|-----------|--------|-------------|-------|
| Fist | None | 10 | Slow | Melee range only |
| Bit Pistol | Bits | 15 | Medium | Starting weapon, accurate |
| Packet Shotgun | Packets | 7x10 | Slow | Spread, devastating close range |
| Cycle Chaingun | Cycles | 12 | Fast | Burns through ammo |
| Debugger (BFG) | Cores | 100 | Very slow | Splash damage, rare ammo |

Weapon sprites are displayed at the bottom of the screen — a 2D overlay drawn on top of the 3D view. Each weapon has frames for: idle, firing, muzzle flash.

### Enemies

All enemies are corrupted OS processes. Each has distinct behaviour:

| Enemy | Health | Damage | Speed | Behaviour |
|-------|--------|--------|-------|-----------|
| Zombie Process | 20 | 5 | Slow | Shuffles toward player, melee attack |
| Fork Bomb | 40 | 15 | Medium | Splits into two when hit (once only), ranged attack |
| Rogue Thread | 60 | 20 | Fast | Quick, erratic movement, charges in straight lines |
| Memory Leak | 100 | 10/sec | Static | Stationary, damages player in radius, must be destroyed |
| Buffer Overflow | 150 | 30 | Medium | Tanky, throws data corruptions (projectiles) |
| **Kernel Panic** | 500 | 50 | Slow | Boss enemy. E1M8. Massive sprite, arena fight |

**Enemy AI (Lua):**
```lua
-- Each enemy type has a Lua behaviour script
-- Example: fork_bomb.lua
function fork_bomb_think(enemy, player, dt)
    if enemy.health < enemy.max_health / 2 and not enemy.has_split then
        -- Split into two weaker copies
        enemy.has_split = true
        spawn_enemy("fork_bomb_child", enemy.x + 1, enemy.y)
        spawn_enemy("fork_bomb_child", enemy.x - 1, enemy.y)
    end
    
    local dist = distance(enemy, player)
    if dist < 8 and has_line_of_sight(enemy, player) then
        -- Ranged attack
        enemy_fire(enemy, player)
    else
        -- Move toward player
        move_toward(enemy, player, dt)
    end
end
```

### Items

| Item | Effect | Sprite |
|------|--------|--------|
| Health Patch | +10 health | Green cross |
| Medkit | +25 health | White box with cross |
| Armor Shard | +5 armor | Blue shard |
| Full Armor | +100 armor | Blue vest |
| Bit Clip | +20 bits | Small ammo box |
| Packet Box | +5 packets | Red shells |
| Cycle Belt | +50 cycles | Ammo belt |
| Core Cell | +1 core | Glowing orb |
| Blue Key | Opens blue doors | Blue card |
| Red Key | Opens red doors | Red card |
| Yellow Key | Opens yellow doors | Yellow card |
| Overclock | Temporary 2x speed, 30s | Lightning bolt |

### Doors and Switches

- **Keyed doors:** Require specific colour key to open. Door linedef has a key requirement flag.
- **Switch doors:** Activated by a switch linedef elsewhere in the level. Lua triggers door open/close.
- **Auto doors:** Open when player gets close, close after delay.
- **Door animation:** Ceiling lowers (or raises), changing sector ceiling height over time. Lua handles the animation via `claos.gui3d.set_sector_ceiling()`.

---

## Level Design

### Episode 1: Corrupted Memory (E1M1 - E1M8)

| Level | Name | Theme | Key Enemies | Keys |
|-------|------|-------|-------------|------|
| E1M1 | Boot Sector | Tutorial, simple layout | Zombie Processes | None |
| E1M2 | Stack Overflow | Vertical, stacked rooms | Zombies, Fork Bombs | Blue |
| E1M3 | Heap Corruption | Open areas, scattered cover | Fork Bombs, Rogue Threads | Blue, Red |
| E1M4 | Page Fault | Maze-like, dead ends | Rogue Threads, Memory Leaks | Blue |
| E1M5 | Segmentation Fault | Split level, two paths | All basic enemies | Red, Yellow |
| E1M6 | Race Condition | Non-linear, multiple routes | Buffer Overflows | Blue, Red |
| E1M7 | Deadlock | Circular level, locked sections | All enemies, heavy | All three |
| E1M8 | Kernel Core | Boss arena | **Kernel Panic (boss)** | None |

### Map Format

Maps are defined in a simple text format on the host, compiled to .bsp with the BSP builder.

```
# e1m1.map — Boot Sector
# Simple format: vertices, linedefs, sectors, things

vertices:
0: 0, 0
1: 256, 0
2: 256, 256
3: 0, 256
4: 128, 0
5: 128, 128
# ...

sectors:
0: floor=0, ceil=128, ftex=floor_metal, ctex=ceil_tech, light=200
1: floor=0, ceil=128, ftex=floor_dark, ctex=ceil_tech, light=160
# ...

linedefs:
0: v0=0, v1=1, front=0, back=-1, mid=wall_brick
1: v1=1, v1=2, front=0, back=1, mid=-1, upper=wall_brick, lower=wall_brick
# ...

things:
player_start: x=64, y=64, angle=0
zombie: x=200, y=200, angle=180
health: x=128, y=64
shotgun: x=192, y=128
# ...
```

---

## HUD (Heads-Up Display)

Drawn in 2D over the 3D view. Implemented in Lua.

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│                     3D Game View                            │
│                                                             │
│                                                             │
│                                                             │
│                                                             │
│                                                             │
│                    [weapon sprite]                           │
├─────────────────────────────────────────────────────────────┤
│ HEALTH: 85   ARMOR: 50   BITS: 42   ██ PACKET SHOTGUN      │
│ ▓▓▓▓▓▓▓▓░░  ▓▓▓▓▓░░░░░                   E1M3             │
│                           [Claude: "Watch the corner..."]   │
└─────────────────────────────────────────────────────────────┘
```

- **Health bar:** Left side, red gradient
- **Armor bar:** Below health, blue gradient
- **Ammo count:** Current weapon's ammo count, centre
- **Weapon name:** Right of ammo
- **Level name:** Bottom right
- **Claude message:** Bottom centre, fades in/out. Claude sends contextual messages.

HUD is rendered in Lua using `claos.gui.*` 2D primitives drawn on top of the 3D buffer.

---

## Claude Integration (The Unique Selling Point)

This is what makes CLAOS DOOM unlike any other Doom clone.

### Real-Time AI Commentary

During gameplay, CLAOS periodically sends game state to Claude, and Claude responds with contextual commentary displayed on the HUD:

```lua
-- Every 30 seconds, or on special events
function send_game_state_to_claude()
    local state = string.format(
        "Player in %s. Health: %d. Enemies nearby: %d. " ..
        "Ammo low: %s. Deaths this level: %d. " ..
        "Provide a short in-character comment (max 50 words) as the CLAOS system AI.",
        current_level.name,
        player.health,
        count_nearby_enemies(8),
        is_ammo_low() and "yes" or "no",
        death_count
    )
    
    local response = claos.ask(state)
    show_hud_message(response, 5)  -- display for 5 seconds
end
```

**Claude might say things like:**
- "Multiple hostile processes detected in the next sector. Your packet reserves are low — consider melee."
- "You've died 4 times on this level. The blue key is in the northeast corridor. I'm not judging."
- "WARNING: Kernel Panic detected in sector 7. That's the boss. Prepare yourself."
- "I can see your health is at 12. There's a medkit behind the column to your left. You're welcome."

### Event-Triggered Commentary

| Event | Claude Prompt |
|-------|--------------|
| Player enters new level | "Player entered [level]. Set the scene in 1-2 sentences." |
| Player takes heavy damage | "Player took heavy damage, health now [X]. React briefly." |
| Player finds a secret | "Player found a secret area. Congratulate them." |
| Player kills boss | "Player defeated [boss]. Celebrate dramatically." |
| Player dies | "Player died in [level]. Provide a witty respawn comment." |
| Player is lost | "Player hasn't progressed in 2 minutes. Give a subtle hint." |
| Low ammo + many enemies | "Player is low on ammo with [N] enemies ahead. Comment." |

### Claude-Generated Content (Stretch Goal)

In the ultimate version, Claude could:
- Generate level descriptions and story text between levels
- Create random enemy placement for procedural levels
- Write custom Lua scripts for unique enemy behaviours
- Describe items when the player picks them up

---

## Controls

```
W / Up Arrow    — Move forward
S / Down Arrow  — Move backward
A              — Strafe left
D              — Strafe right
Left Arrow     — Turn left
Right Arrow    — Turn right
Space / Ctrl   — Fire weapon
1-5            — Switch weapon
E              — Use/interact (doors, switches)
Tab            — Show automap
Escape         — Pause menu
```

All input handled in Lua via `claos.gui.poll_event()`.

---

## Automap

A 2D top-down view of the level, drawn with `claos.gui.*` 2D primitives:
- Lines for walls (white = seen, gray = unseen, red = locked doors)
- Player position as an arrow
- Enemy positions as dots (if in line of sight)
- Toggle with Tab

---

## Technical Requirements

- **Engine:** CLAOS Phase 9 BSP renderer
- **Resolution:** Full screen (whatever VBE mode is set)
- **Textures:** 64x64 and 128x128, .ctx format, power-of-2
- **Sprites:** Variable size, .ctx format with color key transparency (magenta = transparent)
- **Maps:** Pre-compiled .bsp files on ChaosFS
- **Sound:** None (CLAOS has no audio driver — stretch goal for Phase 10?)
- **Music:** None (but Claude can provide "mood text" instead)

---

## File Structure on ChaosFS

```
/games/doom/
├── main.lua              -- Game entry point
├── game.lua              -- Core game loop
├── player.lua            -- Player state, movement, weapons
├── enemies.lua           -- Enemy definitions and AI
├── items.lua             -- Item definitions and pickup logic
├── hud.lua               -- HUD rendering
├── menu.lua              -- Title screen, pause menu
├── automap.lua           -- Automap renderer
├── claude_narrator.lua   -- Claude AI commentary system
├── maps/
│   ├── e1m1.bsp          -- Pre-compiled BSP levels
│   ├── e1m2.bsp
│   └── ...
├── textures/
│   ├── wall_brick.ctx    -- Wall textures
│   ├── wall_tech.ctx
│   ├── floor_metal.ctx
│   ├── ceil_tech.ctx
│   └── ...
├── sprites/
│   ├── pistol_idle.ctx   -- Weapon sprites
│   ├── pistol_fire.ctx
│   ├── zombie_front.ctx  -- Enemy sprites (8 angles)
│   ├── zombie_walk1.ctx
│   ├── health.ctx        -- Item sprites
│   └── ...
└── config.lua            -- Key bindings, settings
```

---

## Game Launch

From the CLAOS shell:
```
claos> lua /games/doom/main.lua
```

Or from the GUI file browser: click on main.lua.

Or add a dedicated icon to the GUI sidebar.

main.lua:
```lua
-- /games/doom/main.lua
-- CLAOS DOOM: Descent into Chaos

claos.print("CLAOS DOOM: Descent into Chaos")
claos.print("Loading engine...")

-- Load game modules
local game = dofile("/games/doom/game.lua")
local player = dofile("/games/doom/player.lua")
local enemies = dofile("/games/doom/enemies.lua")
local hud = dofile("/games/doom/hud.lua")
local menu = dofile("/games/doom/menu.lua")
local narrator = dofile("/games/doom/claude_narrator.lua")

-- Initialize 3D engine
claos.gui3d.load_level("/games/doom/maps/e1m1.bsp")

-- Load textures
claos.gui3d.load_texture("/games/doom/textures/wall_brick.ctx")
-- ... etc

-- Show title screen
menu.show_title()

-- Game loop
narrator.introduce_level("E1M1", "Boot Sector")

while game.running do
    local dt = claos.gui.ticks()  -- delta time
    
    -- Input
    local event = claos.gui.poll_event()
    while event do
        game.handle_input(event)
        event = claos.gui.poll_event()
    end
    
    -- Update
    player.update(dt)
    enemies.update_all(dt)
    game.check_triggers()
    narrator.update(dt)  -- periodic Claude commentary
    
    -- Render 3D
    claos.gui3d.set_camera(player.x, player.y, player.eye_z, player.angle)
    claos.gui3d.render_to_screen()
    
    -- Render 2D overlay
    hud.draw(player)
    narrator.draw_message()
    
    -- Present
    claos.gui.swap()
end

claos.print("Thanks for playing CLAOS DOOM!")
```

---

## Art Style

Since we're creating all assets from scratch and have limited texture resolution:

- **Abstract/tech aesthetic** — circuit board patterns, glowing grid lines, data streams
- **High contrast** — dark bases with bright accent colours for readability
- **Purple accent** — CLAOS purple (#7F77DD) appears in UI elements and special effects
- **Green for health/good** — pickups, safe areas
- **Red for danger** — enemies, damage, locked doors
- **Procedural textures** — consider generating some textures in Lua at load time (noise patterns, gradients) to save disk space

Texture creation tool on host:
```bash
python tools/mkctx.py source.png -o texture.ctx --resize 64x64
```

---

## Success Criteria

The game is "shipped" when:

1. Player can navigate E1M1 in first person with smooth movement
2. Textured walls, floors, and ceilings render correctly
3. At least 3 enemy types are functional with basic AI
4. Weapons work — shoot enemies, they die
5. Items can be picked up (health, ammo, keys)
6. Doors open with keys
7. Claude provides real-time commentary during gameplay
8. Player can complete E1M1 by reaching the exit
9. HUD displays health, ammo, and Claude messages
10. It feels like playing Doom inside a custom OS. Because it is.

**Bonus:**
- All 8 levels playable
- Boss fight works
- Automap works
- Claude narrates the entire playthrough

---

## The Ultimate Screenshot

Imagine this screenshot posted online:

> "I built an operating system from scratch — custom bootloader, custom kernel,
> custom TCP/IP stack, custom TLS implementation, custom filesystem — and then
> I built a Doom clone that runs on it. The AI that designed the OS architecture
> provides real-time commentary while you play."

That's not a meme anymore. That's a legend.
