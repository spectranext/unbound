#ifndef __ENV_H__
#define __ENV_H__

// CLION-specific definitions for syntax highlighting

#if defined(__CLION_IDE_) | defined(__INTELLISENSE__)

#undef  __banked
#undef __LIB__
#undef __z88dk_fastcall
#undef __FASTCALL__
#undef __CALLEE__
#undef __SCCZ80
#undef __Z80
#undef __naked
#undef __z88dk_callee
#undef __stdc
#undef __preserves_regs
#undef __no_z88dk_declspec

#define  __banked
#define __LIB__
#define __z88dk_fastcall
#define __FASTCALL__
#define __CALLEE__
#define __SCCZ80
#define __Z80
#define __naked
#define __z88dk_callee
#define __stdc
#define __preserves_regs
#define __no_z88dk_declspec

#endif

#endif