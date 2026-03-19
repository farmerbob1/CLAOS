-- CLAOS 3D Engine Demo App
-- Uses per-frame scancode polling for smooth movement
local g = claos.gui
local g3 = claos.gui3d
local FW, FH = 8, 16

-- Fixed-point helpers
local FP = g3.FP_ONE or 65536
local function fp(x) return math.floor(x * FP) end
-- Force float to avoid 32-bit integer overflow (Lua uses 32-bit int on i686)
local function fpmul(a, b) return math.floor((a + 0.0) * b / FP) end

return {
    name = "3d", title = "3D Engine", icon = "3",
    w = 640, h = 480, min_w = 320, min_h = 240,

    on_open = function(s)
        g3.init()

        s.px = fp(0)
        s.py = fp(0)
        s.pz = fp(41)
        s.pa = 0

        local level_path = (s.params and s.params.level) or "/games/demo/e1m1.bsp"
        s.has_level = g3.load_level(level_path)

        s.move_speed = fp(6)
        s.turn_speed = 48
        s.show_stats = true
    end,

    on_draw = function(s, x, y, w, h)
        -- Poll scancodes for movement every frame
        local sin_a = g3.sin(s.pa)
        local cos_a = g3.cos(s.pa)
        local spd = s.move_speed
        local kw = claos.key_pressed(g.SC_W)
        local ks = claos.key_pressed(g.SC_S)
        local ka = claos.key_pressed(g.SC_A)
        local kd = claos.key_pressed(g.SC_D)
        local kq = claos.key_pressed(g.SC_Q) or claos.key_pressed(g.SC_LEFT)
        local ke = claos.key_pressed(g.SC_E) or claos.key_pressed(g.SC_RIGHT)

        -- Compute desired new position
        local nx, ny = s.px, s.py
        -- Forward/back: direction = (-sin(a), cos(a))
        if kw then
            nx = nx + fpmul(-sin_a, spd)
            ny = ny + fpmul(cos_a, spd)
        end
        if ks then
            nx = nx + fpmul(sin_a, spd)
            ny = ny + fpmul(-cos_a, spd)
        end
        -- Strafe: right = (cos(a), sin(a))
        if ka then
            nx = nx + fpmul(-cos_a, spd)
            ny = ny + fpmul(-sin_a, spd)
        end
        if kd then
            nx = nx + fpmul(cos_a, spd)
            ny = ny + fpmul(sin_a, spd)
        end
        -- Apply collision (wall sliding)
        if nx ~= s.px or ny ~= s.py then
            s.px, s.py = g3.move(s.px, s.py, nx, ny, fp(16))
        end
        -- Turn
        if kq then s.pa = (s.pa + s.turn_speed) % 4096 end
        if ke then s.pa = (s.pa - s.turn_speed) % 4096 end

        -- Render
        g3.set_viewport(x, y, w, h)
        g3.set_camera(s.px, s.py, s.pz, s.pa)
        g3.render()

        -- HUD
        if s.show_stats then
            local stats = g3.stats()
            g.text(x + 4, y + 4,
                string.format("FPS:%d  Walls:%d", stats.fps or 0, stats.walls_drawn or 0),
                g.GREEN, 0)
            g.text(x + 4, y + 4 + FH,
                string.format("X:%d Y:%d A:%d",
                    math.floor(s.px / FP), math.floor(s.py / FP), s.pa),
                g.GREY, 0)

            -- Minimap (top-right corner, 120x120 pixels)
            local mw, mh = 120, 120
            local mx = x + w - mw - 4
            local my = y + 4
            local scale = 0.1  -- 1 world unit = 0.1 pixels (map is ~1024 wide)
            local cpx = s.px / FP  -- camera world pos
            local cpy = s.py / FP

            -- Minimap background
            g.rect(mx, my, mw, mh, g.rgb(0, 0, 40))
            g.rect_outline(mx, my, mw, mh, g.rgb(60, 60, 120))

            -- Draw map walls (hardcoded from demo.json vertices)
            local verts = {
                {-128,-128}, {128,-128}, {128,128}, {-128,128},  -- 0-3 center
                {-128,-384}, {128,-384},                          -- 4-5
                {384,-128}, {384,128},                            -- 6-7
                {-128,384}, {128,384},                            -- 8-9
                {-384,-128}, {-384,128},                          -- 10-11
                {-128,-512}, {128,-512},                          -- 12-13
                {512,-128}, {512,128},                            -- 14-15
                {-128,512}, {128,512},                            -- 16-17
                {-512,-128}, {-512,128},                          -- 18-19
            }
            local walls = {
                {5,6},{6,2},{1,5}, {13,14},{14,6},{5,13},  -- south
                {2,7},{7,8},{8,3}, {7,15},{15,16},{16,8},  -- east
                {3,10},{10,9},{9,4}, {10,18},{18,17},{17,9}, -- north
                {1,11},{11,12},{12,4}, {11,19},{19,20},{20,12}, -- west
            }
            local wc = g.rgb(100, 180, 100)
            local mcx = mx + mw // 2
            local mcy = my + mh // 2
            for _, wall in ipairs(walls) do
                local a, b = verts[wall[1]], verts[wall[2]]
                if a and b then
                    local ax = mcx + math.floor((a[1] - cpx) * scale)
                    local ay = mcy + math.floor((a[2] - cpy) * scale)
                    local bx = mcx + math.floor((b[1] - cpx) * scale)
                    local by = mcy + math.floor((b[2] - cpy) * scale)
                    -- Clamp to minimap bounds
                    if ax >= mx and ax < mx+mw and ay >= my and ay < my+mh and
                       bx >= mx and bx < mx+mw and by >= my and by < my+mh then
                        g.line(ax, ay, bx, by, wc)
                    end
                end
            end

            -- Player dot and direction
            g.circle_filled(mcx, mcy, 2, g.rgb(255, 255, 0))
            -- Direction line
            local dir_sin = g3.sin(s.pa)
            local dir_cos = g3.cos(s.pa)
            local dl = 8
            local dx2 = mcx + math.floor(fpmul(-dir_sin, dl * FP) / FP)
            local dy2 = mcy + math.floor(fpmul(dir_cos, dl * FP) / FP)
            g.line(mcx, mcy, dx2, dy2, g.rgb(255, 0, 0))
        end

        g.text(x + 4, y + h - FH - 4, "WASD:move  QE:turn  F:stats", g.GREY, 0)
    end,

    on_key = function(s, key)
        if key == string.byte("f") or key == string.byte("F") then
            s.show_stats = not s.show_stats
        end
    end,

    on_close = function(s)
        g3.unload_level()
    end,
}
