-- CLAOS GUI — Desktop
local g = claos.gui

if not g.active() then
    if not g.activate() then
        print("ERROR: Cannot activate VESA mode")
        return
    end
end

local W, H = g.width(), g.height()

-- ── Theme (v2 Light) ──────────────────────────────────────
local T = {
    bg         = g.rgb(244, 241, 236),
    white      = g.rgb(255, 255, 255),
    panel      = g.rgb(249, 248, 246),
    accent     = g.rgb(127, 119, 221),
    accent_lt  = g.rgb(238, 237, 254),
    accent_dk  = g.rgb(83, 74, 183),
    txt        = g.rgb(44, 44, 42),
    txt2       = g.rgb(136, 135, 128),
    txt3       = g.rgb(180, 178, 169),
    txt4       = g.rgb(95, 94, 90),
    green      = g.rgb(99, 153, 34),
    border     = g.rgb(225, 225, 220),
    fw = 8, fh = 16,
    top_h = 36, side_w = 56, rpan_w = 200,
}

-- ── Topbar ────────────────────────────────────────────────
local function draw_topbar()
    g.rect(0, 0, W, T.top_h, T.white)
    g.hline(0, T.top_h - 1, W, T.border)

    -- Logo
    g.rounded_rect(8, 8, 20, 20, 6, T.accent)
    g.text(11, 10, "C", T.white, T.accent)
    g.text(34, 12, "CLAOS", T.txt, T.white)
    g.text(82, 12, "v0.8", T.txt3, T.white)

    -- Status (right side)
    local rx = W - 240

    -- Online dot
    g.circle_filled(rx, 18, 3, T.green)
    g.text(rx + 8, 12, "Online", T.txt2, T.white)

    -- Memory (live)
    local mf = claos.mem_free()
    local mt = claos.mem_total()
    local used = mt - (mf * 4 / 1024)
    local mem_str = string.format("%d / %d MB", math.floor(used), mt)
    g.text(rx + 72, 12, mem_str, T.txt2, T.white)

    -- Uptime (live)
    local up = claos.uptime()
    local m = math.floor(up / 60)
    local s = up % 60
    g.text(rx + 172, 12, string.format("%02d:%02d", m, s), T.txt2, T.white)
end

-- ── Sidebar ───────────────────────────────────────────────
local sidebar_items = {
    { "C", true },   -- Chat (active)
    { ">", false },  -- Terminal
    { "M", false },  -- Monitor
    { "F", false },  -- Files
}

local function draw_sidebar()
    local sy = T.top_h
    g.rect(0, sy, T.side_w, H - sy, T.panel)
    g.vline(T.side_w - 1, sy, H - sy, T.border)

    for i, item in ipairs(sidebar_items) do
        local ix, iy = 9, sy + 12 + (i - 1) * 42
        local bg = item[2] and T.accent or T.bg
        local fg = item[2] and T.white or T.txt2
        g.rounded_rect(ix, iy, 38, 38, 10, bg)
        g.text(ix + 15, iy + 11, item[1], fg, bg)
    end

    -- Settings at bottom
    g.rounded_rect(9, H - 50, 38, 38, 10, T.bg)
    g.text(21, H - 39, "S", T.txt2, T.bg)
end

-- ── Right Panel ───────────────────────────────────────────
local function draw_rpanel()
    local px = W - T.rpan_w
    local py = T.top_h
    g.rect(px, py, T.rpan_w, H - py, T.panel)
    g.vline(px, py, H - py, T.border)

    local mx = px + 12
    local y = py + 16

    -- Claude status
    g.text(mx, y, "CLAUDE", T.txt3, T.panel)
    y = y + 20
    g.rounded_rect(mx, y, T.rpan_w - 24, 40, 10, T.white)
    g.circle_filled(mx + 14, y + 20, 4, T.green)
    g.text(mx + 24, y + 8, "Active", T.txt, T.white)
    g.text(mx + 24, y + 24, "claude-4", T.txt3, T.white)
    y = y + 56

    -- Panic watcher
    g.text(mx, y, "PANIC WATCHER", T.txt3, T.panel)
    y = y + 20
    g.rounded_rect(mx, y, T.rpan_w - 24, 44, 10, T.white)
    g.circle_filled(mx + 14, y + 12, 4, T.green)
    g.text(mx + 24, y + 4, "Armed", T.txt, T.white)
    g.text(mx + 12, y + 24, "Watching Ring 0", T.txt3, T.white)
    y = y + 60

    -- Running tasks (live)
    g.text(mx, y, "RUNNING TASKS", T.txt3, T.panel)
    y = y + 20
    local tasks = { "kernel", "spinner", "uptime", "net_poll", "gui" }
    for _, name in ipairs(tasks) do
        g.rounded_rect(mx, y, T.rpan_w - 24, 24, 8, T.white)
        g.circle_filled(mx + 10, y + 12, 3, T.accent)
        g.text(mx + 20, y + 4, name, T.txt4, T.white)
        y = y + 28
    end

    -- Footer
    y = H - 50
    g.rounded_rect(mx, y, T.rpan_w - 24, 40, 10, T.accent_lt)
    g.text(mx + 28, y + 4, "CLAOS v0.8", T.accent_dk, T.accent_lt)
    g.text(mx + 6, y + 22, "\"I am the kernel now.\"", T.accent, T.accent_lt)
end

-- ── Chat Area ─────────────────────────────────────────────
local function draw_chat()
    local cx = T.side_w
    local cy = T.top_h
    local cw = W - T.side_w - T.rpan_w
    local ch = H - T.top_h

    g.rect(cx, cy, cw, ch, T.white)

    -- Header
    g.hline(cx, cy + 51, cw, T.border)
    g.circle_filled(cx + 28, cy + 26, 16, T.accent_lt)
    g.text(cx + 24, cy + 18, "C", T.accent, T.accent_lt)
    g.text(cx + 52, cy + 12, "Claude", T.txt, T.white)
    g.text(cx + 52, cy + 28, "Connected via HTTPS", T.green, T.white)
    g.rounded_rect(cx + cw - 80, cy + 14, 64, 24, 6, T.bg)
    g.text(cx + cw - 72, cy + 18, "New chat", T.txt2, T.bg)

    -- Messages
    local my = cy + 72

    -- Claude message
    g.circle_filled(cx + 22, my + 12, 12, T.accent_lt)
    g.text(cx + 18, my + 6, "C", T.accent, T.accent_lt)
    g.text(cx + 42, my - 4, "Claude", T.txt3, T.white)
    g.rounded_rect(cx + 42, my + 4, cw - 62, 64, 8, T.panel)
    g.text(cx + 54, my + 12, "Welcome to CLAOS! I'm running", T.txt, T.panel)
    g.text(cx + 54, my + 28, "natively inside your OS.", T.txt, T.panel)
    g.text(cx + 54, my + 44, "What would you like to do?", T.txt, T.panel)
    my = my + 84

    -- User message
    g.text(cx + cw - 42, my - 4, "You", T.txt3, T.white)
    local umsg = "Show me the system status"
    local uw = #umsg * T.fw + 24
    g.rounded_rect(cx + cw - uw - 20, my + 4, uw, 32, 8, T.accent)
    g.text(cx + cw - uw - 8, my + 12, umsg, T.white, T.accent)

    -- Input bar
    local iy = cy + ch - 58
    g.hline(cx, iy, cw, T.border)
    g.rounded_rect(cx + 16, iy + 10, cw - 72, 36, 12, T.bg)
    g.text(cx + 32, iy + 20, "Ask Claude anything...", T.txt3, T.bg)
    g.rounded_rect(cx + cw - 48, iy + 10, 36, 36, 12, T.accent)
    g.text(cx + cw - 38, iy + 20, ">", T.white, T.accent)
end

-- ── Mouse Cursor ──────────────────────────────────────────
local function draw_cursor(mx, my)
    -- 12px arrow cursor
    local c1 = T.txt   -- outline
    local c2 = T.white  -- fill
    -- Draw a simple filled arrow
    for dy = 0, 11 do
        local w = (dy < 8) and (dy + 1) or (12 - dy)
        if w > 0 then
            g.hline(mx, my + dy, 1, c1)           -- left edge
            if w > 2 then
                g.hline(mx + 1, my + dy, w - 2, c2) -- fill
            end
            if w > 1 then
                g.pixel(mx + w - 1, my + dy, c1)   -- right edge
            end
        end
    end
    -- Bottom point outline
    g.pixel(mx, my + 12, c1)
end

-- ── Main Loop ─────────────────────────────────────────────
local function draw_all()
    g.clear(T.bg)
    draw_topbar()
    draw_sidebar()
    draw_chat()
    draw_rpanel()
end

draw_all()
draw_cursor(g.mouse_x(), g.mouse_y())
g.swap()

local running = true
local last_mx, last_my = g.mouse_x(), g.mouse_y()
local frame = 0

while running do
    local dirty = false

    -- Process events
    while true do
        local ev = g.poll_event()
        if not ev then break end

        if ev.type == g.KEY_DOWN then
            if ev.key == 27 then
                running = false
                break
            end
        elseif ev.type == g.MOUSE_MOVE then
            last_mx, last_my = ev.x, ev.y
            dirty = true
        elseif ev.type == g.MOUSE_DOWN then
            dirty = true
        end
    end

    -- Refresh every ~60 frames for live uptime
    frame = frame + 1
    if frame % 60 == 0 then
        dirty = true
    end

    if dirty then
        draw_all()
        draw_cursor(last_mx, last_my)
        g.swap()
    end

    if not running then break end
    claos.sleep(16)  -- ~60fps target
end
