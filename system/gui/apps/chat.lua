-- CLAOS Claude Chat App (uses shared widgets)
local g = claos.gui
local FW, FH = 8, 16

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

return {
    name = "chat", title = "Chat", icon = "C",
    w = 500, h = 450, min_w = 360, min_h = 300,

    on_open = function(s)
        s.msgs = {{f="c", t="Welcome to CLAOS! I'm Claude, running natively inside your OS."}}
        s.waiting = false
        s.sv = WIDGETS.ScrollView.new({})

        s.input = WIDGETS.TextField.new({
            placeholder = "Ask Claude anything...",
            on_enter = function(text)
                if #text > 0 and not s.waiting then
                    s.msgs[#s.msgs+1] = {f="u", t=text}
                    s.input:set_text("")
                    s.waiting = true
                    s.sv:scroll_to_bottom()
                end
            end,
        })
        s.input:focus()

        s.send_btn = WIDGETS.Button.new({label=">", color=WIDGETS.colors.ac})
    end,

    on_draw = function(s, x, y, w, h)
        local C = WIDGETS.colors
        g.rect(x, y, w, h, C.bg)
        s._w = w; s._h = h

        -- Header
        local hdr = WIDGETS.Panel.new({color=C.pn})
        hdr:draw(x, y, w, 44)
        g.hline(x, y+43, w, C.bd)
        g.circle_filled(x+22, y+22, 12, C.al)
        g.text(x+18, y+16, "C", C.ac, C.al)
        g.text(x+42, y+10, "Claude", C.t1, C.pn)
        g.text(x+42, y+26, s.waiting and "Thinking..." or "Connected",
               s.waiting and C.ac or C.gn, C.pn)

        -- Message area
        local msg_top = y + 44
        local msg_bot = y + h - 52
        local msg_h = msg_bot - msg_top
        local mc = math.floor((w - 60) / FW)
        if mc < 10 then mc = 10 end

        -- Calc total content height
        local total = 0
        for _, m in ipairs(s.msgs) do
            total = total + #wrap(m.t, mc) * FH + 20
        end
        if s.waiting then total = total + 36 end
        s.sv.content_h = total
        s.sv.view_h = msg_h

        local _, oy = s.sv:begin_draw(x, msg_top)
        local my = oy + 6

        for _, m in ipairs(s.msgs) do
            local ls = wrap(m.t, mc)
            local lh = #ls * FH + 12
            if my + lh > msg_top - 20 and my < msg_bot + 20 then
                if m.f == "c" then
                    if my >= msg_top - lh then
                        g.rounded_rect(x+10, math.max(my, msg_top), w-20,
                            math.min(lh, msg_bot - math.max(my, msg_top)), 8, C.pn)
                    end
                    for j, l in ipairs(ls) do
                        local ly = my + 6 + (j-1)*FH
                        if ly >= msg_top and ly+FH <= msg_bot then
                            g.text(x+18, ly, l, C.t1, C.pn)
                        end
                    end
                else
                    local mw = 0
                    for _, l in ipairs(ls) do if #l*FW > mw then mw = #l*FW end end
                    mw = mw + 20; if mw > w-20 then mw = w-20 end
                    if my >= msg_top - lh then
                        g.rounded_rect(x+w-mw-10, math.max(my, msg_top), mw,
                            math.min(lh, msg_bot - math.max(my, msg_top)), 8, C.ac)
                    end
                    for j, l in ipairs(ls) do
                        local ly = my + 6 + (j-1)*FH
                        if ly >= msg_top and ly+FH <= msg_bot then
                            g.text(x+w-mw, ly, l, C.white, C.ac)
                        end
                    end
                end
            end
            my = my + lh + 8
        end

        if s.waiting and my >= msg_top and my+28 <= msg_bot then
            g.rounded_rect(x+10, my, 110, 28, 8, C.pn)
            g.text(x+18, my+6, "Thinking...", C.t3, C.pn)
        end

        s.sv:draw_scrollbar(x, msg_top, w, msg_h)

        -- Input bar
        g.rect(x, y+h-52, w, 52, C.bg)
        g.hline(x, y+h-52, w, C.bd)
        s.input:draw(x+10, y+h-44, w-56, 32)
        s.send_btn:draw(x+w-40, y+h-44, 32, 32)
    end,

    on_key = function(s, key)
        s.input:handle_key(key)
    end,

    on_click = function(s, rx, ry, btn)
        local w = s._w or 500; local h = s._h or 450
        -- Send button
        if s.send_btn:hit_test(rx, ry, w-40, h-44-28, 32, 32) then
            local text = s.input:get_text()
            if #text > 0 and not s.waiting then
                s.msgs[#s.msgs+1] = {f="u", t=text}
                s.input:set_text(""); s.waiting = true; s.sv:scroll_to_bottom()
            end; return
        end
        -- Input field
        if ry >= h-52 and rx < w-46 then s.input:handle_click(rx-10); return end
        -- Message area scroll
        if ry > 44 and ry < h-52 then s.sv:handle_click(ry-44, h-52-44) end
    end,

    on_mouse_move = function(s, rx, ry)
        s.input:handle_mouse_move(rx - 10)
    end,

    on_mouse_up = function(s)
        s.input:handle_mouse_up()
    end,
}
