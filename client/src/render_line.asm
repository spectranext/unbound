;
; Title:	ZX Spectrum Vector Output Routines
; Author:	Dean Belfield
; Created:	30/06/2012
; Last Updated:	30/05/2020
;
; Requires:	output
;
; Modinfo:
;
; 03/07/2012:	Simplified line draw; now always draws the line down. Relabled to improve clarity
;		Fixed bug in point tables; one entry had 7 bits
; 04/07/2012:	Added Draw_Horz_Line_Solid & Draw_Horz_Line_Texture; special fast case used for drawing polygons
; 05/07/2012:	Added Draw_Circle routine; needs some optimisation, more a proof of concept at the moment
; 01/04/2020:	Moved Draw_Horz_Line_Solid & Draw_Horz_Line_Texture to vector_filled.z80
; 21/05/2020:	Fixed bug in Draw_Line where final pixel not plotted
; 30/05/2020:	Fixed bug in implementation of Bresenham for Draw_Line and Erase_Line
;

;
; Most of these routines will precalculate the screen address in HL and return a bit position in A (0-7).
; The bit position is used as an index into the table Plot_Point; this contains a pixel set in the correct
; position for each bit, so 0=>%10000000, 1=>%01000000 and so on.
;
; The line routine, for example, will only do this slow calculation once and will work relative to that position
; for the rest of the draw. So, to move the pixel right if the data is stored in register D you would RRC D.
; At this point, if the pixel is %00000010 it would then be %00000001. If the pixel is then rotated again, it would
; be %10000000 and the carry bit would be set. This is picked up in the code and an INC or DEC L is used to move
; HL to the next adjacent character position.
;
; The function Pixel_Address_Down takes HL and move down or up one pixel line, taking into account the
; Spectrum's strange screen layout. Again, this is quicker than calculating the address from scratch each
; time as most of the time it's just doing an INC H (or DEC H).
;
; For the sake of clarity I've used CALLs within loops; I wouldn't normally do this in speed critical code but felt
; that I'd lose clarity if I didn't. Feel free to inline any code that is called.
;
; Finally, this code uses a lot of self-modifying code; the code is in RAM so it is possible to use the code to
; modify itself. This is used in the line routine to adjust the line drawing loop to draw in all four quadrants.
;

; Get screen address
; B = Y pixel position
; C = X pixel position
; Returns address in HL and pixel position within character in A
;
Get_Pixel_Address:	LD A,B				; Calculate Y2,Y1,Y0
			AND %00000111			; Mask out unwanted bits
			OR %01000000			; Set base address of screen
			LD H,A				; Store in H
			LD A,B				; Calculate Y7,Y6
			RRA				; Shift to position
			RRA
			RRA
			AND %00011000			; Mask out unwanted bits
			OR H				; OR with Y2,Y1,Y0
			LD H,A				; Store in H
			LD A,B				; Calculate Y5,Y4,Y3
			RLA				; Shift to position
			RLA
			AND %11100000			; Mask out unwanted bits
			LD L,A				; Store in L
			LD A,C				; Calculate X4,X3,X2,X1,X0
			RRA				; Shift into position
			RRA
			RRA
			AND %00011111			; Mask out unwanted bits
			OR L				; OR with Y5,Y4,Y3
			LD L,A				; Store in L
			LD A,C
			AND 7
			RET

; Move HL down one pixel line
;
Pixel_Address_Down:	INC H				; Go down onto the next pixel line
			LD A,H				; Check if we have gone onto next character boundary
			AND 7
			RET NZ				; No, so skip the next bit
			LD A,L				; Go onto the next character line
			ADD A,32
			LD L,A
			RET C				; Check if we have gone onto next third of screen
			LD A,H				; Yes, so go onto next third
			SUB 8
			LD H,A
			RET

; Move HL up one pixel line
;
Pixel_Address_Up:	DEC H				; Go up onto the next pixel line
			LD A,H				; Check if we have gone onto the next character boundary
			AND 7
			CP 7
			RET NZ
			LD A,L
			SUB 32
			LD L,A
			RET C
			LD A,H
			ADD A,8
			LD H,A
			RET

; Plot routine
; B = Y pixel position
; C = X pixel position
;
Plot:			CALL Get_Pixel_Address		; Get screen address in HL, pixel position (0 to 7) in A
			LD BC,Plot_Point		; Address of point lookup table
			ADD A,C				; Add pixel position to get entry in table
			LD C,A
			LD A,(BC)			; Get pixel data from table
			XOR (HL)			; XOR with screen data
			LD (HL),A			; Write back to screen
			RET

public _render_line

_render_line:
            pop ix
            pop bc
            pop de
            push ix

; Draw Line routine
; B = Y pixel position 1
; C = X pixel position 1
; D = Y pixel position 2
; E = X pixel position 2
;
Draw_Line:		LD A,D				; Check whether we are going to be drawing up
			CP B
			JR NC,Draw_Line_1

			PUSH BC				; If we are, then this neat trick swaps BC and DE
			PUSH DE				; using the stack, forcing the line to be always
			POP BC				; drawn downwards
			POP DE

Draw_Line_1:		CALL Get_Pixel_Address		; Get screen address in HL, pixel position (0-7) in A
;
; At this point we have
;  A = Pixel position (0-7)
; HL = Screen address of the start point
; BC = Start coordinate (B=Y1, C=X1)
; DE = End coordinates  (D=Y2, E=X2)
;
			LD IX,Plot_Point		; Point to the Plot_Point table
			ADD A,IXL			; Add the pixel position to get entry in table
			LD IXL,A

			LD A,D				; Calculate the line height in B (Y2-Y1)
			SUB B
			LD B,A

			LD A,E				; Calculate the line width in C (X2-X1)
			SUB C
			JR C,Draw_Line_X1		; If carry set (negative result) then we are drawing from right to left
;
; This bit of code mods the main loop for drawing left to right
;
			LD C,A				; Store the line width
			LD A,0x2C			; Code for INC L
			LD (Draw_Line_Q1_M3),A		; Mod the code
			LD (Draw_Line_Q2_M3),A
			LD A,0x0A			; Code for RRC D (CB 0A)
			JR Draw_Line_X2			; Skip the next bit
;
; This bit of code mods the main loop for drawing right to left
;
Draw_Line_X1:		NEG				; The width of line is negative, so make it positive again
			LD C,A				; Store the line width
			LD A,0x2D			; Code for DEC L
			LD (Draw_Line_Q1_M3),A
			LD (Draw_Line_Q2_M3),A
			LD A,0x02			; Code for RLC D (CB 02)
;
; We've got the basic information at this point
;
Draw_Line_X2:		LD (Draw_Line_Q1_M2 + 1),A	; A contains the code for RLC D or RRC D, so make the mods
			LD (Draw_Line_Q2_M2 + 1),A
			LD D,(IX+0)			; Get the pixel data from the Point_Plot table
			LD A,B				; Check if B and C are 0
			OR C
			JR Z,Draw_Line_P		; There is no line, so just plot a single point
;
; At this point
; HL = Screen address of the start point
;  B = Line height (YL)
;  C = Line width (XL)
;  D = Pixel data
;
Draw_Line_Q:		LD A,B				; Work out which diagonal we are on
			CP C
			JR NC,Draw_Line_Q2
;
; This bit of code draws the line where B<C (more horizontal than vertical)
;
Draw_Line_Q1:		LD A,C				; A = XL
			LD (Draw_Line_Q1_M1 + 1),A	; Self-mod the code to store XL in loop
			LD C,B				; C = YL
			LD B,A				; B = XL (loop counter)
			LD E,B				; E = XL
			SRL E				; E = XL / 2 (error)
Draw_Line_Q1_L:		LD A,(HL)			; Plot the pixel
			XOR D
			LD (HL),A
			LD A,E				; Add the line height to the error (E = E - YL)
			SUB C
			LD E,A
			JR NC,Draw_Line_Q1_M2
Draw_Line_Q1_M1:	ADD A,0				; Add the line width to the error (E = E + XL) - previously self-modded
			LD E,A
			CALL Pixel_Address_Down
Draw_Line_Q1_M2:	RRC D				; Rotate the pixel right or left; more self-modifying code
			JR NC,Draw_Line_Q1_S
Draw_Line_Q1_M3:	INC L				; If we get a carry then move to adjacent screen address; more self modifying code
Draw_Line_Q1_S:		DJNZ Draw_Line_Q1_L		; Loop until the line is drawn
Draw_Line_P:		LD A,(HL)			; Plot the final point
			XOR D
			LD (HL),A
			RET
;
; This bit draws the line where B>=C (more vertical than horizontal, or diagonal)
;
Draw_Line_Q2:		LD (Draw_Line_Q2_M1 + 1),A	; Self-mod the code to store YL in loop
			LD E,B				; E = YL
			SRL E				; E = YL / 2 (error)
Draw_Line_Q2_L:		LD A,(HL)			; Plot the pixel
			XOR D
			LD (HL),A
			LD A,E				; Add the line width to the error
			SUB C				;
			JR NC,Draw_Line_Q2_S		; Skip the next bit if we don't get a carry
Draw_Line_Q2_M1: 	ADD A,0				; Add the line height to the error (E = E + XL) - previously self-modded
Draw_Line_Q2_M2:	RRC D				; Rotates the pixel right with carry
			JR NC,Draw_Line_Q2_S
Draw_Line_Q2_M3:	INC L				; If we get a carry then move to adjacent screen address; more self modifying code
Draw_Line_Q2_S:		LD E,A				; Store the error value back in
			CALL Pixel_Address_Down		; And also move down
			DJNZ Draw_Line_Q2_L
			JR Draw_Line_P			; Plot the final point

; Note that the functions above only work if each of these tables are in a byte boundary
;
Plot_Point:		DB %10000000,%01000000,%00100000,%00010000,%00001000,%00000100,%00000010,%00000001

public _render_x

_render_x:
    pop ix
    pop bc
    push ix

    push bc
    call Plot
    pop bc

    inc c
    inc b
    push bc
    call Plot
    pop bc

    dec c
    dec c
    push bc
    call Plot
    pop bc

    dec b
    dec b
    push bc
    call Plot
    pop bc

    inc c
    inc c
    call Plot

    ret
