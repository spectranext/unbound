
extern asm_zx_cxy2saddr
extern asm_zx_cxy2aaddr

public render_tile

render_tile:
    ; render one tile onto a screen
    ; bc - address of screen character
    ; hl - xy on screen to draw it
    ; affected: hl, bc, de

    ld a, (bc)                          ; prepare character
    inc bc                              ; switch to sprite character
    cp 0
    jp z, __render_tile_nobg            ; we have no background, much simpler

    push af
    call asm_zx_cxy2saddr               ; convert xy at hl into screen address
    ld de, hl                           ; de now holds screen address
    pop af

    ld h, 0x10                          ; hl now holds character index
    ld l, a

    rept 8
    ld a, (hl)
    ld (de), a
    inc h
    inc d
    endr

    ret

__render_tile_nobg:

    call asm_zx_cxy2saddr               ; convert xy at hl into screen address

    ; render nothing
    ld a, 0

    rept 8
    ld (hl), a
    inc h
    endr

    ret
