-- CLAOS GUI Theme (v2 — Light/Friendly)
-- Colors from claos_desktop_gui_v2_human_friendly.html

local g = claos.gui

local theme = {
    -- Background
    bg_primary    = g.rgb(244, 241, 236),  -- #f4f1ec warm cream
    bg_white      = g.rgb(255, 255, 255),
    bg_panel      = g.rgb(249, 248, 246),  -- #f9f8f6

    -- Accent (Claude purple)
    accent        = g.rgb(127, 119, 221),  -- #7F77DD
    accent_light  = g.rgb(238, 237, 254),  -- #EEEDFE
    accent_dark   = g.rgb(83, 74, 183),    -- #534AB7
    accent_grad1  = g.rgb(175, 169, 236),  -- #AFA9EC
    accent_grad2  = g.rgb(127, 119, 221),  -- #7F77DD

    -- Text
    text_primary  = g.rgb(44, 44, 42),     -- #2C2C2A
    text_secondary= g.rgb(136, 135, 128),  -- #888780
    text_muted    = g.rgb(180, 178, 169),  -- #B4B2A9
    text_dark     = g.rgb(95, 94, 90),     -- #5F5E5A

    -- Status
    green         = g.rgb(99, 153, 34),    -- #639922
    green_bg      = g.rgb(225, 245, 238),  -- #E1F5EE
    green_text    = g.rgb(15, 110, 86),    -- #0F6E56

    -- Borders
    border_light  = g.rgb(230, 230, 225),
    border_subtle = g.rgb(240, 240, 235),

    -- Font
    font_w = g.FONT_W,
    font_h = g.FONT_H,

    -- Layout dimensions
    topbar_h    = 36,
    sidebar_w   = 56,
    rpanel_w    = 200,
}

return theme
