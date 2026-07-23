public _isr_handler
extern _vt_play_isr
extern _set_unique_frame
extern _module_music_active
extern _module_interrupt_active
extern _module_interrupt
extern _vt_play
extern _render_game
extern _isr_render_enabled
extern _client_map_redraw_my_player_object
extern _client_map_redraw_objects
extern _render_particles

defc SPECTRANET_MAP_PAGE = 0xC6
defc SPECTRANET_OBJECTS_PAGE = 0xC9
defc SPECTRANET_SPRITES_PAGE = 0xCB
defc SPECTRANET_TILES_PAGE = 0xCA
defc SPECTRANET_MODULES_NAMESPACE0 = 0xCC
defc SPECTRANET_MODULES_NAMESPACE1_MUSIC0 = 0xCD
defc SPECTRANET_MODULES_NAMESPACE1_MUSIC1 = 0xCE

public SETPAGEA
public SETPAGEB
public PUSHPAGEA
public POPPAGEA
public PUSHPAGEB
public POPPAGEB

defc SETPAGEA = 0x3E33
defc SETPAGEB = 0x3E36
defc PUSHPAGEA = 0x3E81 ; Pages a page into area A, pushing the old one
defc POPPAGEA = 0x3E84 ; Restores the previous page in area A
defc PUSHPAGEB = 0x3E87 ; Pages a page into area B, pushing the old one
defc POPPAGEB = 0x3E8A ; Restores the previous page in area B

_isr_handler:
    di
    push af
    push bc
    push de
    push hl
    ex af,af'
    exx
    push af
    push bc
    push de
    push hl
    push ix
    push iy

set_unique_frame:
    ld a, 1
    ld (_set_unique_frame), a

check_music:
    ld a, (_module_music_active)
    or a
    jr z, no_music

do_play_music:
    ld a, SPECTRANET_MODULES_NAMESPACE1_MUSIC0
    call PUSHPAGEA
    ld a, SPECTRANET_MODULES_NAMESPACE1_MUSIC1
    call PUSHPAGEB
    call _vt_play
    call POPPAGEB
    call POPPAGEA
no_music:
    ld a, (_isr_render_enabled)
    or a
    jr z, skip_render_game

    ld a, SPECTRANET_OBJECTS_PAGE
    call PUSHPAGEB
    ld a, SPECTRANET_MAP_PAGE
    call PUSHPAGEA

    call _client_map_redraw_objects

    ld a, (_module_interrupt_active)
    cp 0xFF
    jr z, skip_module_interrupt
    add SPECTRANET_MODULES_NAMESPACE0
    call SETPAGEA

    call _module_interrupt

skip_module_interrupt:
    call POPPAGEA
    call POPPAGEB

skip_render_game:

    pop iy
    pop ix
    pop hl
    pop de
    pop bc
    pop af
    exx
    ex af,af'
    pop hl
    pop de
    pop bc
    pop af
    ei
    reti
