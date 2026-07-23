
extern asm_zx_cxy2saddr
extern asm_zx_cxy2aaddr

public _redraw_tile

_redraw_tile:
    pop ix                              ; ret
    pop hl                              ; screen location
    pop bc                              ; b - old character, c - new character
    push ix                             ; store ret back

    push hl                             ; store xy

    call asm_zx_cxy2saddr               ; convert xy at hl into screen address
    ld de, hl                           ; screen address on de

    push de
    ld a, b
    call __redraw_tile                  ; out with the old
    pop de

    ld a, c
    call __redraw_tile                  ; in with the new

    ; hl contains tile color

    ld a, (hl)                          ; new tile color in b
    ld b, a

    pop hl                              ; get xy
    call asm_zx_cxy2aaddr               ; get screen char color location

    ld a, b
    ld (hl), a                          ; update tile color

    ret

__redraw_tile:

    ; input:
    ; de - location on screen
    ; a - tile number

    ld h, 0x10                          ; hl now holds character index
    ld l, a

    rept 8
    ld a, (de)
    xor (hl)
    ld (de), a
    inc h
    inc d
    endr

    ret
