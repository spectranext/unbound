#ifndef __CLIENT_KEY_CONTROLS_H
#define __CLIENT_KEY_CONTROLS_H

#include <stdint.h>

#define ROW_SHIFT_ZXCV 0xFEFE
#define ROW_ASDFG 0xFDFE
#define ROW_QWERT 0xFBFE
#define ROW_12345 0xF7FE
#define ROW_09876 0xEFFE
#define ROW_POIUY 0xDFFE
#define ROW_ENTER_LKJH 0xBFFE
#define ROW_SPACE_SYM_MNB 0x7FFE

#define KEY_BIT_SHIFT (1)
#define KEY_BIT_A (1)
#define KEY_BIT_Q (1)
#define KEY_BIT_1 (1)
#define KEY_BIT_0 (1)
#define KEY_BIT_P (1)
#define KEY_BIT_ENTER (1)
#define KEY_BIT_SPACE (1)

#define KEY_BIT_Z (2)
#define KEY_BIT_S (2)
#define KEY_BIT_W (2)
#define KEY_BIT_2 (2)
#define KEY_BIT_9 (2)
#define KEY_BIT_O (2)
#define KEY_BIT_L (2)
#define KEY_BIT_SYM (2)

#define KEY_BIT_X (4)
#define KEY_BIT_D (4)
#define KEY_BIT_E (4)
#define KEY_BIT_3 (4)
#define KEY_BIT_8 (4)
#define KEY_BIT_I (4)
#define KEY_BIT_K (4)
#define KEY_BIT_M (4)

#define KEY_BIT_C (8)
#define KEY_BIT_F (8)
#define KEY_BIT_R (8)
#define KEY_BIT_4 (8)
#define KEY_BIT_7 (8)
#define KEY_BIT_U (8)
#define KEY_BIT_J (8)
#define KEY_BIT_N (8)

#define KEY_BIT_V (16)
#define KEY_BIT_G (16)
#define KEY_BIT_T (16)
#define KEY_BIT_5 (16)
#define KEY_BIT_6 (16)
#define KEY_BIT_Y (16)
#define KEY_BIT_H (16)
#define KEY_BIT_B (16)

extern uint8_t poll_key_row(uint row) __naked __z88dk_fastcall;

#endif