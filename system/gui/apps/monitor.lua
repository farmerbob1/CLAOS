-- CLAOS System Monitor App (uses shared widgets)
local g = claos.gui
local FW, FH = 8, 16

return {
    name = "monitor", title = "System Monitor", icon = "M",
    w = 440, h = 500, min_w = 320, min_h = 400,

    on_open = function(s)
        local C = WIDGETS.colors
        s.cpu_bar = WIDGETS.ProgressBar.new({auto_color=true})
        s.mem_bar = WIDGETS.ProgressBar.new({auto_color=true})
        s.disk_bar = WIDGETS.ProgressBar.new({color=C.ac})
    end,

    on_draw = function(s, x, y, w, h)
        local C = WIDGETS.colors
        local P = WIDGETS.Panel.new({})
        g.rect(x, y, w, h, C.bg)
        local cx, cy = x + 12, y + 12
        local pw = w - 24

        -- CPU
        P:draw(cx, cy, pw, 48)
        g.text_bold(cx+10, cy+8, "CPU", C.t2, C.pn)
        local cpu = claos.cpu()
        s.cpu_bar:set(cpu)
        g.text(cx+10, cy+28, string.format("Load: %d%%", cpu), C.t1, C.pn)
        s.cpu_bar:draw(cx+100, cy+28, pw-120, 10)
        cy = cy + 60

        -- Memory
        P:draw(cx, cy, pw, 68)
        g.text_bold(cx+10, cy+8, "MEMORY", C.t2, C.pn)
        local tb = claos.mem_total(); local fb = claos.mem_free()
        local ub = tb - fb
        local tm = math.floor(tb/1048576); local um = math.floor(ub/1048576); local fm = math.floor(fb/1048576)
        local mp = 0; if tb > 0 then mp = math.floor(ub/tb*100) end
        s.mem_bar:set(mp)
        g.text(cx+10, cy+28, string.format("%dMB used / %dMB total (%d%%)", um, tm, mp), C.t1, C.pn)
        g.text(cx+10, cy+44, string.format("%dMB free", fm), C.t3, C.pn)
        s.mem_bar:draw(cx+10, cy+58, pw-20, 6)
        cy = cy + 80

        -- Disk
        P:draw(cx, cy, pw, 68)
        g.text_bold(cx+10, cy+8, "DISK (ChaosFS)", C.t2, C.pn)
        local dk = claos.disk()
        if dk then
            local dp = 0; if dk.total_kb > 0 then dp = math.floor(dk.used_kb/dk.total_kb*100) end
            s.disk_bar:set(dp)
            g.text(cx+10, cy+28, string.format("%dKB / %dKB (%d%%)", dk.used_kb, dk.total_kb, dp), C.t1, C.pn)
            g.text(cx+10, cy+44, string.format("%d files, %dB blocks", dk.files, dk.block_size), C.t3, C.pn)
            s.disk_bar:draw(cx+10, cy+58, pw-20, 6)
        else
            g.text(cx+10, cy+28, "Not mounted", C.rd, C.pn)
        end
        cy = cy + 80

        -- Uptime
        P:draw(cx, cy, pw, 44)
        g.text_bold(cx+10, cy+8, "UPTIME", C.t2, C.pn)
        local up = claos.uptime()
        g.text(cx+10, cy+24, string.format("%d:%02d:%02d  PIT@100Hz",
            math.floor(up/3600), math.floor(up%3600/60), up%60), C.t1, C.pn)
        cy = cy + 56

        -- Network
        P:draw(cx, cy, pw, 44)
        g.text_bold(cx+10, cy+8, "NETWORK", C.t2, C.pn)
        g.circle_filled(cx+16, cy+32, 4, C.gn)
        g.text(cx+26, cy+24, "10.0.2.15 via e1000", C.t1, C.pn)
        cy = cy + 56

        -- Tasks
        P:draw(cx, cy, pw, 44)
        g.text_bold(cx+10, cy+8, "TASKS", C.t2, C.pn)
        g.text(cx+10, cy+24, "Scheduler active", C.t1, C.pn)
    end,

    on_key = function(s, key) end,
    on_click = function(s, rx, ry, btn) end,
}
