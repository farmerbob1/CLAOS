-- CLAOS File Browser App (uses shared widgets)
local g = claos.gui
local FW, FH = 8, 16
local C = nil  -- set in on_open from WIDGETS.colors

local function fchildren(path)
    local all = claos.ls(path)
    if not all then return {} end
    local out = {}
    local pfx = path
    if pfx ~= "/" then pfx = pfx .. "/" end
    for _, f in ipairs(all) do
        local nm = f.name
        if #nm > #pfx then
            local rest = nm:sub(#pfx + 1)
            if not rest:find("/") then
                out[#out+1] = {name=rest, full=nm, size=f.size, is_dir=f.is_dir}
            end
        end
    end
    return out
end

local function render_file_row(item, x, y, w, h, sel, row_bg)
    -- Icon
    local ix, iy = x+8, y+4
    if item.is_dir then
        g.rounded_rect(ix, iy+2, 10, 6, 2, g.rgb(80,160,240))
        g.rounded_rect(ix, iy+5, 22, 16, 3, g.rgb(80,160,240))
        g.rounded_rect(ix+1, iy+7, 20, 12, 2, g.rgb(100,180,255))
    else
        local ext = item.name:match("%.(%w+)$") or ""
        local fc = g.rgb(160,160,180)
        if ext == "lua" then fc = g.rgb(80,120,220)
        elseif ext == "txt" then fc = g.rgb(180,180,100)
        elseif ext == "bsp" or ext == "ctx" then fc = g.rgb(220,80,80) end
        g.rounded_rect(ix+2, iy, 18, 22, 3, fc)
        g.rounded_rect(ix+3, iy+1, 16, 20, 2, g.rgb(240,240,240))
        g.rect(ix+14, iy, 6, 6, fc)
        g.hline(ix+5, iy+8, 10, g.rgb(180,180,180))
        g.hline(ix+5, iy+12, 8, g.rgb(180,180,180))
        g.hline(ix+5, iy+16, 10, g.rgb(180,180,180))
    end
    -- Name
    local t1 = WIDGETS.colors.t1
    local t3 = WIDGETS.colors.t3
    g.text(x+38, y+8, item.name, t1, row_bg)
    -- Size (right-aligned with padding, integer math only)
    if item.is_dir then
        local stxt = "Folder"
        g.text(x+w-8-#stxt*FW, y+8, stxt, t3, row_bg)
    else
        local sz = item.size
        local str
        if sz >= 1048576 then
            local whole = sz // 1048576
            local frac = ((sz % 1048576) * 100) // 1048576
            str = whole .. "." .. string.format("%02d", frac) .. " MB"
        elseif sz >= 1024 then
            local whole = sz // 1024
            local frac = ((sz % 1024) * 100) // 1024
            str = whole .. "." .. string.format("%02d", frac) .. " KB"
        else
            str = sz .. " B"
        end
        g.text(x+w-8-#str*FW, y+8, str, t3, row_bg)
    end
end

return {
    name = "files", title = "CLAOS Files", icon = "F",
    w = 480, h = 400, min_w = 300, min_h = 250,

    on_open = function(s)
        C = WIDGETS.colors
        s.path = (s.params and s.params.path) or "/"

        s.list = WIDGETS.ListView.new({
            row_h = 32,
            render_item = render_file_row,
            on_select = function(idx, item) end,
            on_dblclick = function(idx, item)
                if item.is_dir then
                    s.path = item.full
                    s.list:set_items(fchildren(s.path))
                    s.list.scroll = 0
                else
                    if WM and WM.open_with then
                        local ext = item.name:match("%.(%w+)$") or ""
                        if ext == "lua" then
                            WM.open_with("term", {run=item.full, name=item.name})
                        elseif ext == "bsp" or ext == "ctx" or ext == "bin" then
                            -- Binary files: open 3D engine if bsp, otherwise skip
                            if ext == "bsp" then
                                WM.open_with("3d", {level=item.full})
                            end
                        else
                            WM.open_with("notepad", {path=item.full, name=item.name})
                        end
                    end
                end
            end,
        })
        s.list:set_items(fchildren(s.path))
    end,

    on_draw = function(s, x, y, w, h)
        g.rect(x, y, w, h, C.bg)
        s._w = w; s._h = h

        -- Header bar
        local hdr_h = 28
        local panel = WIDGETS.Panel.new({color=C.pn})
        panel:draw(x, y, w, hdr_h)
        g.hline(x, y+hdr_h-1, w, C.bd)

        if s.path ~= "/" then
            local back = WIDGETS.Button.new({label="<", color=C.ib, fg=C.t1})
            back:draw(x+4, y+3, 22, 22)
            g.rounded_rect(x+30, y+6, 14, 10, 3, C.ac)
            g.rounded_rect(x+30, y+9, 18, 12, 3, C.ac)
            g.text(x+54, y+6, s.path, C.t1, C.pn)
        else
            g.rounded_rect(x+6, y+6, 14, 10, 3, C.ac)
            g.rounded_rect(x+6, y+9, 18, 12, 3, C.ac)
            g.text(x+30, y+6, "Home", C.t1, C.pn)
        end
        -- Item count
        g.text(x+w-70, y+6, #s.list.items.." items", C.t3, C.pn)

        -- Only refresh items when path changes
        if s._last_path ~= s.path then
            s.list:set_items(fchildren(s.path))
            s._last_path = s.path
        end

        -- List view
        local list_y = y + hdr_h + 2
        local list_h = h - hdr_h - 2
        s.list:draw(x, list_y, w, list_h)

        if #s.list.items == 0 then
            g.text(x+16, list_y+8, "Empty folder", C.t3, C.bg)
        end
    end,

    on_key = function(s, key) end,

    on_click = function(s, rx, ry, btn)
        -- Right click
        if btn and btn >= 2 then return end

        -- Back button
        if s.path ~= "/" and rx < 28 and ry < 28 then
            local p = s.path:match("(.+)/[^/]+$") or "/"
            s.path = p; s.list:set_items(fchildren(s.path)); s.list.scroll = 0
            return
        end

        -- List area
        if ry > 30 then
            s.list:handle_click(rx, ry - 30)
        end
    end,
}
