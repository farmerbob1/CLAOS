-- CLAOS GUI — Interactive Desktop
local g = claos.gui

if not g.active() then
    if not g.activate() then
        print("ERROR: Cannot activate VESA mode")
        return
    end
end

local W, H = g.width(), g.height()

-- ── Theme ─────────────────────────────────────────────────
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
    hover      = g.rgb(235, 233, 228),
    fw = 8, fh = 16,
    top_h = 36, side_w = 56, rpan_w = 200,
}

-- ── Widget System ─────────────────────────────────────────
-- Every widget has: x, y, w, h, draw(), and optional on_click()

local widgets = {}
local focused_widget = nil  -- widget receiving keyboard input
local hover_widget = nil    -- widget under mouse

local function hit_test(wx, wy, ww, wh, mx, my)
    return mx >= wx and mx < wx + ww and my >= wy and my < wy + wh
end

local function make_button(x, y, w, h, label, opts)
    opts = opts or {}
    local btn = {
        x = x, y = y, w = w, h = h,
        label = label,
        bg = opts.bg or T.bg,
        fg = opts.fg or T.txt2,
        bg_hover = opts.bg_hover or T.hover,
        radius = opts.radius or 8,
        hovered = false,
        on_click = opts.on_click,
    }
    function btn:draw()
        local bg = self.hovered and self.bg_hover or self.bg
        g.rounded_rect(self.x, self.y, self.w, self.h, self.radius, bg)
        local tx = self.x + (self.w - #self.label * T.fw) / 2
        local ty = self.y + (self.h - T.fh) / 2
        g.text(math.floor(tx), math.floor(ty), self.label, self.fg, bg)
    end
    widgets[#widgets + 1] = btn
    return btn
end

local function make_textbox(x, y, w, h, placeholder)
    local tb = {
        x = x, y = y, w = w, h = h,
        text = "",
        placeholder = placeholder or "",
        cursor_pos = 0,
        focused = false,
        on_submit = nil,
    }
    function tb:draw()
        local bg = self.focused and T.white or T.bg
        g.rounded_rect(self.x, self.y, self.w, self.h, 12, bg)
        if self.focused then
            g.rect_outline(self.x, self.y, self.w, self.h, T.accent)
        end
        local tx = self.x + 12
        local ty = self.y + (self.h - T.fh) / 2
        if #self.text > 0 then
            -- Visible text (truncate to fit)
            local max_chars = math.floor((self.w - 24) / T.fw)
            local visible = self.text
            if #visible > max_chars then
                visible = string.sub(visible, #visible - max_chars + 1)
            end
            g.text(tx, math.floor(ty), visible, T.txt, bg)
            -- Cursor
            if self.focused then
                local cx = tx + #visible * T.fw
                g.vline(cx, self.y + 6, self.h - 12, T.accent)
            end
        else
            g.text(tx, math.floor(ty), self.placeholder, T.txt3, bg)
            if self.focused then
                g.vline(tx, self.y + 6, self.h - 12, T.accent)
            end
        end
    end
    function tb:key_input(ch)
        if ch == 8 then  -- backspace
            if #self.text > 0 then
                self.text = string.sub(self.text, 1, -2)
            end
        elseif ch == 10 or ch == 13 then  -- enter
            if self.on_submit and #self.text > 0 then
                self.on_submit(self.text)
                self.text = ""
            end
        elseif ch >= 32 and ch < 127 then
            self.text = self.text .. string.char(ch)
        end
    end
    widgets[#widgets + 1] = tb
    return tb
end

-- ── Chat State ────────────────────────────────────────────
local messages = {
    { from = "claude", text = "Welcome to CLAOS! I'm running natively inside your OS — connected directly to Anthropic's API over HTTPS. What would you like to do?" },
}
local chat_scroll = 0
local waiting_for_claude = false

local function add_message(from, text)
    messages[#messages + 1] = { from = from, text = text }
end

local function send_to_claude(user_text)
    add_message("user", user_text)
    waiting_for_claude = true
end

-- ── Layout Constants ──────────────────────────────────────
local chat_x = T.side_w
local chat_y = T.top_h
local chat_w = W - T.side_w - T.rpan_w
local chat_h = H - T.top_h

-- ── Create Widgets ────────────────────────────────────────
-- Chat input textbox
local chat_input = make_textbox(
    chat_x + 16, H - 52, chat_w - 72, 36,
    "Ask Claude anything..."
)
chat_input.focused = true
focused_widget = chat_input

-- Send button
local send_btn = make_button(
    chat_x + chat_w - 48, H - 52, 36, 36, ">",
    { bg = T.accent, fg = T.white, bg_hover = T.accent_dk, radius = 12 }
)

-- Wire up chat submission
local function submit_chat()
    if #chat_input.text > 0 then
        send_to_claude(chat_input.text)
        chat_input.text = ""
    end
end

chat_input.on_submit = submit_chat
send_btn.on_click = submit_chat

-- Sidebar buttons
local active_view = "chat"
local side_btns = {}
local side_items = {
    { "C", "chat" },
    { ">", "term" },
    { "M", "mon" },
    { "F", "files" },
}
for i, item in ipairs(side_items) do
    local sb = make_button(9, T.top_h + 12 + (i-1) * 42, 38, 38, item[1], {
        bg = (item[2] == active_view) and T.accent or T.bg,
        fg = (item[2] == active_view) and T.white or T.txt2,
        bg_hover = (item[2] == active_view) and T.accent or T.hover,
        radius = 10,
        on_click = function()
            active_view = item[2]
            -- Update sidebar button colors
            for j, sb2 in ipairs(side_btns) do
                local active = side_items[j][2] == active_view
                sb2.bg = active and T.accent or T.bg
                sb2.fg = active and T.white or T.txt2
                sb2.bg_hover = active and T.accent or T.hover
            end
        end,
    })
    side_btns[i] = sb
end

-- ── Word Wrap Helper ──────────────────────────────────────
local function wrap_text(text, max_chars)
    local lines = {}
    local pos = 1
    while pos <= #text do
        if #text - pos + 1 <= max_chars then
            lines[#lines + 1] = string.sub(text, pos)
            break
        end
        -- Find last space within max_chars
        local chunk = string.sub(text, pos, pos + max_chars - 1)
        local sp = nil
        for i = #chunk, 1, -1 do
            if string.sub(chunk, i, i) == " " then
                sp = i
                break
            end
        end
        if sp then
            lines[#lines + 1] = string.sub(chunk, 1, sp - 1)
            pos = pos + sp
        else
            lines[#lines + 1] = chunk
            pos = pos + max_chars
        end
    end
    return lines
end

-- ── Drawing Functions ─────────────────────────────────────
local function draw_topbar()
    g.rect(0, 0, W, T.top_h, T.white)
    g.hline(0, T.top_h - 1, W, T.border)

    g.rounded_rect(8, 8, 20, 20, 6, T.accent)
    g.text(11, 10, "C", T.white, T.accent)
    g.text(34, 12, "CLAOS", T.txt, T.white)
    g.text(82, 12, "v0.8", T.txt3, T.white)

    local rx = W - 240
    g.circle_filled(rx, 18, 3, T.green)
    g.text(rx + 8, 12, "Online", T.txt2, T.white)

    local mf = claos.mem_free()
    local mt = claos.mem_total()
    local used = mt - (mf * 4 / 1024)
    g.text(rx + 72, 12, string.format("%d / %d MB", math.floor(used), mt), T.txt2, T.white)

    local up = claos.uptime()
    g.text(rx + 172, 12, string.format("%02d:%02d", math.floor(up/60), up%60), T.txt2, T.white)
end

local function draw_sidebar()
    g.rect(0, T.top_h, T.side_w, H - T.top_h, T.panel)
    g.vline(T.side_w - 1, T.top_h, H - T.top_h, T.border)
    for _, sb in ipairs(side_btns) do sb:draw() end

    g.rounded_rect(9, H - 50, 38, 38, 10, T.bg)
    g.text(21, H - 39, "S", T.txt2, T.bg)
end

local function draw_rpanel()
    local px = W - T.rpan_w
    local py = T.top_h
    g.rect(px, py, T.rpan_w, H - py, T.panel)
    g.vline(px, py, H - py, T.border)

    local mx = px + 12
    local y = py + 16

    g.text(mx, y, "CLAUDE", T.txt3, T.panel)
    y = y + 20
    g.rounded_rect(mx, y, T.rpan_w - 24, 40, 10, T.white)
    g.circle_filled(mx + 14, y + 20, 4, waiting_for_claude and T.accent or T.green)
    g.text(mx + 24, y + 8, waiting_for_claude and "Thinking..." or "Active", T.txt, T.white)
    g.text(mx + 24, y + 24, "claude-4", T.txt3, T.white)
    y = y + 56

    g.text(mx, y, "PANIC WATCHER", T.txt3, T.panel)
    y = y + 20
    g.rounded_rect(mx, y, T.rpan_w - 24, 44, 10, T.white)
    g.circle_filled(mx + 14, y + 12, 4, T.green)
    g.text(mx + 24, y + 4, "Armed", T.txt, T.white)
    g.text(mx + 12, y + 24, "Watching Ring 0", T.txt3, T.white)
    y = y + 60

    g.text(mx, y, "MESSAGES", T.txt3, T.panel)
    y = y + 20
    g.rounded_rect(mx, y, T.rpan_w - 24, 28, 8, T.white)
    g.text(mx + 12, y + 6, tostring(#messages) .. " messages", T.txt4, T.white)

    y = H - 50
    g.rounded_rect(mx, y, T.rpan_w - 24, 40, 10, T.accent_lt)
    g.text(mx + 28, y + 4, "CLAOS v0.8", T.accent_dk, T.accent_lt)
    g.text(mx + 6, y + 22, "\"I am the kernel now.\"", T.accent, T.accent_lt)
end

local function draw_chat()
    g.rect(chat_x, chat_y, chat_w, chat_h, T.white)

    -- Header
    g.hline(chat_x, chat_y + 51, chat_w, T.border)
    g.circle_filled(chat_x + 28, chat_y + 26, 16, T.accent_lt)
    g.text(chat_x + 24, chat_y + 18, "C", T.accent, T.accent_lt)
    g.text(chat_x + 52, chat_y + 12, "Claude", T.txt, T.white)
    g.text(chat_x + 52, chat_y + 28, "Connected via HTTPS", T.green, T.white)

    -- Messages area
    local msg_y = chat_y + 64
    local msg_max_w = chat_w - 100
    local max_chars = math.floor(msg_max_w / T.fw)

    for _, msg in ipairs(messages) do
        if msg_y > H - 80 then break end  -- don't draw below input

        if msg.from == "claude" then
            -- Claude message (left aligned)
            g.circle_filled(chat_x + 22, msg_y + 12, 12, T.accent_lt)
            g.text(chat_x + 18, msg_y + 6, "C", T.accent, T.accent_lt)
            local lines = wrap_text(msg.text, max_chars)
            local bh = #lines * T.fh + 16
            g.rounded_rect(chat_x + 42, msg_y, msg_max_w, bh, 8, T.panel)
            for li, line in ipairs(lines) do
                g.text(chat_x + 52, msg_y + 8 + (li-1) * T.fh, line, T.txt, T.panel)
            end
            msg_y = msg_y + bh + 12
        else
            -- User message (right aligned)
            local lines = wrap_text(msg.text, max_chars - 4)
            local bh = #lines * T.fh + 16
            local bw = 0
            for _, line in ipairs(lines) do
                if #line * T.fw > bw then bw = #line * T.fw end
            end
            bw = bw + 24
            local bx = chat_x + chat_w - bw - 20
            g.rounded_rect(bx, msg_y, bw, bh, 8, T.accent)
            for li, line in ipairs(lines) do
                g.text(bx + 12, msg_y + 8 + (li-1) * T.fh, line, T.white, T.accent)
            end
            msg_y = msg_y + bh + 12
        end
    end

    -- Waiting indicator
    if waiting_for_claude then
        g.circle_filled(chat_x + 22, msg_y + 12, 12, T.accent_lt)
        g.text(chat_x + 18, msg_y + 6, "C", T.accent, T.accent_lt)
        g.rounded_rect(chat_x + 42, msg_y, 100, 32, 8, T.panel)
        g.text(chat_x + 52, msg_y + 8, "Thinking...", T.txt3, T.panel)
    end

    -- Input area
    g.hline(chat_x, H - 62, chat_w, T.border)
    g.rect(chat_x, H - 61, chat_w, 61, T.white)
    chat_input:draw()
    send_btn:draw()
end

local function draw_cursor(mx, my)
    for dy = 0, 11 do
        local w = (dy < 8) and (dy + 1) or (12 - dy)
        if w > 0 then
            g.hline(mx, my + dy, 1, T.txt)
            if w > 2 then g.hline(mx + 1, my + dy, w - 2, T.white) end
            if w > 1 then g.pixel(mx + w - 1, my + dy, T.txt) end
        end
    end
    g.pixel(mx, my + 12, T.txt)
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
    local old_hover = hover_widget

    -- Process events
    while true do
        local ev = g.poll_event()
        if not ev then break end

        if ev.type == g.KEY_DOWN then
            if ev.key == 27 then
                running = false
                break
            end
            -- Send to focused widget
            if focused_widget and focused_widget.key_input then
                focused_widget:key_input(ev.key)
                dirty = true
            end
        elseif ev.type == g.MOUSE_MOVE then
            last_mx, last_my = ev.x, ev.y
            -- Update hover state
            hover_widget = nil
            for _, w in ipairs(widgets) do
                if hit_test(w.x, w.y, w.w, w.h, ev.x, ev.y) then
                    w.hovered = true
                    hover_widget = w
                else
                    w.hovered = false
                end
            end
            dirty = true
        elseif ev.type == g.MOUSE_DOWN then
            -- Click handling
            for _, w in ipairs(widgets) do
                if hit_test(w.x, w.y, w.w, w.h, last_mx, last_my) then
                    if w.on_click then w.on_click() end
                    if w.key_input then
                        focused_widget = w
                        w.focused = true
                    end
                    dirty = true
                    break
                end
            end
        end
    end

    -- Process Claude response if waiting
    if waiting_for_claude then
        -- Try non-blocking Claude call
        local last_msg = messages[#messages]
        if last_msg.from == "user" then
            local resp = claos.ask(last_msg.text)
            if resp then
                add_message("claude", resp)
            else
                add_message("claude", "(No response from Claude)")
            end
            waiting_for_claude = false
            dirty = true
        end
    end

    -- Periodic refresh for uptime
    frame = frame + 1
    if frame % 90 == 0 then dirty = true end

    if dirty then
        draw_all()
        draw_cursor(last_mx, last_my)
        g.swap()
    end

    if not running then break end
    claos.sleep(16)
end
