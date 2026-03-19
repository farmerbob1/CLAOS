-- CLAOS GUI — Desktop Entry Point
-- Loads theme, draws desktop, runs event loop

local g = claos.gui

-- Ensure VESA is active
if not g.active() then
    if not g.activate() then
        print("ERROR: Cannot activate VESA mode")
        return
    end
end

local W, H = g.width(), g.height()

-- Load theme
local theme_src = claos.read('/system/gui/theme.lua')
local theme = nil
if theme_src then
    local fn = load(theme_src, 'theme.lua')
    if fn then theme = fn() end
end
if not theme then
    -- Fallback minimal theme
    theme = {
        bg_primary = g.rgb(244, 241, 236),
        bg_white = g.rgb(255, 255, 255),
        bg_panel = g.rgb(249, 248, 246),
        accent = g.rgb(127, 119, 221),
        accent_light = g.rgb(238, 237, 254),
        accent_dark = g.rgb(83, 74, 183),
        text_primary = g.rgb(44, 44, 42),
        text_secondary = g.rgb(136, 135, 128),
        text_muted = g.rgb(180, 178, 169),
        text_dark = g.rgb(95, 94, 90),
        green = g.rgb(99, 153, 34),
        border_light = g.rgb(230, 230, 225),
        font_w = 8, font_h = 16,
        topbar_h = 36, sidebar_w = 56, rpanel_w = 200,
    }
end

local T = theme

-- ── Drawing Helpers ──

local function draw_topbar()
    -- Frosted glass top bar
    g.rect(0, 0, W, T.topbar_h, T.bg_white)
    g.hline(0, T.topbar_h - 1, W, T.border_light)

    -- CLAOS logo
    g.rounded_rect(8, 8, 20, 20, 6, T.accent)
    g.text(11, 10, "C", T.bg_white, T.accent)
    g.text(34, 12, "CLAOS", T.text_primary, T.bg_white)
    g.text(82, 12, "v0.8", T.text_muted, T.bg_white)

    -- Right side status
    local rx = W - 220
    -- Online indicator
    g.circle_filled(rx, 18, 3, T.green)
    g.text(rx + 8, 12, "Online", T.text_secondary, T.bg_white)

    -- Memory
    local mem_free = claos.mem_free and claos.mem_free() or 0
    local mem_total = claos.mem_total and claos.mem_total() or 128
    local mem_used = mem_total - (mem_free * 4 / 1024)
    g.text(rx + 70, 12, string.format("%d / %d MB", math.floor(mem_used), mem_total),
        T.text_secondary, T.bg_white)

    -- Uptime
    local up = claos.uptime and claos.uptime() or 0
    local mins = math.floor(up / 60)
    local secs = up % 60
    g.text(rx + 160, 12, string.format("%02d:%02d", mins, secs),
        T.text_secondary, T.bg_white)
end

local function draw_sidebar()
    local sx = 0
    local sy = T.topbar_h
    local sw = T.sidebar_w
    local sh = H - T.topbar_h

    g.rect(sx, sy, sw, sh, T.bg_panel)
    g.vline(sw - 1, sy, sh, T.border_light)

    -- Icon buttons (simplified as colored squares with letters)
    local icons = {
        { label = "C", color = T.accent,     tip = "Chat" },
        { label = ">", color = T.bg_primary, tip = "Terminal" },
        { label = "M", color = T.bg_primary, tip = "Monitor" },
        { label = "F", color = T.bg_primary, tip = "Files" },
    }

    for i, icon in ipairs(icons) do
        local ix = 9
        local iy = sy + 12 + (i - 1) * 42
        g.rounded_rect(ix, iy, 38, 38, 10, icon.color)
        local text_color = (icon.color == T.accent) and T.bg_white or T.text_secondary
        g.text(ix + 15, iy + 11, icon.label, text_color, icon.color)
    end

    -- Settings at bottom
    g.rounded_rect(9, H - 50, 38, 38, 10, T.bg_primary)
    g.text(24, H - 39, "S", T.text_secondary, T.bg_primary)
end

local function draw_right_panel()
    local px = W - T.rpanel_w
    local py = T.topbar_h
    local pw = T.rpanel_w
    local ph = H - T.topbar_h

    g.rect(px, py, pw, ph, T.bg_panel)
    g.vline(px, py, ph, T.border_light)

    local y = py + 16
    local mx = px + 12

    -- Claude status
    g.text(mx, y, "CLAUDE", T.text_muted, T.bg_panel)
    y = y + 20
    g.rounded_rect(mx, y, pw - 24, 40, 10, T.bg_white)
    g.circle_filled(mx + 14, y + 20, 4, T.green)
    g.text(mx + 24, y + 8, "Active", T.text_primary, T.bg_white)
    g.text(mx + 24, y + 24, "claude-4", T.text_muted, T.bg_white)
    y = y + 56

    -- Panic watcher
    g.text(mx, y, "PANIC WATCHER", T.text_muted, T.bg_panel)
    y = y + 20
    g.rounded_rect(mx, y, pw - 24, 52, 10, T.bg_white)
    g.circle_filled(mx + 14, y + 12, 4, T.green)
    g.text(mx + 24, y + 4, "Armed", T.text_primary, T.bg_white)
    g.text(mx + 12, y + 24, "Watching Ring 0", T.text_muted, T.bg_white)
    g.text(mx + 12, y + 38, "exceptions", T.text_muted, T.bg_white)
    y = y + 68

    -- Running tasks
    g.text(mx, y, "RUNNING TASKS", T.text_muted, T.bg_panel)
    y = y + 20
    local tasks = { "kernel", "gui", "net_poll" }
    for _, name in ipairs(tasks) do
        g.rounded_rect(mx, y, pw - 24, 24, 8, T.bg_white)
        g.circle_filled(mx + 10, y + 12, 3, T.accent)
        g.text(mx + 20, y + 4, name, T.text_dark, T.bg_white)
        y = y + 28
    end

    -- CLAOS footer
    y = H - 50
    g.rounded_rect(mx, y, pw - 24, 40, 10, T.accent_light)
    g.text(mx + (pw - 24) / 2 - 32, y + 4, "CLAOS v0.8", T.accent_dark, T.accent_light)
    g.text(mx + (pw - 24) / 2 - 56, y + 20, "\"I am the kernel now.\"", T.accent, T.accent_light)
end

local function draw_chat_area()
    local cx = T.sidebar_w
    local cy = T.topbar_h
    local cw = W - T.sidebar_w - T.rpanel_w
    local ch = H - T.topbar_h

    -- White background
    g.rect(cx, cy, cw, ch, T.bg_white)

    -- Chat header
    g.rect(cx, cy, cw, 52, T.bg_white)
    g.hline(cx, cy + 51, cw, T.border_light)

    -- Claude avatar
    g.circle_filled(cx + 28, cy + 26, 16, T.accent_light)
    g.text(cx + 24, cy + 18, "C", T.accent, T.accent_light)
    g.text(cx + 52, cy + 12, "Claude", T.text_primary, T.bg_white)
    g.text(cx + 52, cy + 28, "Connected via HTTPS", T.green, T.bg_white)

    -- New chat button
    g.rounded_rect(cx + cw - 80, cy + 14, 64, 24, 6, T.bg_primary)
    g.text(cx + cw - 72, cy + 18, "New chat", T.text_secondary, T.bg_primary)

    -- Chat messages
    local my = cy + 72

    -- Claude welcome message
    g.circle_filled(cx + 22, my + 12, 12, T.accent_light)
    g.text(cx + 18, my + 6, "C", T.accent, T.accent_light)
    g.text(cx + 42, my - 4, "Claude", T.text_muted, T.bg_white)
    g.rounded_rect(cx + 42, my + 4, cw - 60, 64, 8, T.bg_panel)
    g.text(cx + 54, my + 12, "Welcome to CLAOS! I'm running", T.text_primary, T.bg_panel)
    g.text(cx + 54, my + 28, "natively inside your OS.", T.text_primary, T.bg_panel)
    g.text(cx + 54, my + 44, "What would you like to do?", T.text_primary, T.bg_panel)
    my = my + 84

    -- User message
    g.text(cx + cw - 42, my - 4, "You", T.text_muted, T.bg_white)
    g.rounded_rect(cx + cw - 280, my + 4, 260, 32, 8, T.accent)
    g.text(cx + cw - 268, my + 12, "Show me the system status", T.bg_white, T.accent)
    my = my + 52

    -- Claude response with status cards
    g.circle_filled(cx + 22, my + 12, 12, T.accent_light)
    g.text(cx + 18, my + 6, "C", T.accent, T.accent_light)
    g.text(cx + 42, my - 4, "Claude", T.text_muted, T.bg_white)
    g.rounded_rect(cx + 42, my + 4, cw - 60, 32, 8, T.bg_panel)
    g.text(cx + 54, my + 12, "Here's your system at a glance:", T.text_primary, T.bg_panel)

    -- Input area at bottom
    local iy = cy + ch - 70
    g.hline(cx, iy, cw, T.border_light)
    g.rect(cx, iy + 1, cw, 69, T.bg_white)
    g.rounded_rect(cx + 16, iy + 12, cw - 72, 40, 12, T.bg_primary)
    g.text(cx + 32, iy + 24, "Ask Claude anything...", T.text_muted, T.bg_primary)

    -- Send button
    g.rounded_rect(cx + cw - 48, iy + 12, 40, 40, 12, T.accent)
    g.text(cx + cw - 38, iy + 24, ">", T.bg_white, T.accent)

    -- Quick action chips
    local chips = { "Debug a crash", "Write a driver", "Run Lua script", "System health" }
    local chip_x = cx + 18
    local chip_y = iy + 56
    for _, label in ipairs(chips) do
        local chip_w = #label * T.font_w + 16
        g.rounded_rect(chip_x, chip_y, chip_w, 20, 8, T.accent_light)
        g.text(chip_x + 8, chip_y + 2, label, T.accent_dark, T.accent_light)
        chip_x = chip_x + chip_w + 6
    end
end

local function draw_mouse_cursor(mx, my)
    -- Simple arrow cursor (white with black outline)
    for dy = 0, 11 do
        local w = (dy < 8) and (dy + 1) or (12 - dy)
        g.hline(mx, my + dy, w, T.text_primary)
    end
    for dy = 1, 10 do
        local w = (dy < 7) and dy or (11 - dy)
        if w > 0 then
            g.hline(mx + 1, my + dy, w - 1, T.bg_white)
        end
    end
end

-- ── Main Desktop Render ──

local function draw_desktop()
    g.clear(T.bg_primary)
    draw_topbar()
    draw_sidebar()
    draw_chat_area()
    draw_right_panel()
end

-- ── Event Loop ──

draw_desktop()
g.swap()

local running = true
local last_mx, last_my = -1, -1
local frame = 0

while running do
    local dirty = false

    -- Process all pending events
    while true do
        local ev = g.poll_event()
        if not ev then break end

        if ev.type == g.KEY_DOWN then
            if ev.key == 27 then  -- ESC
                running = false
                break
            end
        elseif ev.type == g.MOUSE_MOVE then
            if ev.x ~= last_mx or ev.y ~= last_my then
                last_mx, last_my = ev.x, ev.y
                dirty = true
            end
        end
    end

    -- Periodic refresh for uptime counter (every ~30 frames)
    frame = frame + 1
    if frame % 30 == 0 then
        dirty = true
    end

    -- Redraw if needed
    if dirty then
        draw_desktop()
        draw_mouse_cursor(last_mx >= 0 and last_mx or g.mouse_x(),
                          last_my >= 0 and last_my or g.mouse_y())
        g.swap()
    end

    claos.sleep(33)  -- ~30 FPS
end
