/*--------------------------------------------------------------------*/
/*--- Shared cycle-LUT key contract (offline generator + runtime).  ---*/
/*---                                                      sigkey.h ---*/
/*--------------------------------------------------------------------*/

/* CodSpeed extension. Defines the packed u64 lookup key shared by the LUT
 * generator (lut-gen/lib/sigkey.py in the sibling valgrind-helpers repo) and
 * the Capstone runtime (cycledecode.c). The two MUST compute identical keys;
 * keep this file and lut-gen/lib/sigkey.py in lockstep (lut-gen/test_lut.py
 * enforces it).
 *
 *   key = (insn_id << 24) | sig
 *
 * insn_id is a Capstone X86_INS_* / ARM64_INS_* enumerator. sig is a 24-bit,
 * id-independent signature describing the first three explicit operands, the
 * effective operand width, and prefix flags.
 */

#ifndef CLG_SIGKEY_H
#define CLG_SIGKEY_H

/* operand type codes */
#define CLG_OP_NONE  0u
#define CLG_OP_REG   1u
#define CLG_OP_MEM   2u
#define CLG_OP_IMM   3u
#define CLG_OP_OTHER 4u

/* width class codes */
#define CLG_W_ANY 0u
#define CLG_W_8   1u
#define CLG_W_16  2u
#define CLG_W_32  3u
#define CLG_W_64  4u
#define CLG_W_128 5u
#define CLG_W_256 6u
#define CLG_W_512 7u

/* flag bits (lock/rep are x86-only, vec is arm64-only; they never co-occur) */
#define CLG_F_LOCK 1u
#define CLG_F_REP  2u
#define CLG_F_VEC  4u

/* sig field layout (24 bits) */
#define CLG_SIG(o0, o1, o2, w, f)                  \
   ( (((unsigned)(o0) & 7u) << 21)                 \
   | (((unsigned)(o1) & 7u) << 18)                 \
   | (((unsigned)(o2) & 7u) << 15)                 \
   | (((unsigned)(w)  & 15u) << 11)                \
   | (((unsigned)(f)  & 7u) << 8) )

#define CLG_CSKEY(id, sig)                         \
   ( ((unsigned long long)(id) << 24) | (unsigned long long)(unsigned)(sig) )

/* Map an operand width in bits to a width class code. */
static inline unsigned clg_width_code(unsigned bits)
{
   switch (bits) {
   case 8:   return CLG_W_8;
   case 16:  return CLG_W_16;
   case 32:  return CLG_W_32;
   case 64:  return CLG_W_64;
   case 128: return CLG_W_128;
   case 256: return CLG_W_256;
   case 512: return CLG_W_512;
   default:  return CLG_W_ANY;
   }
}

#endif /* CLG_SIGKEY_H */
