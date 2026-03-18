# CLAOS — Claude Assisted Operating System
## Master Build Prompt for Claude Code (v4 — C Renderer + Lua GUI)

---

## Project Overview

Build **CLAOS** (pronounced "Chaos") — a toy x86 operating system written entirely from scratch with NO dependency on Linux, Windows, macOS, or any existing OS kernel. CLAOS is an "AI-native OS" where Claude (Anthropic's AI) is integrated at the kernel level as a core system service. Claude can be prompted interactively from the OS shell, receives crash/panic reports automatically, and can send back patches or commands.

This is a meme project and educational toy — not a production OS. Prioritize "cool and functional demo" over robustness.

**CRITICAL DESIGN DECISION: CLAOS connects to the Anthropic API directly via native HTTPS. No relay. No host-side scripts. The OS handles TCP/IP, TLS, and HTTPS entirely on its own using a ported BearSSL library. This means CLAOS can run on real bare-metal hardware with just an internet connection.**

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│                  CLAOS Stack                      │
├──────────────────────────────────────────────────┤
│  Layer 8: GUI — Lua UI Scripts (on ChaosFS)       │
│           desktop.lua, widgets, apps, themes      │
├──────────────────────────────────────────────────┤
│  Layer 7: GUI — C Rendering Engine (in kernel)    │
│           Framebuffer, drawing primitives,         │
│           compositor, font renderer, input events  │
├──────────────────────────────────────────────────┤
│  Layer 6: Lua 5.5 Scripting Environment           │
│           ClaudeScript API bindings               │
├──────────────────────────────────────────────────┤
│  Layer 5: ClaudeShell (interactive AI shell)       │
├──────────────────────────────────────────────────┤
│  Layer 4: Claude Protocol Layer + HTTPS Client    │
│           JSON over HTTP/1.1 + TLS 1.2 (BearSSL) │
├──────────────────────────────────────────────────┤
│  Layer 3: Network Stack                           │
│           e1000 NIC → Ethernet → ARP → IPv4 →    │
│           DNS (UDP) → TCP                         │
├──────────────────────────────────────────────────┤
│  Layer 2: Drivers + Filesystem                    │
│           VGA, PS/2 keyboard+mouse, PIT timer,    │
│           e1000 NIC, ATA/IDE, ChaosFS             │
├──────────────────────────────────────────────────┤
│  Layer 1: Kernel                                  │
│           GDT, IDT, ISRs, physical/virtual        │
│           memory manager, basic scheduler,        │
│           panic handler → Claude                  │
├──────────────────────────────────────────────────┤
│  Layer 0: Bootloader                              │
│           Stage 1 (MBR) → Stage 2 → kernel       │
│           Real mode → Protected mode (32-bit)     │
└──────────────────────────────────────────────────┘
```

### No Relay — Fully Self-Contained

CLAOS talks directly to `api.anthropic.com` over HTTPS. No relay, no host scripts.

---

## Build Phases

### PHASE 1: Boot & Kernel Foundation ✅ COMPLETED
### PHASE 2: Memory Management & Scheduler ✅ COMPLETED
### PHASE 3: Network Stack ✅ COMPLETED
### PHASE 3.5: TLS via BearSSL Port ✅ COMPLETED
### PHASE 4: HTTPS Client & Claude Integration ✅ COMPLETED
### PHASE 5: Interactive Shell ✅ COMPLETED
### PHASE 6: ChaosFS Custom Filesystem ✅ COMPLETED
### PHASE 7: Embedded Lua 5.5 ← IN PROGRESS

**NOTE: Use Lua 5.5.0 (released Dec 2025), NOT 5.4.** Lua 5.5 has 60% better memory usage for arrays and incremental major GC — both important for a 128MB RAM system.

Port Lua 5.5 freestanding, expose CLAOS API bindings including GUI primitives (see Phase 8). Pre-install scripts on ChaosFS.

---

### PHASE 8: GUI — C Rendering Engine + Lua UI Scripts ← BUILD AFTER LUA

**This is the crown jewel of CLAOS.** The GUI uses a two-layer architecture:

- **C Rendering Engine** (compiled into kernel): Fast pixel-level drawing, framebuffer management, font rendering, compositor, double buffering, input dispatch. Think of it like a tiny GPU API.
- **Lua UI Scripts** (stored on ChaosFS): All layout, styling, widget logic, themes, and app windows are written in Lua. Claude can modify these at runtime. The entire look and feel of the OS lives in editable scripts.

**Why this architecture:**
1. Claude can modify the GUI while CLAOS is running — write a new Lua file, reload, done
2. Iterate on UI without recompiling the kernel
3. Users can customize everything by editing Lua scripts
4. Same pattern as game engines (Love2D, Roblox, WoW addons, Neovim)

```
Boot Flow:
  Kernel (C) → Init VESA framebuffer (C) → Mount ChaosFS (C)
    → Load /system/gui/init.lua
      → init.lua loads theme, creates desktop, sidebar, windows
      → enters main event loop:
          wait for input event (C dispatches to Lua)
          → Lua widget handles event
          → Lua calls C drawing primitives
          → C flips back buffer to screen
```

---

#### 8.1 — C Rendering Engine (`gui/render.c`, compiled into kernel)

This is the low-level graphics foundation. It does NOT know about widgets, windows, or themes — it only knows about pixels, rectangles, and text.

##### 8.1.1 — VESA Framebuffer Setup

- Use VBE (VESA BIOS Extensions) to switch to graphical mode
- **MUST be done in Stage 2 bootloader** (requires real mode BIOS interrupts)
- Target: 1024x768x32bpp (fallback: 800x600x32bpp)
- Get framebuffer physical address and pitch from VBE mode info block
- Map framebuffer into kernel virtual address space after paging is enabled

```c
// Stage 2 (real mode): Query and set VBE mode
// INT 10h, AX=4F01h — Get mode info
// INT 10h, AX=4F02h — Set mode (e.g., mode 0x118 = 1024x768x32)
```

##### 8.1.2 — Core Drawing Primitives (`gui/render.c`)

All exposed to Lua via `claos.gui.*` bindings.

```c
// Basic shapes
void fb_clear(uint32_t color);
void fb_pixel(int x, int y, uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness);
void fb_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void fb_line(int x1, int y1, int x2, int y2, uint32_t color);
void fb_circle(int cx, int cy, int radius, uint32_t color);

// Text rendering
void fb_char(int x, int y, char c, uint32_t fg, uint32_t bg);
int  fb_text(int x, int y, const char* str, uint32_t fg, uint32_t bg);
int  fb_text_width(const char* str);  // returns pixel width
int  fb_text_wrapped(int x, int y, int max_width, const char* str, uint32_t fg, uint32_t bg);

// Blitting and compositing
void fb_blit(int x, int y, int w, int h, const uint32_t* pixels);
void fb_copy_region(int dx, int dy, int sx, int sy, int w, int h);
void fb_alpha_blend(int x, int y, int w, int h, const uint32_t* pixels); // ARGB with alpha

// Buffer management
void fb_swap(void);           // Flip back buffer to screen (double buffering)
void fb_set_clip(int x, int y, int w, int h);  // Clipping rectangle
void fb_clear_clip(void);     // Remove clipping

// Screen info
int fb_width(void);           // Screen width in pixels
int fb_height(void);          // Screen height in pixels
```

##### 8.1.3 — Bitmap Font (`gui/font.c`)

- Embed a bitmap font directly in the kernel binary (no file loading needed)
- Two fonts:
  - **System font**: 8x16 fixed-width (for terminal, code, system info)
  - **UI font**: If possible, a proportional font for nicer UI text. Otherwise 8x16 for everything (still looks fine)
- Each glyph is a bitmap array: `uint8_t font_8x16[256][16]` — 256 ASCII chars, 16 rows of 8 pixels
- Font rendering reads each row as a bitmask and draws fg/bg pixels

##### 8.1.4 — Double Buffering

- Allocate a back buffer in RAM: `uint32_t* backbuf = kmalloc(width * height * 4)`
- All drawing goes to the back buffer
- `fb_swap()` copies back buffer to the VESA framebuffer (memcpy)
- Prevents flicker and tearing during redraws
- Only swap once per frame after all widgets have drawn

##### 8.1.5 — Mouse Cursor

- Draw cursor as a small sprite (16x16 arrow)
- Save the pixels under the cursor before drawing
- Restore saved pixels before drawing at new position
- OR: draw cursor as a final overlay after `fb_swap()` directly to the framebuffer (simpler, slight flicker on cursor only)

---

#### 8.2 — Input System (`gui/input.c`, compiled into kernel)

##### 8.2.1 — PS/2 Mouse Driver (`drivers/mouse.c`)

- Initialize PS/2 mouse via controller commands (IRQ12)
- Parse 3-byte mouse packets: button state, X delta, Y delta
- Accumulate deltas into absolute cursor position
- Clamp cursor to screen bounds
- Report: `mouse_x`, `mouse_y`, `mouse_buttons` (left/right/middle)

##### 8.2.2 — Event Queue

- Kernel maintains an event queue (ring buffer, 256 entries)
- Events types:
  ```c
  enum event_type {
      EVENT_KEY_DOWN,
      EVENT_KEY_UP,
      EVENT_MOUSE_MOVE,
      EVENT_MOUSE_DOWN,
      EVENT_MOUSE_UP,
      EVENT_MOUSE_SCROLL
  };
  struct input_event {
      uint8_t type;
      uint16_t key;        // scancode or ASCII
      int16_t mouse_x;     // absolute position
      int16_t mouse_y;
      uint8_t mouse_btn;   // which button
  };
  ```
- Lua polls events: `local event = claos.gui.poll_event()`
- Keyboard and mouse IRQ handlers push events into the queue

---

#### 8.3 — Lua GUI API Bindings (`lua/gui_lib.c`, compiled into kernel)

These C functions are registered as Lua bindings, bridging Lua scripts to the C renderer:

```lua
-- Drawing primitives (all write to back buffer)
claos.gui.clear(color)
claos.gui.rect(x, y, w, h, color)
claos.gui.rect_outline(x, y, w, h, color, thickness)
claos.gui.rounded_rect(x, y, w, h, radius, color)
claos.gui.line(x1, y1, x2, y2, color)
claos.gui.circle(cx, cy, radius, color)
claos.gui.text(x, y, str, fg_color, bg_color)  -- bg_color optional (transparent if nil)
claos.gui.text_width(str)                        -- returns pixel width
claos.gui.text_wrapped(x, y, max_width, str, fg_color, bg_color)

-- Buffer control
claos.gui.swap()              -- flip back buffer to screen
claos.gui.set_clip(x, y, w, h)
claos.gui.clear_clip()

-- Screen info
claos.gui.screen_width()
claos.gui.screen_height()

-- Input events
claos.gui.poll_event()        -- returns {type, key, x, y, button} or nil
claos.gui.mouse_x()
claos.gui.mouse_y()

-- Cursor
claos.gui.set_cursor(visible) -- show/hide mouse cursor
claos.gui.set_cursor_pos(x, y)

-- Color helper (returns uint32 0xAARRGGBB)
claos.gui.rgb(r, g, b)
claos.gui.rgba(r, g, b, a)

-- Timer
claos.gui.ticks()             -- ms since boot, for animations/debouncing
```

---

#### 8.4 — Lua UI Framework (stored on ChaosFS at `/system/gui/`)

This is where the actual UI lives — **entirely in Lua scripts on the filesystem.** Every visual element, every colour, every layout decision is in editable Lua files.

##### Filesystem Layout

```
/system/
├── gui/
│   ├── init.lua              -- Entry point: loads theme, creates desktop, starts event loop
│   ├── theme.lua             -- Active theme (colors, fonts, spacing, border widths)
│   ├── themes/
│   │   ├── light.lua         -- v2: Warm, friendly, light theme (DEFAULT)
│   │   └── dark.lua          -- v1: Hacker, terminal, dark theme
│   ├── widgets/
│   │   ├── widget.lua        -- Base widget class (position, size, visible, focused)
│   │   ├── window.lua        -- Window: title bar, content area, draggable, closeable
│   │   ├── button.lua        -- Clickable button with label
│   │   ├── textbox.lua       -- Text input field with cursor
│   │   ├── label.lua         -- Static text display
│   │   ├── scrollview.lua    -- Scrollable content area with scroll bar
│   │   ├── panel.lua         -- Background panel/card
│   │   ├── icon_button.lua   -- Sidebar icon button
│   │   ├── chip.lua          -- Quick action pill/chip
│   │   ├── progress_bar.lua  -- Progress/usage bar
│   │   └── chat_bubble.lua   -- Chat message bubble (user vs Claude styling)
│   ├── layout/
│   │   ├── desktop.lua       -- Desktop compositor: manages windows, z-order, focus
│   │   ├── topbar.lua        -- Top status bar (logo, net, mem, uptime)
│   │   ├── sidebar.lua       -- Left icon sidebar (app launcher)
│   │   └── right_panel.lua   -- Right info panel (Claude status, tasks, chat history)
│   └── apps/
│       ├── claude_chat.lua   -- Main Claude chat application (the star of the show)
│       ├── terminal.lua      -- Terminal emulator in a window
│       ├── sysmon.lua        -- System monitor with live stats
│       └── file_browser.lua  -- Browse ChaosFS files
```

##### init.lua — The GUI Entry Point

```lua
-- /system/gui/init.lua
-- This is loaded by the kernel after ChaosFS is mounted and Lua is initialized

-- Load the active theme
local theme = dofile("/system/gui/theme.lua")

-- Load widget library
local Window    = dofile("/system/gui/widgets/window.lua")
local Button    = dofile("/system/gui/widgets/button.lua")
local TextBox   = dofile("/system/gui/widgets/textbox.lua")
-- ... etc

-- Load layout components
local desktop    = dofile("/system/gui/layout/desktop.lua")
local topbar     = dofile("/system/gui/layout/topbar.lua")
local sidebar    = dofile("/system/gui/layout/sidebar.lua")
local right_panel = dofile("/system/gui/layout/right_panel.lua")

-- Load default app
local chat_app = dofile("/system/gui/apps/claude_chat.lua")

-- Initialize desktop
desktop.init(theme)
topbar.init(theme)
sidebar.init(theme)
right_panel.init(theme)
chat_app.init(theme)

-- Main event loop
while true do
    -- Poll all pending events
    local event = claos.gui.poll_event()
    while event do
        -- Dispatch to focused component
        desktop.handle_event(event)
        event = claos.gui.poll_event()
    end

    -- Redraw everything
    claos.gui.clear(theme.bg_primary)
    desktop.draw()
    topbar.draw()
    sidebar.draw()
    right_panel.draw()

    -- Flip to screen
    claos.gui.swap()

    -- Small sleep to avoid burning CPU (target ~30fps)
    claos.sleep(33)
end
```

##### theme.lua — Theme Toggle System

```lua
-- /system/gui/theme.lua
-- This file loads the active theme. Edit the line below to switch themes.

local active_theme = "light"  -- Change to "dark" for hacker mode

local theme = dofile("/system/gui/themes/" .. active_theme .. ".lua")
return theme
```

##### themes/light.lua — v2 Friendly Theme

```lua
-- /system/gui/themes/light.lua
-- Warm, friendly, human-centered design
return {
    name = "CLAOS Light",

    -- Backgrounds
    bg_primary     = claos.gui.rgb(244, 241, 236),  -- #f4f1ec warm cream
    bg_card        = claos.gui.rgb(255, 255, 255),  -- #ffffff
    bg_input       = claos.gui.rgb(244, 241, 236),  -- #f4f1ec
    bg_hover       = claos.gui.rgb(238, 237, 254),  -- #EEEDFE light purple
    bg_claude_msg  = claos.gui.rgb(249, 248, 246),  -- #f9f8f6
    bg_user_msg    = claos.gui.rgb(127, 119, 221),  -- #7F77DD purple

    -- Text
    text_primary   = claos.gui.rgb(44, 44, 42),     -- #2C2C2A
    text_secondary = claos.gui.rgb(136, 135, 128),   -- #888780
    text_muted     = claos.gui.rgb(180, 178, 169),   -- #B4B2A9
    text_on_accent = claos.gui.rgb(255, 255, 255),   -- white on purple
    text_claude    = claos.gui.rgb(127, 119, 221),   -- #7F77DD for Claude label

    -- Accent colors
    accent         = claos.gui.rgb(127, 119, 221),   -- #7F77DD Claude purple
    accent_light   = claos.gui.rgb(206, 203, 246),   -- #CECBF6
    accent_bg      = claos.gui.rgb(238, 237, 254),   -- #EEEDFE
    status_ok      = claos.gui.rgb(99, 153, 34),     -- #639922 green
    status_error   = claos.gui.rgb(226, 75, 74),     -- #E24B4A red
    status_warn    = claos.gui.rgb(239, 159, 39),     -- #EF9F27 amber

    -- Borders
    border         = claos.gui.rgba(0, 0, 0, 15),    -- very subtle
    border_hover   = claos.gui.rgba(0, 0, 0, 30),

    -- Dimensions
    topbar_height  = 36,
    sidebar_width  = 56,
    right_panel_width = 200,
    border_radius  = 10,
    window_radius  = 12,
    button_radius  = 8,
    chip_radius    = 8,
    padding        = 12,
    padding_lg     = 20,

    -- Fonts
    font_size      = 14,  -- will map to which bitmap font variant to use
    font_small     = 11,
    font_label     = 10,
}
```

##### themes/dark.lua — v1 Hacker Theme

```lua
-- /system/gui/themes/dark.lua
-- Deep, terminal-native, hacker aesthetic
return {
    name = "CLAOS Dark",

    -- Backgrounds
    bg_primary     = claos.gui.rgb(10, 10, 18),      -- #0a0a12 deep navy
    bg_card        = claos.gui.rgb(15, 15, 28),      -- #0f0f1c
    bg_input       = claos.gui.rgb(20, 20, 35),      -- #141423
    bg_hover       = claos.gui.rgba(127, 119, 221, 20),
    bg_claude_msg  = claos.gui.rgba(127, 119, 221, 15),
    bg_user_msg    = claos.gui.rgb(127, 119, 221),

    -- Text
    text_primary   = claos.gui.rgb(206, 203, 246),   -- #CECBF6 light purple
    text_secondary = claos.gui.rgba(175, 169, 236, 128), -- muted purple
    text_muted     = claos.gui.rgba(175, 169, 236, 100),
    text_on_accent = claos.gui.rgb(255, 255, 255),
    text_claude    = claos.gui.rgb(175, 169, 236),   -- #AFA9EC

    -- Accent colors
    accent         = claos.gui.rgb(127, 119, 221),   -- #7F77DD
    accent_light   = claos.gui.rgb(175, 169, 236),   -- #AFA9EC
    accent_bg      = claos.gui.rgba(127, 119, 221, 20),
    status_ok      = claos.gui.rgb(93, 202, 165),    -- #5DCAA5 teal
    status_error   = claos.gui.rgb(226, 75, 74),
    status_warn    = claos.gui.rgb(239, 159, 39),

    -- Borders
    border         = claos.gui.rgba(127, 119, 221, 38),  -- purple tinted
    border_hover   = claos.gui.rgba(127, 119, 221, 76),

    -- Dimensions (same layout, different skin)
    topbar_height  = 28,     -- slightly more compact
    sidebar_width  = 56,
    right_panel_width = 200,
    border_radius  = 8,
    window_radius  = 8,
    button_radius  = 6,
    chip_radius    = 4,
    padding        = 10,
    padding_lg     = 16,

    font_size      = 14,
    font_small     = 11,
    font_label     = 10,
}
```

##### Example Widget: chat_bubble.lua

```lua
-- /system/gui/widgets/chat_bubble.lua
-- Renders a single chat message bubble

local ChatBubble = {}

function ChatBubble.new(message, is_user, theme)
    return {
        message = message,
        is_user = is_user,
        theme = theme,
        height = 0,  -- calculated during draw
    }
end

function ChatBubble.draw(self, x, y, max_width)
    local t = self.theme
    local bubble_width = math.floor(max_width * 0.85)
    local text_padding = 12

    if self.is_user then
        -- Right-aligned purple bubble
        local bx = x + max_width - bubble_width
        -- Measure text height first
        local text_h = 20  -- approximate, based on line count
        claos.gui.rounded_rect(bx, y, bubble_width, text_h + text_padding * 2,
                               t.button_radius, t.bg_user_msg)
        claos.gui.text_wrapped(bx + text_padding, y + text_padding,
                               bubble_width - text_padding * 2,
                               self.message, t.text_on_accent, nil)
        self.height = text_h + text_padding * 2
    else
        -- Left-aligned Claude bubble
        -- Draw "Claude" label
        claos.gui.text(x, y, "Claude", t.text_claude, nil)
        local label_h = 18
        claos.gui.rounded_rect(x, y + label_h, bubble_width,
                               20 + text_padding * 2,  -- approximate
                               t.button_radius, t.bg_claude_msg)
        claos.gui.text_wrapped(x + text_padding, y + label_h + text_padding,
                               bubble_width - text_padding * 2,
                               self.message, t.text_primary, nil)
        self.height = label_h + 20 + text_padding * 2
    end

    return self.height
end

return ChatBubble
```

##### Example App: claude_chat.lua

```lua
-- /system/gui/apps/claude_chat.lua
-- The main Claude chat application

local ChatBubble = dofile("/system/gui/widgets/chat_bubble.lua")

local chat = {
    messages = {},
    input_text = "",
    scroll_offset = 0,
    theme = nil,
    window = nil,
}

function chat.init(theme)
    chat.theme = theme
    -- Add welcome message
    table.insert(chat.messages, ChatBubble.new(
        "Welcome to CLAOS! I'm running natively inside your operating system. " ..
        "Connected directly to Anthropic's API over HTTPS. What would you like to do?",
        false, theme
    ))
end

function chat.send_message()
    if #chat.input_text == 0 then return end

    -- Add user message
    table.insert(chat.messages, ChatBubble.new(chat.input_text, true, chat.theme))

    -- Call Claude
    local prompt = chat.input_text
    chat.input_text = ""
    local response = claos.ask(prompt)

    -- Add Claude response
    table.insert(chat.messages, ChatBubble.new(response, false, chat.theme))
end

function chat.handle_event(event)
    if event.type == "key_down" then
        if event.key == "enter" then
            chat.send_message()
        elseif event.key == "backspace" then
            chat.input_text = string.sub(chat.input_text, 1, -2)
        else
            chat.input_text = chat.input_text .. event.key
        end
    end
end

function chat.draw(x, y, w, h)
    local t = chat.theme

    -- Draw header
    claos.gui.text(x + t.padding, y + 10, "Claude", t.text_primary, nil)
    claos.gui.text(x + t.padding + 60, y + 12,
                   "Connected via HTTPS", t.status_ok, nil)

    -- Draw messages area
    local msg_y = y + 50 - chat.scroll_offset
    for _, bubble in ipairs(chat.messages) do
        local h = ChatBubble.draw(bubble, x + t.padding, msg_y, w - t.padding * 2)
        msg_y = msg_y + h + 12
    end

    -- Draw input box at bottom
    local input_y = y + h - 56
    claos.gui.rounded_rect(x + t.padding, input_y, w - t.padding * 2 - 48, 40,
                           t.border_radius, t.bg_input)
    claos.gui.text(x + t.padding + 16, input_y + 12,
                   #chat.input_text > 0 and chat.input_text or "Ask Claude anything...",
                   #chat.input_text > 0 and t.text_primary or t.text_muted, nil)

    -- Send button
    claos.gui.rounded_rect(x + w - t.padding - 40, input_y, 40, 40,
                           t.border_radius, t.accent)
    claos.gui.text(x + w - t.padding - 28, input_y + 12, ">", t.text_on_accent, nil)

    -- Quick action chips
    local chip_y = input_y + 46
    local chips = {"Debug a crash", "Write a driver", "Run Lua script", "System health"}
    local chip_x = x + t.padding
    for _, label in ipairs(chips) do
        local cw = claos.gui.text_width(label) + 20
        claos.gui.rounded_rect(chip_x, chip_y, cw, 24, t.chip_radius, t.accent_bg)
        claos.gui.text(chip_x + 10, chip_y + 6, label, t.accent, nil)
        chip_x = chip_x + cw + 6
    end
end

return chat
```

---

#### 8.5 — Runtime Theme Switching

The user can switch themes without rebooting:

```
claos> claude Can you switch to dark mode?
```

Claude modifies `/system/gui/theme.lua`, changing the active theme string from `"light"` to `"dark"`. The GUI event loop detects the file change (or gets a reload signal) and reloads the theme. The entire desktop re-renders with the new colours. No recompilation, no reboot.

Alternatively, add a shell command:
```
claos> theme dark
claos> theme light
```

Or in the GUI settings panel, a toggle button that writes to `theme.lua`.

---

#### 8.6 — Claude GUI Self-Modification (The Killer Feature)

Because the entire UI is Lua on ChaosFS, Claude can modify the OS's own GUI:

```
claos> claude Add a CPU usage graph to the system monitor

[CLAOS → Claude] Sending...
[Claude] I'll add a CPU usage graph. Writing to /system/gui/apps/sysmon.lua...
         Done! The system monitor now shows a live CPU usage graph.
         Reload the GUI to see the changes.
```

Implementation:
- Claude generates Lua code via the API
- The shell (or a Lua helper) writes it to ChaosFS via `claos.write()`
- The GUI reloads the affected module via `dofile()`
- The change is persistent — survives reboot

This is the ultimate demo: an AI modifying its own operating system's interface from inside that operating system.

---

#### 8.7 — Build Process for GUI Files

During the disk image build, pre-populate ChaosFS with the GUI scripts:

```bash
# In Makefile or build script:
python tools/mkchaosfs.py claos.img --add /system/gui/init.lua gui/lua/init.lua
python tools/mkchaosfs.py claos.img --add /system/gui/theme.lua gui/lua/theme.lua
python tools/mkchaosfs.py claos.img --add /system/gui/themes/light.lua gui/lua/themes/light.lua
python tools/mkchaosfs.py claos.img --add /system/gui/themes/dark.lua gui/lua/themes/dark.lua
# ... etc for all widget and app files
```

Store the Lua GUI source in the repo at `gui/lua/` and copy to ChaosFS during build.

---

#### 8.8 — Milestones

**Milestone 1 — Framebuffer works:**
Switch to VESA mode, fill screen with a colour, draw some rectangles and text. Confirm double buffering works.

**Milestone 2 — Mouse works:**
See a cursor on screen. Move it. Click things.

**Milestone 3 — Lua draws to screen:**
Run a Lua script that draws shapes and text via `claos.gui.*` bindings.

**Milestone 4 — Windowed desktop:**
`init.lua` loads and renders the desktop — top bar, sidebar, a window. Window is draggable.

**Milestone 5 — Theme toggle:**
Switch between light and dark theme. Both render correctly.

**Milestone 6 — Claude Chat in GUI:**
Click the chat icon, type a message, see Claude's response in a chat bubble. This is the moment.

**Milestone 7 — Claude modifies its own GUI:**
Ask Claude to change something in the UI. It writes Lua to ChaosFS. The change appears. Internet immortality achieved.

---

## Project Structure

```
claos/
├── boot/
│   ├── stage1.asm              # MBR bootloader
│   └── stage2.asm              # Protected mode + VBE mode set
├── kernel/
│   ├── main.c                  # Kernel entry point
│   ├── gdt.c / idt.c           # Descriptor tables
│   ├── isr.c / irq.c           # Interrupt handlers
│   ├── pmm.c / vmm.c           # Memory management
│   ├── heap.c                  # kmalloc / kfree
│   ├── scheduler.c             # Task scheduler
│   ├── panic.c                 # Panic handler → Claude
│   └── entropy.c               # Entropy pool for TLS
├── drivers/
│   ├── vga.c                   # VGA text mode (fallback / boot)
│   ├── keyboard.c              # PS/2 keyboard
│   ├── mouse.c                 # PS/2 mouse
│   ├── timer.c                 # PIT timer
│   ├── pci.c                   # PCI bus
│   ├── e1000.c                 # NIC driver
│   ├── ata.c                   # ATA/IDE block device
│   └── framebuffer.c           # VESA framebuffer driver
├── fs/
│   ├── chaosfs.c               # ChaosFS driver
│   └── vfs.c                   # Virtual filesystem layer
├── net/
│   ├── ethernet.c / arp.c      # L2
│   ├── ipv4.c / udp.c / tcp.c  # L3-L4
│   ├── dns.c                   # DNS resolver
│   ├── tls.c / tls_client.c    # TLS (BearSSL)
│   ├── ca_certs.c              # Root CA certificates
│   └── https.c                 # HTTPS client
├── lib/
│   ├── bearssl/                # BearSSL (ported)
│   └── lua/                    # Lua 5.5 (ported)
├── gui/
│   ├── render.c                # C rendering engine (primitives)
│   ├── font.c                  # Bitmap font data + renderer
│   ├── font_data.h             # Embedded font bitmaps
│   ├── input.c                 # Event queue + input dispatch
│   ├── gui_lua_bindings.c      # claos.gui.* Lua API
│   └── lua/                    # Lua GUI source (copied to ChaosFS during build)
│       ├── init.lua
│       ├── theme.lua
│       ├── themes/
│       │   ├── light.lua
│       │   └── dark.lua
│       ├── widgets/
│       │   ├── window.lua
│       │   ├── button.lua
│       │   ├── textbox.lua
│       │   ├── label.lua
│       │   ├── scrollview.lua
│       │   ├── panel.lua
│       │   ├── icon_button.lua
│       │   ├── chip.lua
│       │   ├── progress_bar.lua
│       │   └── chat_bubble.lua
│       ├── layout/
│       │   ├── desktop.lua
│       │   ├── topbar.lua
│       │   ├── sidebar.lua
│       │   └── right_panel.lua
│       └── apps/
│           ├── claude_chat.lua
│           ├── terminal.lua
│           ├── sysmon.lua
│           └── file_browser.lua
├── claude/
│   ├── claude.c                # Claude API protocol
│   ├── json.c                  # JSON builder/parser
│   ├── config.h                # API key (DO NOT COMMIT)
│   └── panic_handler.c         # Panic → Claude
├── shell/
│   └── shell.c                 # Interactive ClaudeShell
├── scripts/                    # Pre-installed user Lua scripts
│   ├── welcome.lua
│   ├── sysmon.lua
│   ├── chat.lua
│   └── crash_test.lua
├── tools/
│   └── mkchaosfs.py            # Host tool: format + populate ChaosFS
├── include/                    # Shared headers
├── linker.ld                   # Kernel linker script
├── Makefile
├── .gitignore
└── README.md
```

---

## Technical Specifications

- **Architecture:** x86 (i686), 32-bit protected mode
- **Language:** C (kernel, drivers, renderer) + Lua 5.5 (GUI, scripts) + x86 ASM (boot)
- **Compiler:** `i686-elf-gcc` (`-ffreestanding -nostdlib`)
- **Target VM:** QEMU (`qemu-system-i386`) with `-device e1000`
- **Graphics:** VESA VBE framebuffer, 1024x768x32bpp, double-buffered
- **Filesystem:** ChaosFS (custom)
- **TLS:** BearSSL (ported), TLS 1.2
- **Scripting:** Lua 5.5 (ported), with CLAOS + GUI API bindings
- **GUI:** C rendering engine + Lua UI scripts, two themes (light + dark)
- **Network:** Static IP, native TCP/IP + TLS + HTTPS, direct to api.anthropic.com

---

## Implementation Order

1. ~~Phase 1 — Boot~~ ✅
2. ~~Phase 2 — Memory + scheduler~~ ✅
3. ~~Phase 3 — Network stack~~ ✅
4. ~~Phase 3.5 — TLS~~ ✅
5. ~~Phase 4 — Claude integration~~ ✅
6. ~~Phase 5 — Shell~~ ✅
7. ~~Phase 6 — ChaosFS~~ ✅
8. **Phase 7** — Lua 5.5 ← IN PROGRESS
9. **Phase 8** — GUI (C renderer → Lua bindings → widgets → desktop → chat → self-modification)

---

## Key Constraints

- **No existing OS code.** BearSSL and Lua 5.5 are the only external libs, compiled freestanding.
- **No relay.** CLAOS does HTTPS end-to-end.
- **GUI lives in Lua on ChaosFS.** The C kernel only provides rendering primitives.
- **Comments everywhere.** Educational project.
- **Test after every milestone.** Boot in QEMU and verify.

---

## Personality

- Boot: "Initializing consciousness... done."
- Filesystem: "Mounting consciousness storage... done."
- Lua: "Lua 5.5 awakened. The scripting layer stirs."
- GUI: "Rendering reality... welcome home."
- Panic: "KERNEL PANIC: I have made a terrible mistake. Calling Claude..."
- Theme switch: "Redecorating... done. How's the new look?"

---

## Success Criteria

1. Boot CLAOS in QEMU
2. See ASCII art banner → shell works → Claude responds over native HTTPS
3. `panic` → Claude diagnoses crash in real time
4. `ls /system/gui/` → see Lua GUI files on ChaosFS
5. Boot into graphical desktop with both light and dark themes
6. Chat with Claude in a GUI chat window with message bubbles
7. Ask Claude to modify the GUI → it writes Lua → UI changes live
8. Screenshot it. Post it. Achieve internet immortality.

Go build something legendary.
