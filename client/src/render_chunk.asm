extern _screen_characters_b
extern asm_zx_cxy2saddr
extern asm_zx_cxy2aaddr

extern render_sp_buffer
extern current_sprite_id
extern render_tile

public _render_chunk

render_chunk_data:
    ld a, h
    ld ixh, a                           ; store chunk location into ix
    ld a, l
    ld ixl, a

    ld a, h                             ; store chunk "limits" into iy
    add a, 8
    ld iyh, a
    ld a, l
    add a, 8
    ld iyl, a

    ld a, ixh
    ld h, a                             ; start from lower y

chunk_loop_y:
    ld a, ixl
    ld l, a                             ; each row start from lower x

chunk_loop_x:

    push hl                             ; preserve coords and characters offsets
    push de
    push bc

    ld bc, de                           ; get character index into bc

    call render_tile

    pop bc
    pop de
    pop hl

    inc de                              ; onto next character
    inc de                              ; twice

    inc l                               ; while x < 8, loop over
    ld a, l
    cp iyl
    jp nz, chunk_loop_x

    push hl
    ld h, 0
    ld l, 64 - 16
    add hl, de
    ld de, hl                           ; skip character offsets onto next chunk line
    pop hl

    inc h                               ; while y < 8, loop over
    ld a, h
    cp iyh
    jp nz, chunk_loop_y
    ret

_render_chunk:
    ; refresh one chunk (8x8)
    ; hl - chunk location, h - y, l - x

    ld ixh, 0
    ld a, h
    ld ixl, a

    add ix, ix
    add ix, ix
    add ix, ix
    add ix, ix
    add ix, ix                          ; multiply ix by 32

    ld b, 0
    ld c, l
    add ix, bc                          ; ix now contains character offset

    push ix                             ; store character offset for recoloring

    add ix, ix                          ; multiple ix by two, because two bytes per char

    ld de, _screen_characters_b
    add ix, de
    ld de, ix                           ; store that into de

    push de                             ; store character offset position on _screen_characters for recoloring

    call render_chunk_data              ; render the data

    pop de                              ; restore character offset on _screen_characters
    pop ix                              ; restore character offset only

___xxx:

    ld bc, $5800
    add ix, bc                          ; ix now contains screen address

    ld b, 8

color_loop_y:
    ld c, 8

color_loop_x:
    ld a, (de)                          ; get current char into a
    ld h, 0x18                          ; locate the attribute info
    ld l, a
    ld a, (hl)                          ; get current color into hl
    ld (ix), a                          ; put that color onto the screen

color_loop_skip:
    inc ix
    inc de
    inc de
    dec c
    jp nz, color_loop_x                 ; loop 8 times

    ld h, 0                             ; offset de by (64 - 16)
    ld l, 64 - 16
    add hl, de
    ld de, hl

    push bc
    ld b, 0                             ; offset ix by (32 - 8)
    ld c, 32 - 8
    add ix, bc
    pop bc

    dec b
    jp nz, color_loop_y                 ; loop 8 times

    ret
