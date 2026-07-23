;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Stefan Bylund 2016
;;
;; Routines for calling the Vortex Tracker II (VT) player in PT3PROM.asm.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION code_user

EXTERN VT_START

DEFC VT_INIT = VT_START + 3
DEFC VT_PLAY = VT_START + 5
DEFC VT_MUTE = VT_START + 8
DEFC VT_SETUP_BYTE = VT_START + 10
DEFC VT_CUR_POS_WORD = VT_START + 11

EXTERN SETUP

PUBLIC _vt_setup_byte
DEFC _vt_setup_byte = SETUP

PUBLIC _vt_init
PUBLIC _vt_play
PUBLIC _vt_mute
PUBLIC _vt_get_cur_pos

_vt_init:
    ; hl contains module address
    di
    push ix
    push iy
    call VT_INIT
    pop iy
    pop ix
    ei
    ret

DEFC _vt_play = VT_PLAY

_vt_mute:
    di
    call VT_MUTE
    ei
    ret

_vt_get_cur_pos:
    ld hl,VT_CUR_POS_WORD
    ret

SECTION data_user
