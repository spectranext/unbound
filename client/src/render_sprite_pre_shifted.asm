public _render_sprite_pre_shifted
extern asm_zx_cxy2saddr

_render_xy:
    defw 0

_render_sprite_pre_shifted:
    ; stack arguments:
    ; - xy coordinates
    ; - xy offset in pixels
    ; - pointer to pre-shifted sprite data (3x2)

    pop iy                              ; store ret address
    pop hl                              ; get xy coordinates
    ld (_render_xy), hl                 ; store it for now
    pop bc                              ; get xy offset in pixels

    call asm_zx_cxy2saddr               ; convert xy to screen address
    ld de, hl                           ; load it into de
    ld a, b                             ; shift it vertically
    add d
    ld d, a

    pop hl                              ; get pointer to sprite data

    ld a, c
    add a, c
    add a, c
    add a, l
    ld l, a                             ; shifting horizontally is just 3 x shift of sprite data

    ; current pointers:
    ; de - target screen location
    ; hl - source buffer pointer to shifted data

    ld b, 16
render_sprite_tile_loop:

    ld a, (de)                          ; bake
    xor (hl)
    ld (de), a
    inc l
    inc e

    ld a, (de)
    xor (hl)                            ; a
    ld (de), a
    inc l
    inc e

    ld a, (de)
    xor (hl)                            ; column (3 bytes)
    ld (de), a
    dec l
    dec l
    dec e
    dec e

    inc d                               ; next row
    inc h

    ld a, 00000111b
    and d
    jr nz, skip_next_row                ; skip jump over if we're not on the 0th row

    push hl

    ld hl, (_render_xy)                 ; next row
    inc h
    ld (_render_xy), hl

    call asm_zx_cxy2saddr               ; convert xy to screen address
    ld de, hl

    pop hl

skip_next_row:
    djnz render_sprite_tile_loop        ; bake one more time

    push iy
    ret
