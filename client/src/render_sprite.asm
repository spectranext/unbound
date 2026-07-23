public _render_sprite
extern asm_zx_cxy2saddr

_render_xy:
    defw 0

_render_sprite:
    ; stack arguments:
    ; - xy coordinates
    ; - xy offset in pixels
    ; - pointer to sprite data (2x2)

    pop iy                              ; store ret address
    pop hl                              ; get xy coordinates
    ld (_render_xy), hl                 ; store it for now
    pop bc                              ; get xy offset in pixels

    call asm_zx_cxy2saddr               ; convert xy to screen address
    ld ix, hl                           ; load it into ix

    ld a, b                             ; shift it vertically
    add ixh
    ld ixh, a

    pop hl                              ; get pointer to sprite data
    ld a, 16                            ; we need to count 16 rows (two rows of 8 row characters)

render_sprite_tile_loop:
    ex af, af'

    ld d, (hl)                          ; d - tile 0
    inc hl
    ld e, (hl)                          ; e - tile 1
    inc hl
    ld b, 0                             ; b - tile 2

    ld a, c
    cp 0
    jp z, render_sprite_tile_shift_done ; skip if no shifts at all

render_sprite_tile_sh_once:
    srl d                               ; shift right [deb]
    rr e
    rr b
    dec a
    jp nz, render_sprite_tile_sh_once   ; loop C times

render_sprite_tile_shift_done:
    ld a, d
    xor (ix)
    ld (ix), a                          ; render shifted 16-bit row onto a target buffer

    ld a, e
    xor (ix + 1)
    ld (ix + 1), a

    ld a, b
    xor (ix + 2)
    ld (ix + 2), a

    inc ixh                             ; onto next row

    ld a, 00000111b
    and ixh
    jr nz, skip_next_row                ; skip jump over if we're not on the 0th row

    push hl

    ld hl, (_render_xy)                 ; next row
    inc h
    ld (_render_xy), hl

    call asm_zx_cxy2saddr               ; convert xy to screen address
    ld ix, hl

    pop hl

skip_next_row:

    ex af, af'
    dec a
    jp nz, render_sprite_tile_loop      ; loop 8 times

    push iy
    ret
