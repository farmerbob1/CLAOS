-- CLAOS Shared UI Widgets
-- Reusable components for all apps
local g = claos.gui
local FW, FH = 8, 16
if not CLIPBOARD then CLIPBOARD = "" end

local W = {}

-- Shared theme colors
W.colors = {
    bg     = g.rgb(22,22,34),
    pn     = g.rgb(30,30,45),
    ac     = g.rgb(127,119,221),
    al     = g.rgb(50,46,80),
    t1     = g.rgb(210,208,220),
    t2     = g.rgb(140,138,150),
    t3     = g.rgb(100,98,110),
    gn     = g.rgb(80,200,80),
    rd     = g.rgb(220,100,80),
    yl     = g.rgb(220,180,60),
    bd     = g.rgb(45,45,60),
    ib     = g.rgb(35,35,50),
    white  = g.rgb(255,255,255),
    sel_bg = g.rgb(65,58,110),
    sel_fg = g.rgb(240,238,250),
}
local C = W.colors

-- ═══════════════════════════════════════
-- Label: styled text
-- ═══════════════════════════════════════
W.Label = {}
W.Label.__index = W.Label

function W.Label.new(opts)
    local self = setmetatable({}, W.Label)
    self.text = opts.text or ""
    self.color = opts.color or C.t1
    self.bold = opts.bold or false
    self.scale = opts.scale or 1  -- 1 = normal, 2 = double size
    return self
end

function W.Label:draw(x, y, bg)
    bg = bg or C.bg
    if self.scale == 2 then
        g.text_2x(x, y, self.text, self.color, bg)
    elseif self.bold then
        g.text_bold(x, y, self.text, self.color, bg)
    else
        g.text(x, y, self.text, self.color, bg)
    end
end

function W.Label:width()
    return #self.text * FW * self.scale
end

function W.Label:height()
    return FH * self.scale
end

function W.Label:set_text(t) self.text = t end

-- ═══════════════════════════════════════
-- Button: clickable button
-- ═══════════════════════════════════════
W.Button = {}
W.Button.__index = W.Button

function W.Button.new(opts)
    local self = setmetatable({}, W.Button)
    self.label = opts.label or "Button"
    self.on_click = opts.on_click
    self.color = opts.color or C.ac
    self.fg = opts.fg or C.white
    self.hover = false
    self.disabled = opts.disabled or false
    return self
end

function W.Button:draw(x, y, w, h)
    h = h or 28
    w = w or (#self.label * FW + 16)
    local col = self.disabled and C.bd or (self.hover and g.rgb(150,142,240) or self.color)
    local fg = self.disabled and C.t3 or self.fg
    g.rounded_rect(x, y, w, h, 6, col)
    g.text(x + math.floor((w - #self.label * FW) / 2),
           y + math.floor((h - FH) / 2), self.label, fg, col)
end

function W.Button:hit_test(mx, my, bx, by, bw, bh)
    return mx >= bx and mx < bx+bw and my >= by and my < by+bh
end

-- ═══════════════════════════════════════
-- ProgressBar
-- ═══════════════════════════════════════
W.ProgressBar = {}
W.ProgressBar.__index = W.ProgressBar

function W.ProgressBar.new(opts)
    local self = setmetatable({}, W.ProgressBar)
    self.value = opts.value or 0       -- 0-100
    self.color = opts.color or C.ac
    self.bg_color = opts.bg_color or C.bd
    self.auto_color = opts.auto_color or false  -- green/yellow/red based on value
    return self
end

function W.ProgressBar:set(v) self.value = math.max(0, math.min(100, v)) end

function W.ProgressBar:draw(x, y, w, h)
    h = h or 8
    g.rounded_rect(x, y, w, h, math.min(3, math.floor(h/2)), self.bg_color)
    if self.value > 0 then
        local bw = math.floor((w-4) * self.value / 100)
        if bw < 1 then bw = 1 end
        local col = self.color
        if self.auto_color then
            if self.value > 80 then col = C.rd
            elseif self.value > 50 then col = C.yl
            else col = C.gn end
        end
        g.rounded_rect(x+2, y+1, bw, h-2, math.min(2, math.floor(h/3)), col)
    end
end

-- ═══════════════════════════════════════
-- Panel: rounded rect container
-- ═══════════════════════════════════════
W.Panel = {}
W.Panel.__index = W.Panel

function W.Panel.new(opts)
    local self = setmetatable({}, W.Panel)
    self.color = opts.color or C.pn
    self.radius = opts.radius or 8
    return self
end

function W.Panel:draw(x, y, w, h)
    g.rounded_rect(x, y, w, h, self.radius, self.color)
end

-- ═══════════════════════════════════════
-- ScrollView: scrollable content area
-- ═══════════════════════════════════════
W.ScrollView = {}
W.ScrollView.__index = W.ScrollView

function W.ScrollView.new(opts)
    local self = setmetatable({}, W.ScrollView)
    self.scroll = 0
    self.content_h = 0
    self.view_h = 0
    return self
end

function W.ScrollView:max_scroll()
    local ch = self.content_h or 0
    local vh = self.view_h or 0
    if ch <= vh then return 0 end
    return ch - vh
end

function W.ScrollView:clamp()
    self.scroll = self.scroll or 0
    local mx = self:max_scroll()
    if self.scroll > mx then self.scroll = mx end
    if self.scroll < 0 then self.scroll = 0 end
end

function W.ScrollView:scroll_to_bottom()
    self.scroll = 999999
    self:clamp()
end

function W.ScrollView:scroll_by(delta)
    self.scroll = (self.scroll or 0) + delta
    self:clamp()
end

function W.ScrollView:begin_draw(x, y, w, h)
    self.view_h = h or 0
    self:clamp()
    return x, y - (self.scroll or 0)
end

function W.ScrollView:draw_scrollbar(x, y, w, h)
    local ch = self.content_h or 0
    local vh = self.view_h or 0
    if ch <= vh or ch == 0 then return end
    local mx = self:max_scroll()
    if mx <= 0 then return end
    local sb_h = math.max(16, math.floor(vh / ch * (h or vh)))
    local sb_y = y + math.floor((self.scroll or 0) / mx * ((h or vh) - sb_h))
    g.rounded_rect(x + w - 5, sb_y, 3, sb_h, 1, C.bd)
end

function W.ScrollView:handle_click(ry, view_h)
    local mid = (view_h or 100) / 2
    if ry < mid then self:scroll_by(-60) else self:scroll_by(60) end
end

-- ═══════════════════════════════════════
-- ListView: scrollable list of items
-- ═══════════════════════════════════════
W.ListView = {}
W.ListView.__index = W.ListView

function W.ListView.new(opts)
    local self = setmetatable({}, W.ListView)
    self.items = opts.items or {}        -- array of items (app-defined)
    self.row_h = opts.row_h or 32
    self.selected = -1
    self.scroll = 0
    self.on_select = opts.on_select      -- function(index, item)
    self.on_dblclick = opts.on_dblclick  -- function(index, item)
    self.render_item = opts.render_item  -- function(item, x, y, w, h, selected)
    -- Double-click tracking
    self._last_click_time = 0
    self._last_click_idx = -1
    return self
end

function W.ListView:set_items(items)
    self.items = items
    self.selected = -1
    -- Don't reset scroll — caller can do that if needed
end

function W.ListView:draw(x, y, w, h)
    local max_scroll = math.max(0, #self.items * self.row_h - h)
    if self.scroll > max_scroll then self.scroll = max_scroll end
    if self.scroll < 0 then self.scroll = 0 end

    local first = math.floor(self.scroll / self.row_h) + 1
    local visible = math.floor(h / self.row_h) + 2

    for i = first, math.min(#self.items, first + visible) do
        local ry = y + (i-1) * self.row_h - self.scroll
        if ry + self.row_h < y then goto continue end
        if ry > y + h then break end

        local sel = (i == self.selected)
        -- Default row bg
        local row_bg = sel and C.al or C.bg
        g.rect(x, ry, w, self.row_h - 2, row_bg)

        -- Custom render or default
        if self.render_item then
            self.render_item(self.items[i], x, ry, w, self.row_h, sel, row_bg)
        else
            g.text(x + 8, ry + math.floor((self.row_h - FH) / 2),
                   tostring(self.items[i]), C.t1, row_bg)
        end

        -- Separator
        g.hline(x + 4, ry + self.row_h - 2, w - 8, C.bd)
        ::continue::
    end

    -- Scrollbar
    if #self.items * self.row_h > h and max_scroll > 0 then
        local sb_h = math.max(16, math.floor(h / (#self.items * self.row_h) * h))
        local sb_y = y + math.floor(self.scroll / max_scroll * (h - sb_h))
        g.rounded_rect(x + w - 5, sb_y, 3, sb_h, 1, C.bd)
    end
end

function W.ListView:handle_click(rx, ry)
    local idx = math.floor((ry + self.scroll) / self.row_h) + 1
    if idx < 1 or idx > #self.items then return end

    -- Double-click detection
    local now = claos.uptime()
    local dbl = (idx == self._last_click_idx and (now - self._last_click_time) < 1)
    self._last_click_time = now
    self._last_click_idx = idx

    if dbl then
        if self.on_dblclick then self.on_dblclick(idx, self.items[idx]) end
    else
        self.selected = idx
        if self.on_select then self.on_select(idx, self.items[idx]) end
    end
end

function W.ListView:handle_scroll(delta)
    self.scroll = self.scroll + delta
end

-- ═══════════════════════════════════════
-- TextField: single-line text input
-- ═══════════════════════════════════════
W.TextField = {}
W.TextField.__index = W.TextField

function W.TextField.new(opts)
    local self = setmetatable({}, W.TextField)
    self.text = opts.text or ""
    self.cx = #self.text + 1
    self.scroll = 0
    self.placeholder = opts.placeholder or ""
    self.focused = false
    self.blink = 0
    self.sel_start = nil
    self.sel_end = nil
    self.dragging = false
    self.on_enter = opts.on_enter
    self.on_change = opts.on_change
    return self
end

function W.TextField:get_text() return self.text end
function W.TextField:set_text(t) self.text = t; self.cx = #t+1; self.sel_start = nil; self.sel_end = nil end
function W.TextField:focus() self.focused = true; self.blink = 0 end
function W.TextField:blur() self.focused = false; self.dragging = false end

function W.TextField:_norm_sel()
    if not self.sel_start or not self.sel_end then return nil, nil end
    local s, e = self.sel_start, self.sel_end
    if s > e then s, e = e, s end
    return s, e
end

function W.TextField:_del_sel()
    local s, e = self:_norm_sel()
    if not s then return false end
    self.text = self.text:sub(1, s-1) .. self.text:sub(e)
    self.cx = s; self.sel_start = nil; self.sel_end = nil
    return true
end

function W.TextField:_get_sel_text()
    local s, e = self:_norm_sel()
    if not s then return "" end
    return self.text:sub(s, e-1)
end

function W.TextField:_ensure_visible(vis_chars)
    if self.cx - self.scroll > vis_chars then self.scroll = self.cx - vis_chars end
    if self.cx - self.scroll < 1 then self.scroll = self.cx - 1 end
    if self.scroll < 0 then self.scroll = 0 end
end

function W.TextField:draw(x, y, w, h)
    h = h or (FH + 12)
    local bg = self.focused and g.rgb(40,40,58) or C.ib
    local bd = self.focused and C.ac or C.bd
    local fg = C.t1
    local ph = C.t3

    g.rounded_rect(x, y, w, h, math.min(6, math.floor(h/3)), bg)
    g.rect_outline(x, y, w, h, bd)

    local tx = x + 6
    local ty = y + math.floor((h - FH) / 2)
    local vis_chars = math.floor((w - 14) / FW)
    self:_ensure_visible(vis_chars)

    if #self.text == 0 and not self.focused then
        g.text(tx, ty, self.placeholder, ph, bg)
    else
        local display = self.text:sub(self.scroll + 1, self.scroll + vis_chars)
        local s, e = self:_norm_sel()
        for i = 1, #display do
            local ci = self.scroll + i
            local ch = display:sub(i, i)
            local px = tx + (i-1) * FW
            local in_sel = s and ci >= s and ci < e
            g.text(px, ty, ch, in_sel and C.sel_fg or fg, in_sel and C.sel_bg or bg)
        end
    end

    if self.focused then
        self.blink = self.blink + 1
        if (self.blink % 40) < 28 then
            local cur_x = tx + (self.cx - self.scroll - 1) * FW
            if cur_x >= tx and cur_x < x + w - 4 then
                g.rect(cur_x, ty - 1, 2, FH + 2, C.ac)
            end
        end
    end
end

function W.TextField:handle_key(key)
    if not self.focused then return false end
    self.blink = 0
    local K = g
    local ctrl  = (key & K.K_CTRL) ~= 0
    local shift = (key & K.K_SHIFT) ~= 0
    local base  = key & 0x1FF

    if ctrl then
        if base == 99 or base == 67 then CLIPBOARD = self:_get_sel_text(); return true
        elseif base == 118 or base == 86 then
            self:_del_sel()
            local paste = CLIPBOARD:match("([^\n]*)") or CLIPBOARD
            self.text = self.text:sub(1,self.cx-1)..paste..self.text:sub(self.cx)
            self.cx = self.cx + #paste
            if self.on_change then self.on_change(self.text) end; return true
        elseif base == 120 or base == 88 then
            CLIPBOARD = self:_get_sel_text(); self:_del_sel()
            if self.on_change then self.on_change(self.text) end; return true
        elseif base == 97 or base == 65 then
            self.sel_start = 1; self.sel_end = #self.text + 1; return true
        end; return true
    end

    local function _sel_begin()
        if shift and not self.sel_start then self.sel_start = self.cx; self.sel_end = self.cx end
    end
    local function _sel_finish()
        if shift then self.sel_end = self.cx else self.sel_start = nil; self.sel_end = nil end
    end

    if base == K.K_LEFT then _sel_begin()
        if self.cx > 1 then self.cx = self.cx-1 end; _sel_finish(); return true
    elseif base == K.K_RIGHT then _sel_begin()
        if self.cx <= #self.text then self.cx = self.cx+1 end; _sel_finish(); return true
    elseif base == K.K_HOME then _sel_begin(); self.cx = 1; _sel_finish(); return true
    elseif base == K.K_END then _sel_begin(); self.cx = #self.text+1; _sel_finish(); return true
    end

    if base == 8 then
        if self:_del_sel() then if self.on_change then self.on_change(self.text) end; return true end
        if self.cx > 1 then
            self.text = self.text:sub(1,self.cx-2)..self.text:sub(self.cx)
            self.cx = self.cx-1; if self.on_change then self.on_change(self.text) end
        end; return true
    end
    if base == K.K_DELETE then
        if self:_del_sel() then if self.on_change then self.on_change(self.text) end; return true end
        if self.cx <= #self.text then
            self.text = self.text:sub(1,self.cx-1)..self.text:sub(self.cx+1)
            if self.on_change then self.on_change(self.text) end
        end; return true
    end
    if base == 10 or base == 13 then
        if self.on_enter then self.on_enter(self.text) end; return true
    end
    if base == 9 then return false end
    if base >= 32 and base < 127 then
        self:_del_sel()
        self.text = self.text:sub(1,self.cx-1)..string.char(base)..self.text:sub(self.cx)
        self.cx = self.cx+1; if self.on_change then self.on_change(self.text) end; return true
    end
    return false
end

function W.TextField:handle_click(rx)
    self.blink = 0
    local col = math.floor((rx - 6) / FW) + 1 + self.scroll
    if col < 1 then col = 1 end
    if col > #self.text + 1 then col = #self.text + 1 end
    self.cx = col; self.sel_start = col; self.sel_end = col; self.dragging = true
end

function W.TextField:handle_mouse_move(rx)
    if not self.dragging then return end
    local col = math.floor((rx - 6) / FW) + 1 + self.scroll
    if col < 1 then col = 1 end
    if col > #self.text + 1 then col = #self.text + 1 end
    self.cx = col; self.sel_end = col
end

function W.TextField:handle_mouse_up()
    self.dragging = false
    if self.sel_start and self.sel_end and self.sel_start == self.sel_end then
        self.sel_start = nil; self.sel_end = nil
    end
end

return W
