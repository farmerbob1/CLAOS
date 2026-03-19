-- CLAOS Desktop Environment
-- Window manager, dock, desktop, app framework
local g = claos.gui
if not g.active() then
    if not g.activate() then print("ERROR: No VESA") return end
end
local W, H = g.width(), g.height()
local FW, FH = 8, 16

-- ═══════════════════════════════════════
-- Theme (dark space)
-- ═══════════════════════════════════════
local C = {
    bg    = g.rgb(12,12,22),
    bg2   = g.rgb(14,14,26),
    bg3   = g.rgb(10,12,20),
    bg4   = g.rgb(8,8,16),
    win   = g.rgb(22,22,34),
    title = g.rgb(28,28,42),
    title_u = g.rgb(24,24,36),
    pn    = g.rgb(30,30,45),
    ac    = g.rgb(127,119,221),
    al    = g.rgb(50,46,80),
    t1    = g.rgb(210,208,220),
    t2    = g.rgb(140,138,150),
    t3    = g.rgb(100,98,110),
    gn    = g.rgb(80,200,80),
    rd    = g.rgb(220,80,80),
    bd    = g.rgb(45,45,60),
    shadow = g.rgb(4,4,10),
    dock  = g.rgb(18,18,30),
    ib    = g.rgb(35,35,50),
    white = g.rgb(255,255,255),
    star1 = g.rgb(180,180,200),
    star2 = g.rgb(140,140,170),
    star3 = g.rgb(220,220,240),
    menu_bg = g.rgb(28,28,42),
    menu_hv = g.rgb(50,46,80),
}

-- ═══════════════════════════════════════
-- Constants
-- ═══════════════════════════════════════
local DOCK_W = 52
local TB = 28
local BTN_SZ = 16
local BTN_PAD = 6

-- ═══════════════════════════════════════
-- Helpers
-- ═══════════════════════════════════════
local function hit(bx,by,bw,bh,mx,my)
    return mx>=bx and mx<bx+bw and my>=by and my<by+bh
end

local function wrap(s, mc)
    local r = {}; local p = 1
    while p <= #s do
        if #s-p+1 <= mc then r[#r+1]=s:sub(p); break end
        local c = s:sub(p, p+mc-1); local sp
        for i=#c,1,-1 do if c:sub(i,i)==" " then sp=i; break end end
        if sp then r[#r+1]=c:sub(1,sp-1); p=p+sp
        else r[#r+1]=c; p=p+mc end
    end; return r
end

local function loadmod(path)
    local src = claos.read(path)
    if not src then error("loadmod: not found: "..path) end
    local fn, err = load(src, path)
    if not fn then error("loadmod: "..(err or "parse error")) end
    return fn()
end

-- Safe app callback — catches errors, stores on window for display
local function app_call(win, method, ...)
    local fn = win.app[method]
    if not fn then return end
    local ok, err = pcall(fn, win.state, ...)
    if not ok then
        win.error = tostring(err)
        -- Log to serial
        print("[GUI] App '" .. (win.app.name or "?") .. "' error in " .. method .. ": " .. win.error)
    end
end

-- Double-click
local dbl_time, dbl_id = 0, -1
local function is_dblclick(id)
    local now = claos.uptime()
    local dbl = (id == dbl_id and (now - dbl_time) < 1)
    dbl_time = now; dbl_id = id
    return dbl
end

-- ═══════════════════════════════════════
-- Icon drawing (pixel art with primitives)
-- ═══════════════════════════════════════
local function icon_chat(x, y, sz, fg)
    -- Speech bubble
    local bw = math.floor(sz*0.85)
    local bh = math.floor(sz*0.6)
    g.rounded_rect(x+math.floor((sz-bw)/2), y+2, bw, bh, math.floor(sz/6), fg)
    -- Tail triangle (3 pixels)
    local tx = x + math.floor(sz*0.25)
    local ty = y + 2 + bh - 2
    g.pixel(tx, ty+1, fg); g.pixel(tx-1, ty+2, fg); g.pixel(tx, ty+2, fg)
    g.pixel(tx+1, ty+1, fg)
end

local function icon_terminal(x, y, sz, fg)
    -- Monitor outline
    g.rounded_rect(x+2, y+2, sz-4, sz-8, 3, fg)
    g.rounded_rect(x+4, y+4, sz-8, sz-12, 2, g.rgb(16,16,24))
    -- Prompt "> _"
    g.text(x+6, y+math.floor(sz/2)-8, ">", g.rgb(80,220,80), g.rgb(16,16,24))
end

local function icon_monitor(x, y, sz, fg)
    -- Chart area
    g.rounded_rect(x+2, y+2, sz-4, sz-8, 3, fg)
    g.rounded_rect(x+4, y+4, sz-8, sz-12, 2, g.rgb(20,20,32))
    -- Bar chart lines
    local bx = x + 7
    local by = y + sz - 10
    g.vline(bx, by-8, 8, g.rgb(127,119,221))
    g.vline(bx+4, by-12, 12, g.rgb(80,200,80))
    g.vline(bx+8, by-6, 6, g.rgb(220,160,80))
    g.vline(bx+12, by-14, 14, g.rgb(80,160,240))
end

local function icon_folder(x, y, sz, fg)
    -- Folder tab
    g.rounded_rect(x+3, y+4, math.floor(sz*0.4), math.floor(sz*0.3), 2, fg)
    -- Folder body
    g.rounded_rect(x+2, y+math.floor(sz*0.3)+2, sz-4, math.floor(sz*0.55), 3, fg)
    -- Lighter inner
    g.rounded_rect(x+3, y+math.floor(sz*0.3)+4, sz-6, math.floor(sz*0.55)-4, 2,
        g.rgb(100,180,255))
end

local function icon_settings(x, y, sz, fg)
    -- Gear: circle with notches
    local cx, cy = x + math.floor(sz/2), y + math.floor(sz/2)
    local r = math.floor(sz/3)
    g.circle_filled(cx, cy, r, fg)
    g.circle_filled(cx, cy, r-3, g.rgb(60,60,80))
    -- Notches (top, right, bottom, left)
    g.rect(cx-2, cy-r-3, 4, 6, fg)
    g.rect(cx-2, cy+r-3, 4, 6, fg)
    g.rect(cx-r-3, cy-2, 6, 4, fg)
    g.rect(cx+r-3, cy-2, 6, 4, fg)
end

local function icon_power(x, y, sz, fg)
    local cx, cy = x + math.floor(sz/2), y + math.floor(sz/2)
    local r = math.floor(sz/3)
    g.circle(cx, cy, r, fg)
    g.vline(cx, cy-r-2, math.floor(r*0.8), fg)
    -- Break circle at top
    g.rect(cx-2, cy-r-1, 4, 4, C.dock)
end

local function icon_file(x, y, sz, fg)
    -- Document
    g.rounded_rect(x+4, y+2, sz-8, sz-4, 2, fg)
    g.rounded_rect(x+5, y+3, sz-10, sz-6, 2, g.rgb(240,240,240))
    -- Fold corner
    g.rect(x+sz-10, y+2, 6, 6, fg)
    -- Lines
    g.hline(x+8, y+10, sz-18, g.rgb(180,180,180))
    g.hline(x+8, y+14, sz-20, g.rgb(180,180,180))
    g.hline(x+8, y+18, sz-16, g.rgb(180,180,180))
end

-- ═══════════════════════════════════════
-- App registry
-- ═══════════════════════════════════════
local apps = {}
local dock_items = {}

local function register_app(path)
    local ok, app = pcall(loadmod, path)
    if not ok then
        print("App load error: " .. tostring(app))
        return
    end
    apps[app.name] = app
    if not app.hide_dock then
        dock_items[#dock_items+1] = app
    end
end

-- Load shared widgets (global for all apps)
WIDGETS = loadmod("/system/gui/widgets.lua")

register_app("/system/gui/apps/chat.lua")
register_app("/system/gui/apps/term.lua")
register_app("/system/gui/apps/monitor.lua")
register_app("/system/gui/apps/files.lua")
register_app("/system/gui/apps/notepad.lua")

-- ═══════════════════════════════════════
-- Window Manager
-- ═══════════════════════════════════════
local wins = {}
local next_id = 1
local drag = nil     -- {win, ox, oy} for title bar drag
local resize = nil   -- {win, edge, ox, oy, ow, oh} for resize
local RESIZE_GRIP = 8  -- pixels from edge to detect resize
local mouse_x, mouse_y = W/2, H/2
local dock_hover = -1

-- Context menu state
local ctx_menu = nil  -- {x, y, items={{label,action},...}, hover=-1}

local function get_focused()
    for i = #wins, 1, -1 do
        if wins[i].vis then return wins[i] end
    end
    return nil
end

local function has_open(app_name)
    for _, w in ipairs(wins) do
        if w.app.name == app_name and w.vis then return true end
    end
    return false
end

local function has_minimized(app_name)
    for _, w in ipairs(wins) do
        if w.app.name == app_name and not w.vis then return true end
    end
    return false
end

local function find_window(app_name)
    for _, w in ipairs(wins) do
        if w.app.name == app_name then return w end
    end
    return nil
end

local function wm_focus(win)
    for i = 1, #wins do
        if wins[i] == win then
            table.remove(wins, i)
            wins[#wins+1] = win
            break
        end
    end
end

local function wm_close(win)
    app_call(win, "on_close")
    for i = 1, #wins do
        if wins[i] == win then table.remove(wins, i); break end
    end
    if drag and drag.win == win then drag = nil end
end

local function wm_minimize(win)
    win.vis = false
    if drag and drag.win == win then drag = nil end
end

local function wm_open(app_name)
    local app = apps[app_name]
    if not app then return end

    -- For non-notepad apps, focus existing window
    if app_name ~= "notepad" then
        local existing = find_window(app_name)
        if existing then
            existing.vis = true
            wm_focus(existing)
            return existing
        end
    end

    local n = #wins
    local cx = DOCK_W + 60 + (n % 6) * 30
    local cy = 40 + (n % 6) * 30

    local win = {
        id = next_id,
        title = app.title or app.name,
        x = cx, y = cy,
        w = app.w or 480, h = app.h or 360,
        min_w = app.min_w or 200, min_h = app.min_h or 150,
        vis = true,
        app = app,
        state = {},
    }
    next_id = next_id + 1
    wins[#wins+1] = win
    app_call(win, "on_open")
    return win
end

local function wm_open_with(app_name, params)
    local app = apps[app_name]
    if not app then return end

    local n = #wins
    local cx = DOCK_W + 80 + (n % 6) * 30
    local cy = 50 + (n % 6) * 30

    local win = {
        id = next_id,
        title = (params and params.name) or app.title or app.name,
        x = cx, y = cy,
        w = app.w or 480, h = app.h or 360,
        min_w = app.min_w or 200, min_h = app.min_h or 150,
        vis = true,
        app = app,
        state = {params = params},
    }
    next_id = next_id + 1
    wins[#wins+1] = win
    app_call(win, "on_open")
    return win
end

-- Global WM interface for apps
WM = {
    open = wm_open,
    open_with = wm_open_with,
}

-- ═══════════════════════════════════════
-- Context Menu
-- ═══════════════════════════════════════
local function show_ctx_menu(x, y, items)
    -- Clamp to screen
    local mw = 0
    for _, it in ipairs(items) do
        if #it.label * FW + 24 > mw then mw = #it.label * FW + 24 end
    end
    local mh = #items * 24 + 8
    if x + mw > W then x = W - mw end
    if y + mh > H then y = H - mh end
    ctx_menu = {x=x, y=y, w=mw, h=mh, items=items, hover=-1}
end

local function draw_ctx_menu()
    if not ctx_menu then return end
    local m = ctx_menu
    -- Shadow
    g.rect(m.x+2, m.y+2, m.w, m.h, C.shadow)
    -- Background
    g.rounded_rect(m.x, m.y, m.w, m.h, 6, C.menu_bg)
    g.rect_outline(m.x, m.y, m.w, m.h, C.bd)
    -- Items
    for i, it in ipairs(m.items) do
        local iy = m.y + 4 + (i-1) * 24
        if i == m.hover then
            g.rounded_rect(m.x+3, iy, m.w-6, 22, 4, C.menu_hv)
        end
        local fg = it.disabled and C.t3 or C.t1
        g.text(m.x+12, iy+3, it.label, fg, i == m.hover and C.menu_hv or C.menu_bg)
    end
end

local function ctx_menu_hittest(mx, my)
    if not ctx_menu then return -1 end
    local m = ctx_menu
    if not hit(m.x, m.y, m.w, m.h, mx, my) then return -1 end
    for i = 1, #m.items do
        local iy = m.y + 4 + (i-1) * 24
        if hit(m.x, iy, m.w, 22, mx, my) then return i end
    end
    return -1
end

-- ═══════════════════════════════════════
-- Background (deterministic stars)
-- ═══════════════════════════════════════
local stars = {}
do
    local seed = 42
    local sc = {C.star1, C.star2, C.star3}
    for i = 1, 150 do
        seed = (seed * 1103515245 + 12345) % 2147483648
        local sx = seed % W
        seed = (seed * 1103515245 + 12345) % 2147483648
        local sy = seed % H
        stars[#stars+1] = {sx, sy, sc[(i % 3) + 1], i % 7 == 0}
    end
end

local function draw_bg()
    local bh = math.floor(H / 4)
    g.rect(0, 0, W, bh, C.bg)
    g.rect(0, bh, W, bh, C.bg2)
    g.rect(0, bh*2, W, bh, C.bg3)
    g.rect(0, bh*3, W, H-bh*3, C.bg4)
    for _, s in ipairs(stars) do
        g.pixel(s[1], s[2], s[3])
        if s[4] then
            g.pixel(s[1]+1, s[2], s[3])
            g.pixel(s[1], s[2]+1, s[3])
        end
    end
end

-- ═══════════════════════════════════════
-- Desktop icons (right side)
-- ═══════════════════════════════════════
local desk_items = {}
local function refresh_desktop()
    desk_items = {}
    local items = claos.ls("/")
    if not items then return end
    for _, f in ipairs(items) do
        local nm = f.name
        if nm:sub(1,1) == "/" then nm = nm:sub(2) end
        if not nm:find("/") and nm ~= "system" then
            desk_items[#desk_items+1] = {name=nm, full=f.name, is_dir=f.is_dir, size=f.size}
        end
    end
end
refresh_desktop()

local desk_sel = -1

local function draw_desktop_icons()
    local ix = W - 90
    local iy = 80
    for i, item in ipairs(desk_items) do
        local sel = (i == desk_sel)
        local bg = sel and C.al or C.bg4
        g.rounded_rect(ix-4, iy, 72, 64, 8, bg)

        -- Draw icon
        if item.is_dir then
            icon_folder(ix+12, iy+4, 32, g.rgb(80,160,240))
        else
            icon_file(ix+12, iy+4, 32, g.rgb(160,160,180))
        end

        -- Name (truncate)
        local nm = item.name
        if #nm > 8 then nm = nm:sub(1,7).."." end
        local tw = #nm * FW
        g.text(ix + 32 - math.floor(tw/2), iy+40, nm, C.t1, bg)

        iy = iy + 74
        if iy > H - 80 then break end
    end
end

-- ═══════════════════════════════════════
-- Dock (left side)
-- ═══════════════════════════════════════
-- Icon draw functions for dock apps
local dock_icon_fn = {
    chat = icon_chat,
    term = icon_terminal,
    monitor = icon_monitor,
    files = icon_folder,
}

local function draw_dock()
    local n = #dock_items
    local dock_h = n * 48 + 110
    local dy = math.floor((H - dock_h) / 2)
    if dy < 20 then dy = 20 end

    -- Dock background
    g.rounded_rect(4, dy, 44, dock_h, 14, C.dock)
    g.rect_outline(4, dy, 44, dock_h, C.bd)

    -- App icons
    for i, app in ipairs(dock_items) do
        local iy = dy + 10 + (i-1) * 48
        local hv = (dock_hover == i)
        local is_open = has_open(app.name)
        local is_min = has_minimized(app.name)

        -- Hover highlight
        if hv then
            g.rounded_rect(8, iy, 36, 36, 8, g.rgb(50,50,75))
        end

        -- Icon background
        local ibg = is_open and C.ac or C.ib
        g.rounded_rect(8, iy, 36, 36, 8, ibg)

        -- Draw pixel icon
        local fn = dock_icon_fn[app.name]
        if fn then
            fn(10, iy+2, 32, is_open and C.white or C.t2)
        else
            g.text(20, iy+10, app.icon, is_open and C.white or C.t2, ibg)
        end

        -- Running indicator dot (left edge)
        if is_open or is_min then
            g.circle_filled(2, iy+18, 2, C.white)
        end
    end

    -- Separator
    local bot = dy + n * 48 + 18
    g.hline(12, bot, 28, C.bd)

    -- Settings icon
    icon_settings(10, bot+8, 32, C.t3)

    -- Power icon
    icon_power(10, bot+48, 32, C.t3)
end

local function get_dock_hover(mx, my)
    if mx > DOCK_W then return -1 end
    local n = #dock_items
    local dock_h = n * 48 + 110
    local dy = math.floor((H - dock_h) / 2)
    if dy < 20 then dy = 20 end
    for i = 1, n do
        local iy = dy + 10 + (i-1) * 48
        if hit(8, iy, 36, 36, mx, my) then return i end
    end
    local bot = dy + n * 48 + 18
    if hit(10, bot+48, 32, 32, mx, my) then return -2 end  -- power
    return -1
end

-- ═══════════════════════════════════════
-- Status widget (top right)
-- ═══════════════════════════════════════
local function draw_status()
    local sw, sh = 190, 58
    local sx, sy = W - sw - 8, 8

    g.rounded_rect(sx, sy, sw, sh, 10, C.dock)
    g.rect_outline(sx, sy, sw, sh, C.bd)

    -- Online + uptime
    g.circle_filled(sx+14, sy+12, 4, C.gn)
    g.text(sx+24, sy+6, "Online", C.t2, C.dock)
    local up = claos.uptime()
    g.text(sx+100, sy+6,
        string.format("%02d:%02d", math.floor(up/60), up%60), C.t2, C.dock)

    -- CPU
    local cpu = claos.cpu()
    g.text(sx+12, sy+22, string.format("CPU %d%%", cpu), C.t3, C.dock)
    g.rounded_rect(sx+70, sy+24, 50, 6, 2, C.bd)
    local cw = math.floor(46 * cpu / 100)
    if cw > 0 then g.rounded_rect(sx+72, sy+25, cw, 4, 1, C.gn) end

    -- Memory (both APIs return bytes)
    local total_b = claos.mem_total()
    local free_b = claos.mem_free()
    local used_b = total_b - free_b
    local mem_pct = 0
    if total_b > 0 then mem_pct = math.floor(used_b / total_b * 100) end
    g.text(sx+12, sy+36, string.format("Mem %d%%", mem_pct), C.t3, C.dock)
    g.rounded_rect(sx+70, sy+38, 108, 6, 2, C.bd)
    local bw = math.floor(104 * mem_pct / 100)
    if bw > 0 then g.rounded_rect(sx+72, sy+39, bw, 4, 1, C.ac) end

    -- Memory text
    g.text(sx+130, sy+22, string.format("%dMB", math.floor(used_b/1048576)), C.t3, C.dock)
end

-- ═══════════════════════════════════════
-- Window rendering
-- ═══════════════════════════════════════
local function draw_window(win)
    local focused = (win == get_focused())

    -- Shadow
    g.rect(win.x+3, win.y+3, win.w, win.h, C.shadow)

    -- Window body
    g.rounded_rect(win.x, win.y, win.w, win.h, 6, C.win)

    -- Title bar
    local tb_col = focused and C.title or C.title_u
    g.rounded_rect(win.x, win.y, win.w, TB, 6, tb_col)
    g.rect(win.x, win.y+TB-6, win.w, 6, tb_col)
    g.hline(win.x, win.y+TB-1, win.w, C.bd)

    -- Window icon (small, in title bar)
    local fn = dock_icon_fn[win.app.name]
    if fn then
        fn(win.x+6, win.y+4, 20, focused and C.t1 or C.t3)
    end

    -- Title text
    g.text(win.x + (fn and 28 or 10), win.y+6, win.title,
        focused and C.t1 or C.t2, tb_col)

    -- Close button (X)
    local cx = win.x + win.w - BTN_SZ - BTN_PAD
    local cy = win.y + BTN_PAD
    g.rounded_rect(cx, cy, BTN_SZ, BTN_SZ, 4, C.rd)
    g.text(cx+4, cy, "x", C.white, C.rd)

    -- Minimize button (-)
    local mx2 = cx - BTN_SZ - 4
    g.rounded_rect(mx2, cy, BTN_SZ, BTN_SZ, 4, C.bd)
    g.hline(mx2+4, cy+7, 8, C.t2)

    -- Content area
    if win.error then
        -- Show error overlay instead of app content
        local ex, ey = win.x + 8, win.y + TB + 8
        g.rect(win.x, win.y + TB, win.w, win.h - TB, g.rgb(40,15,15))
        g.text_bold(ex, ey, "App Error", C.rd, g.rgb(40,15,15))
        -- Word-wrap error message
        local mc = math.floor((win.w - 20) / FW)
        local msg = win.error
        local ly = ey + 24
        while #msg > 0 and ly < win.y + win.h - 20 do
            local line = msg:sub(1, mc)
            g.text(ex, ly, line, C.t1, g.rgb(40,15,15))
            msg = msg:sub(mc + 1)
            ly = ly + FH
        end
        g.text(ex, win.y + win.h - 20, "Click to dismiss", C.t3, g.rgb(40,15,15))
    else
        app_call(win, "on_draw", win.x, win.y + TB, win.w, win.h - TB)
    end

    -- Resize grip (bottom-right corner — diagonal lines)
    local gx = win.x + win.w - 12
    local gy = win.y + win.h - 12
    g.line(gx+4, gy+10, gx+10, gy+4, C.t3)
    g.line(gx+7, gy+10, gx+10, gy+7, C.t3)
    g.line(gx+10, gy+10, gx+10, gy+10, C.t3)
end

local function hit_close(win)
    return hit(win.x + win.w - BTN_SZ - BTN_PAD, win.y + BTN_PAD,
        BTN_SZ, BTN_SZ, mouse_x, mouse_y)
end

local function hit_minimize(win)
    return hit(win.x + win.w - BTN_SZ*2 - BTN_PAD - 4, win.y + BTN_PAD,
        BTN_SZ, BTN_SZ, mouse_x, mouse_y)
end

local function hit_titlebar(win)
    return hit(win.x, win.y, win.w - BTN_SZ*2 - BTN_PAD*2, TB, mouse_x, mouse_y)
end

-- Returns resize edge string or nil
-- "br" = bottom-right, "b" = bottom, "r" = right
local function hit_resize(win)
    local mx, my = mouse_x, mouse_y
    local r = RESIZE_GRIP
    -- Bottom-right corner (priority)
    if mx >= win.x+win.w-r*2 and mx <= win.x+win.w and
       my >= win.y+win.h-r*2 and my <= win.y+win.h then
        return "br"
    end
    -- Right edge
    if mx >= win.x+win.w-r and mx <= win.x+win.w and
       my >= win.y+TB and my <= win.y+win.h then
        return "r"
    end
    -- Bottom edge
    if my >= win.y+win.h-r and my <= win.y+win.h and
       mx >= win.x and mx <= win.x+win.w then
        return "b"
    end
    return nil
end

-- ═══════════════════════════════════════
-- Mouse cursor
-- ═══════════════════════════════════════
local function draw_cursor(mx, my)
    for dy = 0, 11 do
        local w = (dy < 8) and (dy+1) or (12-dy)
        if w > 0 then
            g.hline(mx, my+dy, 1, C.t1)
            if w > 2 then g.hline(mx+1, my+dy, w-2, C.white) end
            if w > 1 then g.pixel(mx+w-1, my+dy, C.t1) end
        end
    end
end

-- ═══════════════════════════════════════
-- Draw everything
-- ═══════════════════════════════════════
local function draw_all()
    draw_bg()
    draw_desktop_icons()
    draw_dock()

    for i = 1, #wins do
        if wins[i].vis then draw_window(wins[i]) end
    end

    draw_status()
    draw_ctx_menu()
end

-- ═══════════════════════════════════════
-- Event loop
-- ═══════════════════════════════════════
draw_all(); draw_cursor(mouse_x, mouse_y); g.swap()

local run = true
local frame = 0

while run do
    local dirty = false

    while true do
        local ev = g.poll_event()
        if not ev then break end

        if ev.type == g.MOUSE_MOVE then
            mouse_x, mouse_y = ev.x, ev.y
            if resize then
                -- Handle window resize
                local r = resize
                local dx = mouse_x - r.ox
                local dy = mouse_y - r.oy
                if r.edge == "br" or r.edge == "r" then
                    r.win.w = math.max(r.win.min_w, r.ow + dx)
                end
                if r.edge == "br" or r.edge == "b" then
                    r.win.h = math.max(r.win.min_h, r.oh + dy)
                end
                -- Clamp to screen
                if r.win.x + r.win.w > W then r.win.w = W - r.win.x end
                if r.win.y + r.win.h > H then r.win.h = H - r.win.y end
            elseif drag then
                drag.win.x = mouse_x - drag.ox
                drag.win.y = mouse_y - drag.oy
                if drag.win.x < -(drag.win.w - 60) then drag.win.x = -(drag.win.w - 60) end
                if drag.win.y < 0 then drag.win.y = 0 end
                if drag.win.x > W - 60 then drag.win.x = W - 60 end
                if drag.win.y > H - TB then drag.win.y = H - TB end
            else
                dock_hover = get_dock_hover(mouse_x, mouse_y)
                -- Update context menu hover
                if ctx_menu then
                    ctx_menu.hover = ctx_menu_hittest(mouse_x, mouse_y)
                end
                -- Forward to focused window
                local fw = get_focused()
                if fw and fw.vis and not fw.error then
                    local rx = mouse_x - fw.x
                    local ry = mouse_y - fw.y - TB
                    if rx >= 0 and ry >= 0 and rx < fw.w and ry < fw.h - TB then
                        app_call(fw, "on_mouse_move", rx, ry)
                    end
                end
            end
            dirty = true

        elseif ev.type == g.MOUSE_DOWN then
            dirty = true
            local right_click = (ev.button and ev.button >= 2)

            -- Context menu handling
            if ctx_menu then
                local ci = ctx_menu_hittest(mouse_x, mouse_y)
                if ci > 0 and ctx_menu.items[ci].action then
                    ctx_menu.items[ci].action()
                end
                ctx_menu = nil  -- close menu regardless
            elseif right_click then
                -- Right-click context menus
                local on_window = false
                for i = #wins, 1, -1 do
                    local w = wins[i]
                    if w.vis and hit(w.x, w.y, w.w, w.h, mouse_x, mouse_y) then
                        on_window = true
                        wm_focus(w)
                        -- Window context menu
                        local items = {
                            {label="Close", action=function() wm_close(w) end},
                            {label="Minimize", action=function() wm_minimize(w) end},
                        }
                        show_ctx_menu(mouse_x, mouse_y, items)
                        break
                    end
                end
                if not on_window then
                    -- Desktop context menu
                    -- Check if right-clicked on a desktop icon
                    local on_icon = false
                    local ix = W - 90
                    local iy = 80
                    for i, item in ipairs(desk_items) do
                        if hit(ix-4, iy, 72, 64, mouse_x, mouse_y) then
                            on_icon = true
                            local fi = item  -- capture
                            local items = {}
                            if fi.is_dir then
                                items[#items+1] = {label="Open Folder", action=function()
                                    wm_open_with("files", {path=fi.full})
                                end}
                            else
                                items[#items+1] = {label="Open File", action=function()
                                    wm_open_with("notepad", {path=fi.full, name=fi.name})
                                end}
                            end
                            show_ctx_menu(mouse_x, mouse_y, items)
                            break
                        end
                        iy = iy + 74
                        if iy > H - 80 then break end
                    end
                    if not on_icon then
                        -- General desktop context menu
                        show_ctx_menu(mouse_x, mouse_y, {
                            {label="Open Terminal", action=function() wm_open("term") end},
                            {label="Open Files", action=function() wm_open("files") end},
                            {label="System Monitor", action=function() wm_open("monitor") end},
                            {label="Refresh Desktop", action=function() refresh_desktop() end},
                        })
                    end
                end
            elseif drag then
                -- ignore
            elseif mouse_x < DOCK_W then
                -- Dock click
                local idx = get_dock_hover(mouse_x, mouse_y)
                if idx == -2 then
                    -- Power menu — position next to button
                    local n = #dock_items
                    local dock_h = n * 48 + 110
                    local dy = math.max(20, math.floor((H - dock_h) / 2))
                    local pwr_y = dy + n * 48 + 18 + 48
                    show_ctx_menu(DOCK_W + 4, pwr_y - 72, {
                        {label="Shutdown", action=function() claos.shutdown() end},
                        {label="Reboot", action=function() claos.reboot() end},
                        {label="Exit to Shell", action=function() run = false end},
                    })
                elseif idx > 0 and idx <= #dock_items then
                    wm_open(dock_items[idx].name)
                end
            else
                -- Try windows (top to bottom)
                local handled = false
                for i = #wins, 1, -1 do
                    local w = wins[i]
                    if w.vis and hit(w.x, w.y, w.w, w.h, mouse_x, mouse_y) then
                        wm_focus(w)
                        if hit_close(w) then
                            wm_close(w)
                        elseif hit_minimize(w) then
                            wm_minimize(w)
                        elseif hit_resize(w) then
                            -- Start resize
                            local edge = hit_resize(w)
                            resize = {win=w, edge=edge, ox=mouse_x, oy=mouse_y, ow=w.w, oh=w.h}
                        elseif hit_titlebar(w) then
                            drag = {win=w, ox=mouse_x-w.x, oy=mouse_y-w.y}
                        else
                            if w.error then
                                w.error = nil  -- click dismisses error
                            else
                                app_call(w, "on_click", mouse_x-w.x, mouse_y-w.y-TB, ev.button)
                            end
                        end
                        handled = true
                        break
                    end
                end

                -- Desktop icons
                if not handled then
                    local ix = W - 90
                    local iy = 80
                    for i, item in ipairs(desk_items) do
                        if hit(ix-4, iy, 72, 64, mouse_x, mouse_y) then
                            if is_dblclick(1000+i) then
                                if item.is_dir then
                                    wm_open_with("files", {path=item.full})
                                else
                                    wm_open_with("notepad", {path=item.full, name=item.name})
                                end
                            else
                                desk_sel = i
                            end
                            break
                        end
                        iy = iy + 74
                        if iy > H - 80 then break end
                    end
                end
            end

        elseif ev.type == g.MOUSE_UP then
            if drag then drag = nil end
            if resize then resize = nil end
            -- Forward mouse up to focused window's app
            local fw = get_focused()
            if fw and fw.vis and not fw.error then
                app_call(fw, "on_mouse_up", mouse_x-fw.x, mouse_y-fw.y-TB)
            end
            dirty = true

        elseif ev.type == g.KEY_DOWN then
            dirty = true
            -- Close context menu on any key
            if ctx_menu then ctx_menu = nil
            elseif ev.key == 27 then
                local fw = get_focused()
                if fw then wm_close(fw)
                else run = false end
            else
                local fw = get_focused()
                if fw and fw.vis and fw.app.on_key and not fw.error then
                    app_call(fw, "on_key", ev.key)
                end
            end
        end
    end

    -- Handle Claude blocking calls (skip if shutting down)
    if run then for _, w in ipairs(wins) do
        if w.vis and w.app.name == "chat" and w.state.waiting then
            -- Force redraw to show "Thinking..." before blocking
            draw_all(); draw_cursor(mouse_x, mouse_y); g.swap()
            local last = w.state.msgs[#w.state.msgs]
            if last and last.f == "u" then
                local ok, r = pcall(claos.ask, last.t)
                if ok and r then
                    w.state.msgs[#w.state.msgs+1] = {f="c", t=r}
                else
                    local err_msg = ok and "(no response)" or ("Error: " .. tostring(r))
                    w.state.msgs[#w.state.msgs+1] = {f="c", t=err_msg}
                end
                w.state.waiting = false
                if w.state.sv then w.state.sv:scroll_to_bottom() end
            end
            dirty = true
        end
    end end  -- close "if run" and "for" from Claude blocking section

    frame = frame + 1
    -- Refresh periodically: every 20 frames (~320ms) for cursor blink
    if frame % 20 == 0 then dirty = true end

    if dirty then
        draw_all()
        draw_cursor(mouse_x, mouse_y)
        g.swap()
    end

    if not run then break end
    claos.sleep(16)
end
