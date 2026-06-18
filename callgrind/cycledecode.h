/*--------------------------------------------------------------------*/
/*--- Per-instruction cycle estimation: decode + LUT lookup.       ---*/
/*---                                                 cycledecode.h ---*/
/*--------------------------------------------------------------------*/

/* CodSpeed extension. Estimates per-instruction throughput/latency cost by
 * decoding the real guest instruction (Capstone) and looking the result up in
 * a generated cost table (x86_caps_lut.inc / arm64_caps_lut.inc), keyed by the
 * packed signature in sigkey.h.
 */

#ifndef CLG_CYCLEDECODE_H
#define CLG_CYCLEDECODE_H

/* Result of clg_cycledecode_init(): which init step failed, or OK. */
typedef enum {
   CLG_CD_OK = 0,
   CLG_CD_ERR_MEM_OPT,    /* cs_option(0, CS_OPT_MEM) failed       */
   CLG_CD_ERR_OPEN,       /* cs_open failed                        */
   CLG_CD_ERR_DETAIL,     /* cs_option(CS_OPT_DETAIL, CS_OPT_ON) failed */
   CLG_CD_ERR_INSN_ALLOC, /* cs_malloc failed                      */
} ClgCdInit;

/* Per-tier lookup accounting since init. The decoded total equals the sum of
 * the other five fields. */
typedef struct {
   unsigned long decoded;     /* total clg_cycle_cost() calls               */
   unsigned long hit_exact;   /* matched on the exact signature             */
   unsigned long hit_width;   /* matched after dropping the width field     */
   unsigned long hit_default; /* matched the per-instruction sig==0 default */
   unsigned long decode_fail; /* Capstone failed to decode the bytes        */
   unsigned long miss;        /* decoded but no row, even the default        */
} ClgCdStats;

/* Initialise Capstone and the cost table. Returns CLG_CD_OK if cycle estimation
 * is available (built with Capstone, amd64/arm64 guest). Call once at startup
 * before clg_cycle_cost(). */
ClgCdInit clg_cycledecode_init(void);

/* Human-readable name of an init result, for the disable-it message. */
const char* clg_cycledecode_init_str(ClgCdInit st);

/* Decode the guest instruction in [bytes, bytes+len) and write its estimated
 * cost in centi-cycles (1 cycle = 100): *ct = reciprocal throughput
 * (throughput-bound), *cl = latency (latency-bound). Always fills *ct/*cl: on a
 * decode failure or table miss it writes the flat fallback cost (1.00 cycle)
 * and counts it. */
void clg_cycle_cost(const unsigned char* bytes,
                    unsigned             len,
                    unsigned*            ct,
                    unsigned*            cl);

/* Per-tier diagnostic counters since init. */
void clg_cycledecode_stats(ClgCdStats* out);

#endif /* CLG_CYCLEDECODE_H */
