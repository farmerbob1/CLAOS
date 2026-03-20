-- CLAOS Terminal App (uses shared widgets)
local g = claos.gui
local FW, FH = 8, 16

return {
    name = "term", title = "Terminal", icon = "T",
    w = 520, h = 380, min_w = 320, min_h = 200,

    on_open = function(s)
        s.lines = {"CLAOS Terminal v0.8", "Type commands. help for list.", ""}
        s.sv = WIDGETS.ScrollView.new({})
        s.wrap_width = 60  -- chars per line, updated in on_draw

        -- Word-wrap a string into s.lines
        s.add_text = function(text)
            local cpl = s.wrap_width
            for line in (text.."\n"):gmatch("([^\n]*)\n") do
                if #line <= cpl then
                    s.lines[#s.lines+1] = line
                else
                    -- Wrap long lines
                    local pos = 1
                    while pos <= #line do
                        -- Try to break at a space
                        local chunk = line:sub(pos, pos + cpl - 1)
                        if #chunk < cpl or pos + cpl > #line then
                            s.lines[#s.lines+1] = chunk
                            pos = pos + #chunk
                        else
                            local sp = chunk:reverse():find(" ")
                            if sp and sp < cpl - 10 then
                                local brk = cpl - sp
                                s.lines[#s.lines+1] = line:sub(pos, pos + brk - 1)
                                pos = pos + brk + 1
                            else
                                s.lines[#s.lines+1] = chunk
                                pos = pos + cpl
                            end
                        end
                    end
                end
            end
        end

        -- Helper: run a Lua script file, capturing print output
        s.run_script = function(path)
            s.lines[#s.lines+1] = "Running: " .. path
            s.lines[#s.lines+1] = ""
            local src = claos.read(path)
            if not src then
                s.lines[#s.lines+1] = "Error: could not read " .. path
                s.lines[#s.lines+1] = ""
                return
            end
            local fn, err = load(src, path)
            if not fn then
                s.lines[#s.lines+1] = "Parse error: " .. tostring(err)
                s.lines[#s.lines+1] = ""
                return
            end
            -- Override print to capture into terminal
            local old_print = print
            print = function(...)
                local parts = {}
                for i = 1, select("#", ...) do
                    parts[#parts+1] = tostring(select(i, ...))
                end
                s.add_text(table.concat(parts, "\t"))
            end
            local ok, rerr = pcall(fn)
            print = old_print
            if not ok then
                s.lines[#s.lines+1] = "Runtime error: " .. tostring(rerr)
            end
            s.lines[#s.lines+1] = ""
            s.sv:scroll_to_bottom()
        end

        s.input = WIDGETS.TextField.new({
            placeholder = "",
            on_enter = function(text)
                if #text == 0 then return end
                s.lines[#s.lines+1] = "claos> " .. text
                s.input:set_text("")
                local cmd = text

                if cmd == "help" then
                    s.lines[#s.lines+1] = "Commands: help sysinfo uptime ls ping clear run"
                elseif cmd == "clear" then
                    s.lines = {""}
                elseif cmd:sub(1,5) == "ping " then
                    local host = cmd:sub(6)
                    if #host == 0 then
                        s.lines[#s.lines+1] = "Usage: ping <hostname>"
                    else
                        s.lines[#s.lines+1] = "Pinging " .. host .. "..."
                        s.lines[#s.lines+1] = "Asking Claude..."
                        s.sv:scroll_to_bottom()
                        local ok, r = pcall(claos.ask, "Reply with only: 'Pong from Claude! Host: " .. host .. " - Network OK' Nothing else.")
                        if ok and r then
                            s.add_text(r)
                        else
                            s.lines[#s.lines+1] = "Network error: could not reach Claude API"
                        end
                    end
                elseif cmd == "sysinfo" then
                    local mt = claos.mem_total(); local mf = claos.mem_free()
                    s.lines[#s.lines+1] = string.format("Mem: %dMB total, %dMB free",
                        math.floor(mt/1048576), math.floor(mf/1048576))
                    s.lines[#s.lines+1] = "Up: " .. claos.uptime() .. "s"
                elseif cmd == "uptime" then
                    s.lines[#s.lines+1] = claos.uptime() .. "s"
                elseif cmd == "ls" or cmd:sub(1,3) == "ls " then
                    local path = #cmd > 3 and cmd:sub(4) or "/"
                    local fs = claos.ls(path)
                    if fs then for _, f in ipairs(fs) do
                        s.lines[#s.lines+1] = (f.is_dir and "d " or "  ") .. f.name
                    end else s.lines[#s.lines+1] = "ls: not found" end
                elseif cmd:sub(1,4) == "run " then
                    local path = cmd:sub(5)
                    s.run_script(path)
                else
                    s.lines[#s.lines+1] = "Asking Claude (agent mode)..."
                    s.sv:scroll_to_bottom()
                    local ok, r = pcall(claos.ask, cmd)
                    if ok and r then
                        s.add_text(r)
                    else
                        s.lines[#s.lines+1] = ok and "(no response)" or ("Error: "..tostring(r))
                    end
                end
                s.lines[#s.lines+1] = ""
                s.sv:scroll_to_bottom()
            end,
        })
        s.input:focus()

        -- Auto-run script if opened with params.run
        if s.params and s.params.run then
            s.run_script(s.params.run)
        end
    end,

    on_draw = function(s, x, y, w, h)
        local tbg = g.rgb(16,16,24)
        local fg = g.rgb(80,220,80)
        g.rect(x, y, w, h, tbg)
        s._w = w; s._h = h
        s.wrap_width = math.floor((w - 16) / FW)

        -- Output area
        local out_h = h - 30
        s.sv.content_h = #s.lines * FH
        s.sv.view_h = out_h
        s.sv:clamp()

        local first = math.floor(s.sv.scroll / FH) + 1
        local visible = math.floor(out_h / FH) + 1
        for i = first, math.min(#s.lines, first + visible) do
            local ly = y + 2 + (i-1)*FH - s.sv.scroll
            if ly + FH > y and ly < y + out_h then
                g.text(x+8, ly, s.lines[i], fg, tbg)
            end
        end

        s.sv:draw_scrollbar(x, y, w, out_h)

        -- Prompt
        g.rect(x, y+h-28, w, 28, g.rgb(12,12,20))
        g.hline(x, y+h-28, w, g.rgb(40,80,40))
        g.text(x+4, y+h-22, ">", fg, g.rgb(12,12,20))
        s.input:draw(x+14, y+h-26, w-20, 24)
    end,

    on_key = function(s, key)
        s.input:handle_key(key)
    end,

    on_click = function(s, rx, ry, btn)
        local h = s._h or 380
        if ry >= h-30 then
            s.input:handle_click(rx - 14)
        else
            s.sv:handle_click(ry, h-30)
        end
    end,

    on_mouse_move = function(s, rx, ry)
        s.input:handle_mouse_move(rx - 14)
    end,

    on_mouse_up = function(s)
        s.input:handle_mouse_up()
    end,
}
