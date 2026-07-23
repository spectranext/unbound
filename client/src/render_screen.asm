extern _screen_characters_b
extern _right_panel
extern _sprites_data
extern _sprites_next
extern asm_zx_cxy2saddr
extern asm_zx_cxy2aaddr
extern render_tile

public _render_screen

_render_screen:
    ; refresh whole screen

    ld bc, _screen_characters_b
    ld ixh, 0                           ; line number

__render_screen_line:
    ld a, ixh

    ld h, a
    ld l, 0
    ld ixl, 0

__render_screen_tile:
    push bc
    push hl
    call render_tile
    pop hl
    pop bc

    inc bc                              ; onto next character
    inc bc

    inc l                               ; move on to the next tile on the right

    inc ixl

    ld a, ixl
    cp 32
    jp nz, __render_screen_tile         ; loop until the whole line is drawn

__render_screen_line_done:

    inc ixh
    ld a, ixh
    cp 23
    jp nz, __render_screen_line         ; loop until whole screen is drawn

    ; refresh color, too

    ld de, _screen_characters_b
    ld ix, $5800

    ld bc, 736
__render_screen_color_loop:
    ld a, (de)                          ; get current char into a
    inc de                              ; onto next char
    inc de                              ; skip sprite info

    ld h, 0x18                          ; locate the attribute info
    ld l, a
    ld a, (hl)                          ; get current color into hl

    ld (ix), a                          ; put that color onto the screen
    inc ix

    dec bc
    ld a, b
    or c
    jp nz, __render_screen_color_loop   ; loop 768 times

    ret
