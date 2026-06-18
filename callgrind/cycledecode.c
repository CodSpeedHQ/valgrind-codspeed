/*--------------------------------------------------------------------*/
/*--- Per-instruction cycle estimation: decode + LUT lookup.       ---*/
/*---                                                 cycledecode.c ---*/
/*--------------------------------------------------------------------*/

/* CodSpeed extension. See cycledecode.h.
 *
 * Decodes the real guest instruction (via the cycledecode_capstone.c bridge)
 * and looks the result up in a generated cost table (x86_caps_lut.inc /
 * arm64_caps_lut.inc) keyed by the packed signature defined in sigkey.h. Works
 * for both amd64 and arm64 guests (the tool is built natively for its primary
 * arch, so host == guest here).
 */

#include "cycledecode.h"

#if defined(CLG_WITH_CAPSTONE)

#include "cycledecode_capstone.h"
#include "sigkey.h"
#include <stdint.h>

/*------------------------------------------------------------*/
/*--- Generated cost table                                  -*/
/*------------------------------------------------------------*/

typedef struct {
   unsigned long long key;
   unsigned           cy;
   unsigned           cl;
} ClgRow;

typedef struct {
   unsigned off; /* index of this id's first row in clg_lut_rows */
   unsigned n;   /* number of rows for this id (0 => not present) */
} ClgRange;

/* Defines clg_lut_rows[] (grouped by insn id, sig-ascending within each id) and
 * clg_lut_range[] (per-id [off,count) into clg_lut_rows, indexed by enum id).
 */
#include CLG_LUT_INC

static const int clg_lut_nrange =
   (int)(sizeof(clg_lut_range) / sizeof(clg_lut_range[0]));

/* Flat cost charged when no table row applies (1.00 cycle, centi-cycles). */
#define CLG_CD_FALLBACK_CT 100
#define CLG_CD_FALLBACK_CL 100

static ClgCdStats stats;

/*------------------------------------------------------------*/
/*--- Lookup                                                -*/
/*------------------------------------------------------------*/

/* Find the row for (id, sig): O(1) id->block, then binary search by sig within
 * the block (rows are sig-ascending). Returns 0 if the id or sig is absent. */
static const ClgRow* row_for(unsigned id, unsigned sig)
{
   if (id >= (unsigned)clg_lut_nrange)
      return 0;
   ClgRange r = clg_lut_range[id];
   if (r.n == 0)
      return 0;

   unsigned long long want = CLG_CSKEY(id, sig);
   int                lo = (int)r.off, hi = (int)(r.off + r.n) - 1;
   while (lo <= hi) {
      int mid = lo + (hi - lo) / 2;
      if (clg_lut_rows[mid].key == want)
         return &clg_lut_rows[mid];
      if (clg_lut_rows[mid].key < want)
         lo = mid + 1;
      else
         hi = mid - 1;
   }
   return 0;
}

/* Strip the width field (sig bits [14:11]) for a width-agnostic retry. */
static unsigned sig_any_width(unsigned sig)
{
   return sig & ~((unsigned)0xF << 11);
}

/*------------------------------------------------------------*/
/*--- Per-arch signature from cs_detail                     -*/
/*------------------------------------------------------------*/

#if CLG_CS_ARCH_SEL == 1

static unsigned op_code_x86(x86_op_type t)
{
   switch (t) {
   case X86_OP_REG:
      return CLG_OP_REG;
   case X86_OP_MEM:
      return CLG_OP_MEM;
   case X86_OP_IMM:
      return CLG_OP_IMM;
   default:
      return CLG_OP_OTHER;
   }
}

static unsigned scalar_fp_width_x86(unsigned id)
{
   switch (id) {
   case X86_INS_ADDSS:
   case X86_INS_CMPSS:
   case X86_INS_COMISS:
   case X86_INS_CVTSD2SS:
   case X86_INS_CVTSI2SS:
   case X86_INS_CVTSS2SI:
   case X86_INS_CVTTSS2SI:
   case X86_INS_DIVSS:
   case X86_INS_MAXSS:
   case X86_INS_MINSS:
   case X86_INS_MOVSS:
   case X86_INS_MULSS:
   case X86_INS_RCPSS:
   case X86_INS_RSQRTSS:
   case X86_INS_SQRTSS:
   case X86_INS_SUBSS:
   case X86_INS_UCOMISS:
      return 32;
   case X86_INS_ADDSD:
   case X86_INS_CMPSD:
   case X86_INS_COMISD:
   case X86_INS_CVTDQ2PD:
   case X86_INS_CVTPD2DQ:
   case X86_INS_CVTPD2PI:
   case X86_INS_CVTPD2PS:
   case X86_INS_CVTPI2PD:
   case X86_INS_CVTPS2PD:
   case X86_INS_CVTSD2SI:
   case X86_INS_CVTSI2SD:
   case X86_INS_CVTSS2SD:
   case X86_INS_CVTTPD2DQ:
   case X86_INS_CVTTPD2PI:
   case X86_INS_CVTTSD2SI:
   case X86_INS_DIVSD:
   case X86_INS_MAXSD:
   case X86_INS_MINSD:
   case X86_INS_MOVSD:
   case X86_INS_MULSD:
   case X86_INS_SQRTSD:
   case X86_INS_SUBSD:
   case X86_INS_UCOMISD:
      return 64;
   default:
      return 0;
   }
}

static unsigned compute_sig(const cs_insn* in)
{
   const cs_x86* x    = &in->detail->x86;
   unsigned      o[3] = {CLG_OP_NONE, CLG_OP_NONE, CLG_OP_NONE};
   unsigned      w = 0, flags = 0;

   for (int i = 0; i < x->op_count && i < 3; i++)
      o[i] = op_code_x86(x->operands[i].type);

   for (int i = 0; i < x->op_count; i++) {
      const cs_x86_op* op = &x->operands[i];
      if (op->type == X86_OP_REG || op->type == X86_OP_MEM) {
         unsigned bits = (unsigned)op->size * 8u;
         if (bits > w)
            w = bits;
      }
   }
   unsigned scalar_w = scalar_fp_width_x86(in->id);
   if (scalar_w)
      w = scalar_w;
   for (int i = 0; i < 4; i++) {
      unsigned char pfx = x->prefix[i];
      if (pfx == X86_PREFIX_LOCK)
         flags |= CLG_F_LOCK;
      if (pfx == X86_PREFIX_REP || pfx == X86_PREFIX_REPNE)
         flags |= CLG_F_REP;
   }
   return CLG_SIG(o[0], o[1], o[2], clg_width_code(w), flags);
}

#elif CLG_CS_ARCH_SEL == 2

static unsigned vas_elem_bits(arm64_vas vas)
{
   switch (vas) {
   case ARM64_VAS_8B:
   case ARM64_VAS_16B:
      return 8;
   case ARM64_VAS_4H:
   case ARM64_VAS_8H:
      return 16;
   case ARM64_VAS_2S:
   case ARM64_VAS_4S:
      return 32;
   case ARM64_VAS_1D:
   case ARM64_VAS_2D:
      return 64;
   case ARM64_VAS_1Q:
      return 128;
   default:
      return 0;
   }
}

static unsigned scalar_fp_bits(arm64_reg r)
{
   if (r >= ARM64_REG_B0 && r <= ARM64_REG_B31)
      return 8;
   if (r >= ARM64_REG_H0 && r <= ARM64_REG_H31)
      return 16;
   if (r >= ARM64_REG_S0 && r <= ARM64_REG_S31)
      return 32;
   if (r >= ARM64_REG_D0 && r <= ARM64_REG_D31)
      return 64;
   if (r >= ARM64_REG_Q0 && r <= ARM64_REG_Q31)
      return 128;
   return 0;
}

static unsigned gp_reg_bits(arm64_reg r)
{
   if (r >= ARM64_REG_W0 && r <= ARM64_REG_W30)
      return 32;
   if (r == ARM64_REG_WSP || r == ARM64_REG_WZR)
      return 32;
   if (r >= ARM64_REG_X0 && r <= ARM64_REG_X28)
      return 64;
   if (r == ARM64_REG_X29 || r == ARM64_REG_X30)
      return 64;
   if (r == ARM64_REG_SP || r == ARM64_REG_XZR)
      return 64;
   return 0;
}

/* arm64 keys on operand width + vector flag only; the three operand-type
 * codes are intentionally left NONE (the LUT generator keys arm64 the same
 * way). The x86 compute_sig above fills them. */
static unsigned compute_sig(const cs_insn* in)
{
   const cs_arm64* a = &in->detail->arm64;
   unsigned        w = 0, flags = 0;

   for (int i = 0; i < a->op_count; i++)
      if (a->operands[i].vas != ARM64_VAS_INVALID) {
         flags |= CLG_F_VEC;
         unsigned ew = vas_elem_bits(a->operands[i].vas);
         if (ew > w)
            w = ew;
      }
   if (!(flags & CLG_F_VEC))
      for (int i = 0; i < a->op_count; i++)
         if (a->operands[i].type == ARM64_OP_REG) {
            arm64_reg r  = a->operands[i].reg;
            unsigned  ew = scalar_fp_bits(r);
            if (ew == 0)
               ew = gp_reg_bits(r);
            if (ew > w)
               w = ew;
         }
   return CLG_SIG(CLG_OP_NONE, CLG_OP_NONE, CLG_OP_NONE, clg_width_code(w),
                  flags);
}

#endif

/*------------------------------------------------------------*/
/*--- Public ABI                                            -*/
/*------------------------------------------------------------*/

ClgCdInit clg_cycledecode_init(void)
{
   ClgCdInit st = clg_cs_bridge_init();
   if (st != CLG_CD_OK)
      return st;

   stats = (ClgCdStats){0};
   return CLG_CD_OK;
}

const char* clg_cycledecode_init_str(ClgCdInit st)
{
   switch (st) {
   case CLG_CD_OK:
      return "ok";
   case CLG_CD_ERR_MEM_OPT:
      return "cs_option(CS_OPT_MEM) failed";
   case CLG_CD_ERR_OPEN:
      return "cs_open failed";
   case CLG_CD_ERR_DETAIL:
      return "cs_option(CS_OPT_DETAIL) failed";
   case CLG_CD_ERR_INSN_ALLOC:
      return "cs_malloc failed";
   default:
      return "unknown";
   }
}

void clg_cycle_cost(const unsigned char* bytes,
                    unsigned             len,
                    unsigned*            ct,
                    unsigned*            cl)
{
   stats.decoded++;

   const cs_insn* in = clg_cs_bridge_decode(bytes, len);
   if (!in) {
      stats.decode_fail++;
      goto fallback;
   }

   unsigned      id  = in->id;
   unsigned      sig = compute_sig(in);
   unsigned      any = sig_any_width(sig);
   const ClgRow* row;

   /* exact -> width-agnostic -> per-instruction default -> miss */
   if ((row = row_for(id, sig))) {
      stats.hit_exact++;
   } else if (any != sig && (row = row_for(id, any))) {
      stats.hit_width++;
   } else if ((row = row_for(id, 0))) {
      stats.hit_default++;
   } else {
      stats.miss++;
      goto fallback;
   }

   *ct = row->cy; /* throughput-bound (reciprocal throughput) */
   *cl = row->cl; /* latency-bound */
   return;

fallback:
   *ct = CLG_CD_FALLBACK_CT;
   *cl = CLG_CD_FALLBACK_CL;
}

void clg_cycledecode_stats(ClgCdStats* out)
{
   if (out)
      *out = stats;
}

#else /* !CLG_WITH_CAPSTONE */

#error                                                                         \
   "Callgrind cycle estimation requires Capstone; configure with --with-capstone=PATH (or set CAPSTONE_DIR)."

#endif
