public _soundfx
extern _screen_border


_soundfx:
    ld a, l
	ld hl,sfxData		;address of sound effects data

	push ix
	push iy

	ld b,0
	ld c,a
	add hl,bc
	add hl,bc
	ld e,(hl)
	inc hl
	ld d,(hl)
	push de
	pop ix				;put it into ix

	;ld a,(23624)		;get border color from BASIC vars to keep it unchanged
	ld a, (_screen_border)
	;rra
	;rra
	;rra
	and 7
	ld (sfxRoutineToneBorder  +1),a
	ld (sfxRoutineNoiseBorder +1),a
	ld (sfxRoutineSampleBorder+1),a


readData:
	ld a,(ix+0)			;read block type
	ld c,(ix+1)			;read duration 1
	ld b,(ix+2)
	ld e,(ix+3)			;read duration 2
	ld d,(ix+4)
	push de
	pop iy

	dec a
	jr z,sfxRoutineTone
	dec a
	jr z,sfxRoutineNoise
	dec a
	jr z,sfxRoutineSample
	pop iy
	pop ix
	ret



;play sample

sfxRoutineSample:
	ex de,hl
sfxRS0:
	ld e,8				;7
	ld d,(hl)			;7
	inc hl				;6
sfxRS1:
	ld a,(ix+5)			;19
sfxRS2:
	dec a				;4
	jr nz,sfxRS2		;7/12
	rl d				;8
	sbc a,a				;4
	and 16				;7
	and 16				;7	dummy
sfxRoutineSampleBorder:
	or 0				;7
	out (254),a			;11
	dec e				;4
	jp nz,sfxRS1		;10=88t
	dec bc				;6
	ld a,b				;4
	or c				;4
	jp nz,sfxRS0		;10=132t

	ld c,6

nextData:
	add ix,bc		;skip to the next block
	jr readData



;generate tone with many parameters

sfxRoutineTone:
	ld e,(ix+5)			;freq
	ld d,(ix+6)
	ld a,(ix+9)			;duty
	ld (sfxRoutineToneDuty+1),a
	ld hl,0

sfxRT0:
	push bc
	push iy
	pop bc
sfxRT1:
	add hl,de			;11
	ld a,h				;4
sfxRoutineToneDuty:
	cp 0				;7
	sbc a,a				;4
	and 16				;7
sfxRoutineToneBorder:
	or 0				;7
	out (254),a			;11
	ld a,(0)			;13	dummy
	dec bc				;6
	ld a,b				;4
	or c				;4
	jp nz,sfxRT1		;10=88t

	ld a,(sfxRoutineToneDuty+1)	 ;duty change
	add a,(ix+10)
	ld (sfxRoutineToneDuty+1),a

	ld c,(ix+7)			;slide
	ld b,(ix+8)
	ex de,hl
	add hl,bc
	ex de,hl

	pop bc
	dec bc
	ld a,b
	or c
	jr nz,sfxRT0

	ld c,11
	jr nextData



;generate noise with two parameters

sfxRoutineNoise:
	ld e,(ix+5)			;pitch

	ld d,1
	ld h,d
	ld l,d
sfxRN0:
	push bc
	push iy
	pop bc
sfxRN1:
	ld a,(hl)			;7
	and 16				;7
sfxRoutineNoiseBorder:
	or 0				;7
	out (254),a			;11
	dec d				;4
	jp z,sfxRN2			;10
	nop					;4	dummy
	jp sfxRN3			;10	dummy
sfxRN2:
	ld d,e				;4
	inc hl				;6
	ld a,h				;4
	and 31				;7
	ld h,a				;4
	ld a,(0)			;13 dummy
sfxRN3:
	nop					;4	dummy
	dec bc				;6
	ld a,b				;4
	or c				;4
	jp nz,sfxRN1		;10=88 or 112t

	ld a,e
	add a,(ix+6)		;slide
	ld e,a

	pop bc
	dec bc
	ld a,b
	or c
	jr nz,sfxRN0

	ld c,7
	jr nextData


sfxData:

SoundEffectsData:
	defw SoundEffect3Data

SoundEffect3Data:
	defb 1 ;tone
	defw 10,400,1000,65336,2688
	defb 0
