-- CLAOS GUI Desktop
local g = claos.gui
if not g.active() then
    if not g.activate() then print("ERROR: No VESA") return end
end
local W,H = g.width(),g.height()
local FW,FH = 8,16

-- Theme
local dk = false
local function mkT(d)
    if d then return {
        bg=g.rgb(18,18,28),wh=g.rgb(30,30,42),pn=g.rgb(24,24,36),
        ac=g.rgb(127,119,221),al=g.rgb(50,46,80),ad=g.rgb(170,165,240),
        t1=g.rgb(210,208,220),t2=g.rgb(140,138,150),t3=g.rgb(100,98,110),
        gn=g.rgb(99,200,80),bd=g.rgb(45,45,60),hv=g.rgb(40,40,55),
        ib=g.rgb(35,35,50),wb=g.rgb(28,28,40),rd=g.rgb(220,80,80),
    } else return {
        bg=g.rgb(244,241,236),wh=g.rgb(255,255,255),pn=g.rgb(249,248,246),
        ac=g.rgb(127,119,221),al=g.rgb(238,237,254),ad=g.rgb(83,74,183),
        t1=g.rgb(44,44,42),t2=g.rgb(136,135,128),t3=g.rgb(180,178,169),
        gn=g.rgb(99,153,34),bd=g.rgb(225,225,220),hv=g.rgb(235,233,228),
        ib=g.rgb(244,241,236),wb=g.rgb(255,255,255),rd=g.rgb(220,80,80),
    } end
end
local T = mkT(false)
local TH,SW,RP = 36,56,200

-- Helpers
local function hit(x,y,w,h,mx,my) return mx>=x and mx<x+w and my>=y and my<y+h end
local last_click_time = 0
local last_click_item = -1
local function is_dblclick(idx)
    local now = claos.uptime()
    local dbl = (idx == last_click_item and (now - last_click_time) < 1)
    last_click_time = now
    last_click_item = idx
    return dbl
end
local function wrap(s,mc)
    local r={} local p=1
    while p<=#s do
        if #s-p+1<=mc then r[#r+1]=s:sub(p); break end
        local c=s:sub(p,p+mc-1) local sp
        for i=#c,1,-1 do if c:sub(i,i)==" " then sp=i break end end
        if sp then r[#r+1]=c:sub(1,sp-1); p=p+sp
        else r[#r+1]=c; p=p+mc end
    end; return r
end

-- State
local view = "chat"
local msgs = {{f="c",t="Welcome to CLAOS! I'm running natively inside your OS. What would you like to do?"}}
local wait = false
local cscr = 0
local inp = ""
local lmx,lmy = W/2,H/2

-- Topbar
local function dTop()
    g.rect(0,0,W,TH,T.wh); g.hline(0,TH-1,W,T.bd)
    g.rounded_rect(8,8,20,20,6,T.ac); g.text(11,10,"C",T.wh,T.ac)
    g.text(34,12,"CLAOS",T.t1,T.wh); g.text(82,12,"v0.8",T.t3,T.wh)
    local rx=W-200
    g.circle_filled(rx,18,3,T.gn); g.text(rx+8,12,"Online",T.t2,T.wh)
    local up=claos.uptime()
    g.text(rx+80,12,string.format("%02d:%02d",math.floor(up/60),up%60),T.t2,T.wh)
end

-- Sidebar
local sideI = {{"C","chat"},{"T","term"},{"M","mon"},{"F","files"}}
local function dSide()
    g.rect(0,TH,SW,H-TH,T.pn); g.vline(SW-1,TH,H-TH,T.bd)
    for i,s in ipairs(sideI) do
        local y=TH+12+(i-1)*42
        local a=s[2]==view
        local bg=a and T.ac or T.bg; local fg=a and T.wh or T.t2
        g.rounded_rect(9,y,38,38,10,bg); g.text(24,y+11,s[1],fg,bg)
    end
    -- Settings/theme toggle
    g.rounded_rect(9,H-50,38,38,10,T.bg)
    g.text(21,H-39,dk and "L" or "D",T.t2,T.bg)
end

-- Right panel
local function dRight()
    local px=W-RP
    g.rect(px,TH,RP,H-TH,T.pn); g.vline(px,TH,H-TH,T.bd)
    local x,y=px+12,TH+16
    g.text(x,y,"CLAUDE",T.t3,T.pn); y=y+20
    g.rounded_rect(x,y,RP-24,36,10,T.wh)
    g.circle_filled(x+14,y+18,4,wait and T.ac or T.gn)
    g.text(x+24,y+10,wait and "Thinking" or "Active",T.t1,T.wh); y=y+48
    g.text(x,y,"VIEW: "..view:upper(),T.t3,T.pn); y=y+24
    g.text(x,y,#msgs.." messages",T.t3,T.pn)
    y=H-50
    g.rounded_rect(x,y,RP-24,40,10,T.al)
    g.text(x+16,y+12,"CLAOS v0.8",T.ad,T.al)
end

-- Chat view
local function dChat()
    local cx,cy,cw,ch=SW,TH,W-SW-RP,H-TH
    g.rect(cx,cy,cw,ch,T.wb)
    -- Header
    g.hline(cx,cy+44,cw,T.bd)
    g.circle_filled(cx+24,cy+22,12,T.al)
    g.text(cx+20,cy+16,"C",T.ac,T.al)
    g.text(cx+44,cy+10,"Claude",T.t1,T.wb)
    g.text(cx+44,cy+26,"Connected",T.gn,T.wb)
    -- Messages
    local mc=math.floor((cw-80)/FW)
    local my=cy+50-cscr
    for _,m in ipairs(msgs) do
        local ls=wrap(m.t,mc); local lh=#ls*FH+12
        if my+lh>cy+44 and my<cy+ch-52 then
            if m.f=="c" then
                g.rounded_rect(cx+12,my,cw-24,lh,8,T.pn)
                for j,l in ipairs(ls) do
                    local ly=my+6+(j-1)*FH
                    if ly>cy+44 and ly<cy+ch-52 then g.text(cx+20,ly,l,T.t1,T.pn) end
                end
            else
                local mw=0; for _,l in ipairs(ls) do if #l*FW>mw then mw=#l*FW end end; mw=mw+20
                g.rounded_rect(cx+cw-mw-12,my,mw,lh,8,T.ac)
                for j,l in ipairs(ls) do
                    local ly=my+6+(j-1)*FH
                    if ly>cy+44 and ly<cy+ch-52 then g.text(cx+cw-mw-2,ly,l,T.wh,T.ac) end
                end
            end
        end
        my=my+lh+8
    end
    if wait then
        g.rounded_rect(cx+12,my,100,28,8,T.pn)
        g.text(cx+20,my+6,"Thinking...",T.t3,T.pn)
    end
    -- Input
    g.hline(cx,cy+ch-50,cw,T.bd)
    g.rect(cx,cy+ch-49,cw,49,T.wb)
    g.rounded_rect(cx+12,cy+ch-42,cw-60,32,12,T.ib)
    if #inp>0 then
        local mc2=math.floor((cw-84)/FW)
        local vi=inp; if #vi>mc2 then vi=vi:sub(#vi-mc2+1) end
        g.text(cx+24,cy+ch-34,vi,T.t1,T.ib)
        g.vline(cx+24+#vi*FW,cy+ch-40,20,T.ac)
    else
        g.text(cx+24,cy+ch-34,"Ask Claude anything...",T.t3,T.ib)
        g.vline(cx+24,cy+ch-40,20,T.ac)
    end
    g.rounded_rect(cx+cw-42,cy+ch-42,32,32,10,T.ac)
    g.text(cx+cw-34,cy+ch-34,">",T.wh,T.ac)
end

-- Terminal view
local tl={"CLAOS Terminal v0.8","Type commands. ESC=desktop",""}
local ti=""
local function dTerm()
    local cx,cy,cw,ch=SW,TH,W-SW-RP,H-TH
    local bg=g.rgb(20,20,30); local fg=g.rgb(100,220,100)
    g.rect(cx,cy,cw,ch,bg)
    local ml=math.floor((ch-32)/FH)
    local st=math.max(1,#tl-ml+1)
    for i=st,#tl do
        g.text(cx+8,cy+4+(i-st)*FH,tl[i],fg,bg)
    end
    g.text(cx+8,cy+ch-24,"claos> "..ti,fg,bg)
    g.vline(cx+8+(7+#ti)*FW,cy+ch-28,20,fg)
end

-- System monitor
local function dMon()
    local cx,cy,cw,ch=SW+8,TH+8,W-SW-RP-16,H-TH-16
    g.rect(SW,TH,W-SW-RP,H-TH,T.wb)
    local y=cy
    g.text(cx,y,"SYSTEM MONITOR",T.t1,T.wb); y=y+28
    -- Memory
    g.rounded_rect(cx,y,cw,60,10,T.pn)
    g.text(cx+12,y+8,"MEMORY",T.t3,T.pn)
    local mt=claos.mem_total(); local mf=claos.mem_free()
    local u=mt-(mf*4/1024); local p=math.floor(u/mt*100)
    g.text(cx+12,y+28,string.format("%dMB / %dMB (%d%%)",math.floor(u),mt,p),T.t1,T.pn)
    g.rounded_rect(cx+12,y+46,cw-24,8,3,T.bd)
    local bw=math.floor((cw-28)*p/100)
    if bw>0 then g.rounded_rect(cx+14,y+47,bw,6,2,T.ac) end
    y=y+72
    -- Uptime
    g.rounded_rect(cx,y,cw,44,10,T.pn)
    g.text(cx+12,y+8,"UPTIME",T.t3,T.pn)
    local up=claos.uptime()
    g.text(cx+12,y+24,string.format("%02d:%02d:%02d  PIT@100Hz",math.floor(up/3600),math.floor(up%3600/60),up%60),T.t1,T.pn)
    y=y+56
    -- Network
    g.rounded_rect(cx,y,cw,44,10,T.pn)
    g.text(cx+12,y+8,"NETWORK",T.t3,T.pn)
    g.circle_filled(cx+18,y+32,4,T.gn)
    g.text(cx+28,y+24,"10.0.2.15 via e1000",T.t1,T.pn)
end

-- Files view
-- File browser state
local fpath = "/"
local fsel = -1  -- selected index
local fitems = {} -- current directory listing

-- Get direct children of a path
local function fchildren(path)
    local all = claos.ls(path)
    if not all then return {} end
    local out = {}
    local pfx = path
    if pfx ~= "/" then pfx = pfx .. "/" end
    for _,f in ipairs(all) do
        local nm = f.name
        -- Check if this is a direct child (not nested deeper)
        if #nm > #pfx then
            local rest = nm:sub(#pfx + 1)
            if not rest:find("/") then
                -- Direct child — extract just the filename
                out[#out+1] = {name=rest, full=nm, size=f.size, is_dir=f.is_dir}
            end
        end
    end
    return out
end

-- Notepad app state
local note_file = nil   -- path of open file
local note_lines = {}   -- wrapped lines
local note_scroll = 0
local note_title = ""

local function open_file(path, name)
    local content = claos.read(path)
    if not content then return end
    note_file = path
    note_title = name
    note_lines = {}
    -- Split into lines, then wrap each
    local mc = math.floor((W-SW-RP-40)/FW)
    for line in (content.."\n"):gmatch("([^\n]*)\n") do
        if #line == 0 then
            note_lines[#note_lines+1] = ""
        else
            local wl = wrap(line, mc)
            for _,wl2 in ipairs(wl) do note_lines[#note_lines+1] = wl2 end
        end
    end
    note_scroll = 0
    view = "note"
end

local function dNote()
    local cx,cy,cw,ch = SW,TH,W-SW-RP,H-TH
    g.rect(cx,cy,cw,ch,T.wb)
    -- Title bar
    g.rect(cx,cy,cw,32,T.pn)
    g.hline(cx,cy+31,cw,T.bd)
    -- Back button
    g.rounded_rect(cx+8,cy+4,28,24,6,T.ib)
    g.text(cx+14,cy+8,"<",T.t1,T.ib)
    -- File name
    g.text(cx+44,cy+8,note_title,T.t1,T.pn)
    -- Line count
    g.text(cx+cw-100,cy+8,#note_lines.." lines",T.t3,T.pn)

    -- Content area
    local top = cy + 36
    local bot = cy + ch
    local visible = math.floor((bot - top) / FH)
    local max_scroll = math.max(0, #note_lines - visible)
    if note_scroll > max_scroll then note_scroll = max_scroll end
    if note_scroll < 0 then note_scroll = 0 end

    -- Line numbers + content
    for i = 1, visible do
        local li = i + note_scroll
        if li > #note_lines then break end
        local ly = top + (i-1) * FH
        -- Line number
        g.text(cx+4, ly, string.format("%3d", li), T.t3, T.wb)
        -- Content
        g.text(cx+36, ly, note_lines[li], T.t1, T.wb)
    end

    -- Scrollbar
    if #note_lines > visible then
        local sb_h = math.max(20, math.floor(visible / #note_lines * (bot-top)))
        local sb_y = top + math.floor(note_scroll / max_scroll * (bot-top-sb_h))
        g.rounded_rect(cx+cw-8, sb_y, 6, sb_h, 3, T.bd)
    end
end

local function dFiles()
    local cx,cy,cw,ch=SW,TH,W-SW-RP,H-TH
    g.rect(cx,cy,cw,ch,T.wb)

    -- Title bar with back button
    if fpath ~= "/" then
        g.rounded_rect(cx+8,cy+6,28,24,6,T.ib)
        g.text(cx+14,cy+10,"<",T.t1,T.ib)
        g.text(cx+44,cy+8,"FILE BROWSER",T.t1,T.wb)
    else
        g.text(cx+12,cy+8,"FILE BROWSER",T.t1,T.wb)
    end

    -- Path bar
    g.rounded_rect(cx+12,cy+32,cw-24,24,8,T.ib)
    local dp = fpath
    if #dp > math.floor((cw-40)/FW) then dp = "..."..dp:sub(-math.floor((cw-48)/FW)) end
    g.text(cx+20,cy+36,dp,T.t1,T.ib)
    g.hline(cx,cy+62,cw,T.bd)

    -- File listing
    fitems = fchildren(fpath)
    local y = cy + 68
    for i,f in ipairs(fitems) do
        if y + 32 > cy + ch then break end
        -- Selection highlight
        local row_bg = (i == fsel) and T.al or T.wb
        g.rect(cx+4,y,cw-8,30,row_bg)
        -- Icon
        if f.is_dir then
            g.rounded_rect(cx+12,y+3,24,24,6,T.al)
            g.text(cx+17,y+7,"D",T.ac,T.al)
        else
            g.rounded_rect(cx+12,y+3,24,24,6,T.pn)
            g.text(cx+17,y+7,"F",T.t3,T.pn)
        end
        -- Name
        g.text(cx+44,y+7,f.name,T.t1,row_bg)
        -- Size or "Folder"
        if f.is_dir then
            g.text(cx+cw-72,y+7,"Folder",T.t3,row_bg)
        else
            local sz=f.size
            local s
            if sz>=1048576 then s=string.format("%.1f MB",sz/1048576)
            elseif sz>=1024 then s=string.format("%.1f KB",sz/1024)
            else s=sz.." B" end
            g.text(cx+cw-72,y+7,s,T.t3,row_bg)
        end
        g.hline(cx+12,y+30,cw-24,T.bd)
        y = y + 34
    end

    -- Empty folder message
    if #fitems == 0 then
        g.text(cx+20,cy+80,"Empty folder",T.t3,T.wb)
    end
end

-- Mouse cursor
local function dCur(mx,my)
    for dy=0,11 do
        local w=(dy<8) and (dy+1) or (12-dy)
        if w>0 then
            g.hline(mx,my+dy,1,T.t1)
            if w>2 then g.hline(mx+1,my+dy,w-2,T.wh) end
            if w>1 then g.pixel(mx+w-1,my+dy,T.t1) end
        end
    end
end

-- Draw all
local function dAll()
    g.clear(T.bg); dTop(); dSide()
    if view=="chat" then dChat()
    elseif view=="term" then dTerm()
    elseif view=="mon" then dMon()
    elseif view=="files" then dFiles()
    elseif view=="note" then dNote() end
    dRight()
end

-- Event loop
dAll(); dCur(lmx,lmy); g.swap()
local run=true; local fr=0

while run do
    local dirty=false
    while true do
        local ev=g.poll_event()
        if not ev then break end
        if ev.type==g.KEY_DOWN then
            if ev.key==27 then run=false break end
            if view=="chat" then
                if ev.key==8 then
                    if #inp>0 then inp=inp:sub(1,-2) end
                elseif ev.key==10 or ev.key==13 then
                    if #inp>0 then
                        msgs[#msgs+1]={f="u",t=inp}; inp=""; wait=true; cscr=99999
                    end
                elseif ev.key>=32 and ev.key<127 then
                    inp=inp..string.char(ev.key)
                end
                dirty=true
            elseif view=="term" then
                if ev.key==8 then
                    if #ti>0 then ti=ti:sub(1,-2) end
                elseif ev.key==10 or ev.key==13 then
                    if #ti>0 then
                        tl[#tl+1]="claos> "..ti
                        local cmd=ti; ti=""
                        if cmd=="help" then tl[#tl+1]="Commands: help sysinfo uptime ls clear"
                        elseif cmd=="clear" then tl={""}
                        elseif cmd=="sysinfo" then
                            tl[#tl+1]="Mem: "..claos.mem_total().."MB, "..claos.mem_free().." pages free"
                            tl[#tl+1]="Up: "..claos.uptime().."s"
                        elseif cmd=="uptime" then tl[#tl+1]=claos.uptime().."s"
                        elseif cmd=="ls" then
                            local fs=claos.ls("/")
                            if fs then for _,f in ipairs(fs) do
                                tl[#tl+1]=(f.is_dir and "d " or "  ")..f.name
                            end end
                        else
                            tl[#tl+1]="Asking Claude..."; dirty=true
                            dAll(); dCur(lmx,lmy); g.swap()
                            local r=claos.ask(cmd)
                            local wl=wrap(r or "(no response)",math.floor((W-SW-RP-20)/FW))
                            for _,l in ipairs(wl) do tl[#tl+1]=l end
                        end
                        tl[#tl+1]=""
                    end
                elseif ev.key>=32 and ev.key<127 then
                    ti=ti..string.char(ev.key)
                end
                dirty=true
            elseif view=="note" then
                -- Scroll notepad with - and + or space
                if ev.key==45 then note_scroll=note_scroll-5; dirty=true  -- minus
                elseif ev.key==43 or ev.key==32 then note_scroll=note_scroll+5; dirty=true end -- plus/space
            end
        elseif ev.type==g.MOUSE_MOVE then
            lmx,lmy=ev.x,ev.y; dirty=true
        elseif ev.type==g.MOUSE_DOWN then
            -- Sidebar clicks
            for i,s in ipairs(sideI) do
                if hit(9,TH+12+(i-1)*42,38,38,lmx,lmy) then
                    view=s[2]; dirty=true
                end
            end
            -- Theme toggle
            if hit(9,H-50,38,38,lmx,lmy) then
                dk=not dk; T=mkT(dk); dirty=true
            end
            -- File browser clicks
            if view=="files" then
                local cx,cy=SW,TH
                local cw=W-SW-RP
                -- Back button
                if fpath~="/" and hit(cx+8,cy+6,28,24,lmx,lmy) then
                    local p=fpath:match("(.+)/[^/]+$") or "/"
                    fpath=p; fsel=-1; dirty=true
                end
                -- File/folder rows
                local ry=cy+68
                for i,f in ipairs(fitems) do
                    if hit(cx+4,ry,cw-8,30,lmx,lmy) then
                        local dbl = is_dblclick(i)
                        if dbl then
                            -- Double click: open folder or file
                            if f.is_dir then
                                fpath=f.full; fsel=-1
                            else
                                open_file(f.full, f.name)
                            end
                        else
                            -- Single click: select
                            fsel=i
                        end
                        dirty=true
                        break
                    end
                    ry=ry+34
                end
            end
            -- Notepad clicks
            if view=="note" then
                local cx,cy=SW,TH
                local cw,ch=W-SW-RP,H-TH
                -- Back button
                if hit(cx+8,cy+4,28,24,lmx,lmy) then
                    view="files"; dirty=true
                -- Scroll: click upper half = up, lower half = down
                elseif hit(cx,cy+36,cw,ch-36,lmx,lmy) then
                    local mid = cy + 36 + (ch-36)/2
                    if lmy < mid then
                        note_scroll = note_scroll - 10
                    else
                        note_scroll = note_scroll + 10
                    end
                    dirty=true
                end
            end
            -- Chat send button
            if view=="chat" then
                local cx=SW; local cw=W-SW-RP
                if hit(cx+cw-42,H-TH+TH-42,32,32,lmx,lmy) then
                    if #inp>0 then
                        msgs[#msgs+1]={f="u",t=inp}; inp=""; wait=true; cscr=99999
                    end
                end
            end
            dirty=true
        end
    end

    -- Claude response
    if wait then
        local last=msgs[#msgs]
        if last.f=="u" then
            local r=claos.ask(last.t)
            msgs[#msgs+1]={f="c",t=r or "(no response)"}
            wait=false; cscr=99999; dirty=true
        end
    end

    fr=fr+1
    if fr%90==0 then dirty=true end
    if dirty then dAll(); dCur(lmx,lmy); g.swap() end
    if not run then break end
    claos.sleep(16)
end
