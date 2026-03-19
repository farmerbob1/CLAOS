-- CLAOS Notepad — Text Editor with soft wrap
local g = claos.gui
local FW, FH = 8, 16
if not CLIPBOARD then CLIPBOARD = "" end

-- Soft-wrap a single raw line into display segments
-- Returns array of {text, raw_line, col_start}
local function wrap_line(raw, max_cols)
    if #raw <= max_cols then
        return {{text=raw, col=1}}
    end
    local segs = {}
    local p = 1
    while p <= #raw do
        local chunk = raw:sub(p, p+max_cols-1)
        if p + max_cols - 1 >= #raw then
            segs[#segs+1] = {text=chunk, col=p}
            break
        end
        -- Try to break at space
        local bp = nil
        for i = #chunk, math.max(1, #chunk-20), -1 do
            if chunk:sub(i,i) == " " then bp = i; break end
        end
        if bp then
            segs[#segs+1] = {text=chunk:sub(1,bp), col=p}
            p = p + bp
        else
            segs[#segs+1] = {text=chunk, col=p}
            p = p + max_cols
        end
    end
    return segs
end

-- Build display map: array of {raw_line_idx, col_start, text}
local function build_display(lines, max_cols)
    local dm = {}
    for li = 1, #lines do
        local segs = wrap_line(lines[li], max_cols)
        for _, seg in ipairs(segs) do
            dm[#dm+1] = {li=li, col=seg.col, text=seg.text}
        end
    end
    return dm
end

-- Find display row for a raw cursor position
local function cursor_to_display(dm, cy, cx)
    for i, d in ipairs(dm) do
        if d.li == cy then
            local col_end = d.col + #d.text
            if cx >= d.col and cx <= col_end then
                return i, cx - d.col
            end
        end
    end
    -- Fallback: last display row of this line
    for i = #dm, 1, -1 do
        if dm[i].li == cy then return i, cx - dm[i].col end
    end
    return 1, 0
end

return {
    name = "notepad", title = "Notepad", icon = "N",
    w = 560, h = 440, min_w = 320, min_h = 200,
    hide_dock = true,

    on_open = function(s)
        s.lines = {""}
        s.cx = 1; s.cy = 1
        s.scroll_y = 0
        s.sel = nil
        s.dragging = false
        s.modified = false
        s.filepath = nil
        s.title = "Untitled"
        s.blink = 0
        s.dm = nil      -- display map (rebuilt on draw)
        s.last_w = 0

        if s.params and s.params.path then
            local content = claos.read(s.params.path)
            if content then
                s.filepath = s.params.path
                s.title = s.params.name or s.params.path
                s.lines = {}
                for line in (content.."\n"):gmatch("([^\n]*)\n") do
                    s.lines[#s.lines+1] = line
                end
                if #s.lines == 0 then s.lines = {""} end
            end
        end
    end,

    on_draw = function(s, x, y, w, h)
        local bg     = g.rgb(22,22,34)
        local gut_bg = g.rgb(18,18,28)
        local t1     = g.rgb(210,208,220)
        local t3     = g.rgb(100,98,110)
        local bd     = g.rgb(45,45,60)
        local ac     = g.rgb(127,119,221)
        local sel_bg = g.rgb(65,58,110)
        local sel_fg = g.rgb(240,238,250)
        local bar_bg = g.rgb(28,28,42)

        g.rect(x, y, w, h, bg)

        -- Menu bar
        local bar_h = 20
        g.rect(x, y, w, bar_h, bar_bg)
        g.hline(x, y+bar_h-1, w, bd)
        g.text(x+4, y+2, "Save", t3, bar_bg)
        g.text(x+44, y+2, "Copy", t3, bar_bg)
        g.text(x+84, y+2, "Paste", t3, bar_bg)
        g.text(x+132, y+2, "Cut", t3, bar_bg)
        if s.modified then g.text(x+w-20, y+2, "*", ac, bar_bg) end
        g.text(x+w-80, y+2, #s.lines.."L", t3, bar_bg)

        -- Editor geometry
        local LN_W = 36
        local ed_x = x + LN_W
        local ed_y = y + bar_h
        local ed_w = w - LN_W - 8
        local ed_h = h - bar_h - 14
        local vis_lines = math.floor(ed_h / FH)
        local vis_cols = math.floor(ed_w / FW)
        if vis_cols < 10 then vis_cols = 10 end

        s._vis = vis_lines
        s._vis_cols = vis_cols
        s._ed_x = ed_x
        s._ed_y = ed_y
        s._bar_h = bar_h
        s._LN_W = LN_W

        -- Rebuild display map if width changed
        if w ~= s.last_w or not s.dm then
            s.dm = build_display(s.lines, vis_cols)
            s.last_w = w
        end

        -- Find cursor display row and ensure visible
        local cur_drow, cur_dcol = cursor_to_display(s.dm, s.cy, s.cx)
        local max_sy = math.max(0, #s.dm - vis_lines)
        if s.scroll_y > max_sy then s.scroll_y = max_sy end
        if s.scroll_y < 0 then s.scroll_y = 0 end
        if cur_drow - s.scroll_y < 1 then s.scroll_y = cur_drow - 1 end
        if cur_drow - s.scroll_y > vis_lines then s.scroll_y = cur_drow - vis_lines end

        -- Gutter
        g.rect(x, ed_y, LN_W, ed_h, gut_bg)
        g.vline(x+LN_W-1, ed_y, ed_h, bd)

        -- Normalize selection
        local has_sel = false
        local nsy, nsx, ney, nex
        if s.sel then
            nsy, nsx, ney, nex = s.sel[1], s.sel[2], s.sel[3], s.sel[4]
            if nsy > ney or (nsy == ney and nsx > nex) then
                nsy, nsx, ney, nex = ney, nex, nsy, nsx
            end
            has_sel = true
        end

        -- Draw display rows
        local prev_li = -1
        for i = 1, vis_lines do
            local di = i + s.scroll_y
            if di > #s.dm then break end
            local d = s.dm[di]
            local ly = ed_y + (i-1) * FH

            -- Line number (only on first segment of a raw line)
            if d.li ~= prev_li then
                g.text(x+1, ly, string.format("%3d", d.li), t3, gut_bg)
                prev_li = d.li
            end

            -- Draw each character with correct bg
            local line_text = d.text
            for ci = 1, #line_text do
                local raw_col = d.col + ci - 1
                local ch = line_text:sub(ci, ci)
                local px = ed_x + (ci-1) * FW

                -- Check if this char is selected
                local in_sel = false
                if has_sel then
                    if d.li > nsy and d.li < ney then
                        in_sel = true
                    elseif d.li == nsy and d.li == ney then
                        in_sel = (raw_col >= nsx and raw_col < nex)
                    elseif d.li == nsy then
                        in_sel = (raw_col >= nsx)
                    elseif d.li == ney then
                        in_sel = (raw_col < nex)
                    end
                end

                local char_fg = in_sel and sel_fg or t1
                local char_bg = in_sel and sel_bg or bg
                g.text(px, ly, ch, char_fg, char_bg)
            end

            -- Fill selection highlight past end of text (for full-line selections)
            if has_sel then
                local after_col = d.col + #line_text
                local line_end = #s.lines[d.li] + 1
                -- Check if selection extends past visible text
                if d.li > nsy and d.li < ney then
                    -- whole line selected - fill to end
                    local fill_x = ed_x + #line_text * FW
                    if fill_x < ed_x + ed_w then
                        g.rect(fill_x, ly, ed_w - #line_text * FW, FH, sel_bg)
                    end
                elseif d.li == nsy and ney > nsy and after_col > nsx then
                    local fill_x = ed_x + #line_text * FW
                    if fill_x < ed_x + ed_w then
                        g.rect(fill_x, ly, ed_w - #line_text * FW, FH, sel_bg)
                    end
                end
            end
        end

        -- Cursor blink
        s.blink = s.blink + 1
        local show_cursor = (s.blink % 40) < 28
        local cdr = cur_drow - s.scroll_y
        if show_cursor and cdr >= 1 and cdr <= vis_lines then
            g.rect(ed_x + cur_dcol * FW, ed_y + (cdr-1)*FH, 2, FH, ac)
        end

        -- Scrollbar
        if #s.dm > vis_lines and max_sy > 0 then
            local sb_x = x + w - 6
            local sb_h = math.max(20, math.floor(vis_lines / #s.dm * ed_h))
            local sb_y = ed_y + math.floor(s.scroll_y / max_sy * (ed_h - sb_h))
            g.rounded_rect(sb_x, sb_y, 4, sb_h, 2, bd)
        end

        -- Status bar
        g.rect(x, y+h-14, w, 14, bar_bg)
        g.text(x+4, y+h-13, string.format("Ln %d Col %d", s.cy, s.cx), t3, bar_bg)
        if s.filepath then
            g.text(x+120, y+h-13, s.filepath, t3, bar_bg)
        end
    end,

    on_key = function(s, key)
        local K = g
        s.blink = 0
        s.dm = nil  -- force display rebuild

        local ctrl  = (key & K.K_CTRL) ~= 0
        local shift = (key & K.K_SHIFT) ~= 0
        local base  = key & 0x1FF

        local function ensure_vis()
            -- handled in on_draw via cur_drow
        end

        local function norm_sel()
            if not s.sel then return nil end
            local sy,sx,ey,ex = s.sel[1],s.sel[2],s.sel[3],s.sel[4]
            if sy > ey or (sy == ey and sx > ex) then sy,sx,ey,ex = ey,ex,sy,sx end
            return sy,sx,ey,ex
        end

        local function get_sel_text()
            local sy,sx,ey,ex = norm_sel()
            if not sy then return "" end
            if sy == ey then return s.lines[sy]:sub(sx, ex-1) end
            local t = s.lines[sy]:sub(sx)
            for i = sy+1, ey-1 do t = t .. "\n" .. s.lines[i] end
            return t .. "\n" .. s.lines[ey]:sub(1, ex-1)
        end

        local function del_sel()
            local sy,sx,ey,ex = norm_sel()
            if not sy then return false end
            if sy == ey then
                s.lines[sy] = s.lines[sy]:sub(1,sx-1) .. s.lines[sy]:sub(ex)
            else
                s.lines[sy] = s.lines[sy]:sub(1,sx-1) .. s.lines[ey]:sub(ex)
                for i = ey, sy+1, -1 do table.remove(s.lines, i) end
            end
            s.cy = sy; s.cx = sx; s.sel = nil; s.modified = true
            return true
        end

        local function sel_begin()
            if not s.sel then s.sel = {s.cy, s.cx, s.cy, s.cx} end
        end
        local function sel_update()
            if s.sel then s.sel[3] = s.cy; s.sel[4] = s.cx end
        end

        local function insert_text(txt)
            del_sel()
            local plines = {}
            for line in (txt.."\n"):gmatch("([^\n]*)\n") do plines[#plines+1] = line end
            if #plines == 0 then return end
            local l = s.lines[s.cy]
            local before = l:sub(1, s.cx-1)
            local after = l:sub(s.cx)
            if #plines == 1 then
                s.lines[s.cy] = before .. plines[1] .. after
                s.cx = s.cx + #plines[1]
            else
                s.lines[s.cy] = before .. plines[1]
                for i = 2, #plines-1 do
                    table.insert(s.lines, s.cy+i-1, plines[i])
                end
                table.insert(s.lines, s.cy+#plines-1, plines[#plines] .. after)
                s.cy = s.cy + #plines - 1
                s.cx = #plines[#plines] + 1
            end
            s.modified = true
        end

        -- Ctrl shortcuts (base is ASCII letter)
        if ctrl then
            if base == 115 or base == 83 then  -- s/S = Save
                if s.filepath then
                    local content = table.concat(s.lines, "\n")
                    if claos.write(s.filepath, content) then s.modified = false end
                end; return
            elseif base == 99 or base == 67 then  -- c/C = Copy
                CLIPBOARD = get_sel_text(); return
            elseif base == 118 or base == 86 then  -- v/V = Paste
                if #CLIPBOARD > 0 then insert_text(CLIPBOARD) end; return
            elseif base == 120 or base == 88 then  -- x/X = Cut
                CLIPBOARD = get_sel_text(); del_sel(); return
            elseif base == 97 or base == 65 then  -- a/A = Select All
                s.sel = {1, 1, #s.lines, #s.lines[#s.lines]+1}; return
            end
            return
        end

        -- Arrow keys
        if base == K.K_UP then
            if shift then sel_begin() end
            if s.cy > 1 then s.cy = s.cy - 1
                if s.cx > #s.lines[s.cy]+1 then s.cx = #s.lines[s.cy]+1 end
            end
            if shift then sel_update() else s.sel = nil end; return
        elseif base == K.K_DOWN then
            if shift then sel_begin() end
            if s.cy < #s.lines then s.cy = s.cy + 1
                if s.cx > #s.lines[s.cy]+1 then s.cx = #s.lines[s.cy]+1 end
            end
            if shift then sel_update() else s.sel = nil end; return
        elseif base == K.K_LEFT then
            if shift then sel_begin() end
            if s.cx > 1 then s.cx = s.cx - 1
            elseif s.cy > 1 then s.cy = s.cy-1; s.cx = #s.lines[s.cy]+1 end
            if shift then sel_update() else s.sel = nil end; return
        elseif base == K.K_RIGHT then
            if shift then sel_begin() end
            if s.cx <= #s.lines[s.cy] then s.cx = s.cx + 1
            elseif s.cy < #s.lines then s.cy = s.cy+1; s.cx = 1 end
            if shift then sel_update() else s.sel = nil end; return
        elseif base == K.K_HOME then
            if shift then sel_begin() end; s.cx = 1
            if shift then sel_update() else s.sel = nil end; return
        elseif base == K.K_END then
            if shift then sel_begin() end; s.cx = #s.lines[s.cy]+1
            if shift then sel_update() else s.sel = nil end; return
        elseif base == K.K_PGUP then
            s.cy = math.max(1, s.cy-20)
            if s.cx > #s.lines[s.cy]+1 then s.cx = #s.lines[s.cy]+1 end
            s.sel = nil; return
        elseif base == K.K_PGDN then
            s.cy = math.min(#s.lines, s.cy+20)
            if s.cx > #s.lines[s.cy]+1 then s.cx = #s.lines[s.cy]+1 end
            s.sel = nil; return
        elseif base == K.K_DELETE then
            if s.sel then del_sel()
            elseif s.cx <= #s.lines[s.cy] then
                local l = s.lines[s.cy]
                s.lines[s.cy] = l:sub(1,s.cx-1)..l:sub(s.cx+1); s.modified = true
            elseif s.cy < #s.lines then
                s.lines[s.cy] = s.lines[s.cy]..s.lines[s.cy+1]
                table.remove(s.lines, s.cy+1); s.modified = true
            end; return
        end

        -- Backspace
        if base == 8 then
            if s.sel then del_sel()
            elseif s.cx > 1 then
                local l = s.lines[s.cy]
                s.lines[s.cy] = l:sub(1,s.cx-2)..l:sub(s.cx)
                s.cx = s.cx-1; s.modified = true
            elseif s.cy > 1 then
                s.cx = #s.lines[s.cy-1]+1
                s.lines[s.cy-1] = s.lines[s.cy-1]..s.lines[s.cy]
                table.remove(s.lines, s.cy); s.cy = s.cy-1; s.modified = true
            end; return
        end

        -- Enter
        if base == 10 or base == 13 then
            del_sel()
            local l = s.lines[s.cy]
            s.lines[s.cy] = l:sub(1,s.cx-1)
            table.insert(s.lines, s.cy+1, l:sub(s.cx))
            s.cy = s.cy+1; s.cx = 1; s.modified = true; return
        end

        -- Tab
        if base == 9 then
            del_sel()
            local l = s.lines[s.cy]
            s.lines[s.cy] = l:sub(1,s.cx-1).."    "..l:sub(s.cx)
            s.cx = s.cx+4; s.modified = true; return
        end

        -- Printable
        if base >= 32 and base < 127 then
            del_sel()
            local l = s.lines[s.cy]
            s.lines[s.cy] = l:sub(1,s.cx-1)..string.char(base)..l:sub(s.cx)
            s.cx = s.cx+1; s.modified = true
        end
    end,

    on_click = function(s, rx, ry, btn)
        s.blink = 0; s.dm = nil
        local bar_h = s._bar_h or 20
        local LN_W = s._LN_W or 36

        -- Menu bar
        if ry < bar_h then
            if rx < 36 then  -- Save
                if s.filepath then
                    local content = table.concat(s.lines, "\n")
                    if claos.write(s.filepath, content) then s.modified = false end
                end
            elseif rx < 76 then  -- Copy
                if s.sel then
                    local sy,sx,ey,ex = s.sel[1],s.sel[2],s.sel[3],s.sel[4]
                    if sy > ey or (sy == ey and sx > ex) then sy,sx,ey,ex = ey,ex,sy,sx end
                    if sy == ey then CLIPBOARD = s.lines[sy]:sub(sx, ex-1)
                    else
                        local t = s.lines[sy]:sub(sx)
                        for i = sy+1, ey-1 do t = t.."\n"..s.lines[i] end
                        CLIPBOARD = t.."\n"..s.lines[ey]:sub(1, ex-1)
                    end
                end
            elseif rx < 124 then  -- Paste
                if #CLIPBOARD > 0 then
                    local plines = {}
                    for line in (CLIPBOARD.."\n"):gmatch("([^\n]*)\n") do plines[#plines+1] = line end
                    if #plines >= 1 then
                        if s.sel then
                            local sy,sx,ey,ex = s.sel[1],s.sel[2],s.sel[3],s.sel[4]
                            if sy > ey or (sy == ey and sx > ex) then sy,sx,ey,ex = ey,ex,sy,sx end
                            if sy == ey then
                                s.lines[sy] = s.lines[sy]:sub(1,sx-1)..s.lines[sy]:sub(ex)
                            else
                                s.lines[sy] = s.lines[sy]:sub(1,sx-1)..s.lines[ey]:sub(ex)
                                for i = ey, sy+1, -1 do table.remove(s.lines, i) end
                            end
                            s.cy = sy; s.cx = sx; s.sel = nil
                        end
                        local l = s.lines[s.cy]
                        local before = l:sub(1,s.cx-1); local after = l:sub(s.cx)
                        if #plines == 1 then
                            s.lines[s.cy] = before..plines[1]..after
                            s.cx = s.cx + #plines[1]
                        else
                            s.lines[s.cy] = before..plines[1]
                            for i = 2, #plines-1 do table.insert(s.lines, s.cy+i-1, plines[i]) end
                            table.insert(s.lines, s.cy+#plines-1, plines[#plines]..after)
                            s.cy = s.cy+#plines-1; s.cx = #plines[#plines]+1
                        end
                        s.modified = true
                    end
                end
            elseif rx < 156 then  -- Cut
                if s.sel then
                    local sy,sx,ey,ex = s.sel[1],s.sel[2],s.sel[3],s.sel[4]
                    if sy > ey or (sy == ey and sx > ex) then sy,sx,ey,ex = ey,ex,sy,sx end
                    if sy == ey then CLIPBOARD = s.lines[sy]:sub(sx, ex-1)
                    else
                        local t = s.lines[sy]:sub(sx)
                        for i = sy+1, ey-1 do t = t.."\n"..s.lines[i] end
                        CLIPBOARD = t.."\n"..s.lines[ey]:sub(1, ex-1)
                    end
                    if sy == ey then
                        s.lines[sy] = s.lines[sy]:sub(1,sx-1)..s.lines[sy]:sub(ex)
                    else
                        s.lines[sy] = s.lines[sy]:sub(1,sx-1)..s.lines[ey]:sub(ex)
                        for i = ey, sy+1, -1 do table.remove(s.lines, i) end
                    end
                    s.cy = sy; s.cx = sx; s.sel = nil; s.modified = true
                end
            end
            return
        end

        -- Editor area: position cursor via display map
        if s.dm then
            local drow = math.floor((ry - bar_h) / FH) + 1 + s.scroll_y
            if drow < 1 then drow = 1 end
            if drow > #s.dm then drow = #s.dm end
            local d = s.dm[drow]
            local col_offset = math.floor((rx - LN_W) / FW)
            if col_offset < 0 then col_offset = 0 end
            local raw_col = d.col + col_offset
            if raw_col > #s.lines[d.li] + 1 then raw_col = #s.lines[d.li] + 1 end
            s.cy = d.li; s.cx = raw_col
        else
            local col = math.floor((rx - LN_W) / FW) + 1
            local row = math.floor((ry - bar_h) / FH) + 1 + s.scroll_y
            if col < 1 then col = 1 end
            if row < 1 then row = 1 end
            if row > #s.lines then row = #s.lines end
            if col > #s.lines[row]+1 then col = #s.lines[row]+1 end
            s.cy = row; s.cx = col
        end

        s.sel = {s.cy, s.cx, s.cy, s.cx}
        s.dragging = true
    end,

    on_mouse_move = function(s, rx, ry)
        if not s.dragging then return end
        local bar_h = s._bar_h or 20
        local LN_W = s._LN_W or 36

        if s.dm then
            local drow = math.floor((ry - bar_h) / FH) + 1 + s.scroll_y
            if drow < 1 then drow = 1 end
            if drow > #s.dm then drow = #s.dm end
            local d = s.dm[drow]
            local col_offset = math.floor((rx - LN_W) / FW)
            if col_offset < 0 then col_offset = 0 end
            local raw_col = d.col + col_offset
            if raw_col > #s.lines[d.li]+1 then raw_col = #s.lines[d.li]+1 end
            s.cy = d.li; s.cx = raw_col
        end

        if s.sel then s.sel[3] = s.cy; s.sel[4] = s.cx end

        -- Auto-scroll
        local vis = s._vis or 20
        local scroll_row = 0
        if s.dm then
            for i, d in ipairs(s.dm) do
                if d.li == s.cy and s.cx >= d.col and s.cx <= d.col + #d.text then
                    scroll_row = i; break
                end
            end
        end
        if scroll_row > 0 then
            if scroll_row - s.scroll_y < 2 and s.scroll_y > 0 then
                s.scroll_y = s.scroll_y - 1
            elseif scroll_row - s.scroll_y > vis - 1 then
                s.scroll_y = s.scroll_y + 1
            end
        end
    end,

    on_mouse_up = function(s)
        s.dragging = false
        if s.sel and s.sel[1] == s.sel[3] and s.sel[2] == s.sel[4] then
            s.sel = nil
        end
    end,
}
