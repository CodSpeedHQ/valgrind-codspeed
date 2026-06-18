/*--------------------------------------------------------------------*/
/*--- Capstone bridge for cycle estimation: handle + libc shims.   ---*/
/*---                                        cycledecode_capstone.h ---*/
/*--------------------------------------------------------------------*/

#ifndef CLG_CYCLEDECODE_CAPSTONE_H
#define CLG_CYCLEDECODE_CAPSTONE_H

#include "cycledecode.h" /* ClgCdInit */
#include <capstone/capstone.h>

/* Arch selection. The build can force it with -DCLG_CS_ARCH_SEL=1 (x86) or =2
 * (arm64); otherwise it follows the host (== guest, since the tool is native).
 */
#if !defined(CLG_CS_ARCH_SEL)
#if defined(__aarch64__)
#define CLG_CS_ARCH_SEL 2
#elif defined(__x86_64__) || defined(__i386__)
#define CLG_CS_ARCH_SEL 1
#else
#error "cycledecode: unsupported guest arch"
#endif
#endif

#if CLG_CS_ARCH_SEL == 1
#define CLG_CS_ARCH CS_ARCH_X86
#define CLG_CS_MODE CS_MODE_64
#define CLG_LUT_INC "x86_caps_lut.inc"
#elif CLG_CS_ARCH_SEL == 2
#define CLG_CS_ARCH CS_ARCH_ARM64
#define CLG_CS_MODE CS_MODE_ARM
#define CLG_LUT_INC "arm64_caps_lut.inc"
#else
#error "cycledecode: bad CLG_CS_ARCH_SEL"
#endif

/* Open the Capstone handle for the selected arch, enable instruction detail,
 * and allocate the scratch insn. Returns CLG_CD_OK or the failing step. */
ClgCdInit clg_cs_bridge_init(void);

/* Decode one instruction from [bytes, bytes+len). Returns the scratch cs_insn*
 * (valid until the next call) on success, or NULL on decode failure. */
const cs_insn* clg_cs_bridge_decode(const unsigned char* bytes, unsigned len);

#endif /* CLG_CYCLEDECODE_CAPSTONE_H */
