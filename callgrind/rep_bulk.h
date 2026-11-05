/*--------------------------------------------------------------------*/
/*--- REP bulk cost model: calibrated hot-copy curve.              ---*/
/*---                                                    rep_bulk.h ---*/
/*--------------------------------------------------------------------*/

/* CodSpeed extension.
 *
 * Replaces the per-element cost of rep-prefixed MOVS/STOS with a calibrated
 * curve F: the hot-cache cost of one instruction over a given number of
 * bytes, in centi-cycles. Each executed element adds
 *
 *    F(remaining_bytes) - F(remaining_bytes - elem_bytes)
 *
 * which telescopes to F(total_bytes) and stays exact across signal
 * interruption, because "remaining" is read from the guest repeat counter.
 */

#ifndef CLG_REP_BULK_H
#define CLG_REP_BULK_H

#include "pub_tool_basics.h"

typedef enum {
   CLG_REP_OP_NONE = 0,
   CLG_REP_OP_MOVS = 1,  /* rep movs{b,w,d,q}: one read + one write per element */
   CLG_REP_OP_STOS = 2   /* rep stos{b,w,d,q}: one write per element */
} ClgRepBulkOp;

typedef struct {
   unsigned char op;         /* ClgRepBulkOp                            */
   unsigned char elem_width; /* element size in bytes (1,2,4,8), 0 if none */
} ClgRepBulkInfo;

/* From the guest DF flag, which is constant during an instruction. */
#define CLG_REP_DIR_FORWARD  0
#define CLG_REP_DIR_BACKWARD 1

/* Alignment class, latched from the first element's pointers (STOS uses
 * dst as src): 0 both %64==0, 1 both %64==8, 2 other (conservative). */
#define CLG_REP_ALIGN_ALIGNED      0
#define CLG_REP_ALIGN_SAME_UNALIGN 1
#define CLG_REP_ALIGN_OTHER        2

typedef struct {
   unsigned char op;          /* ClgRepBulkOp                            */
   unsigned char elem_width;  /* 1, 2, 4 or 8 bytes                      */
   unsigned char direction;   /* CLG_REP_DIR_*                           */
   unsigned char align_class; /* CLG_REP_ALIGN_*                         */
} ClgRepBulkKey;

typedef struct {
   UInt  bytes;        /* cumulative element bytes, strictly ascending    */
   ULong centicycles;  /* total hot-resident cost, centi-cycles           */
} ClgRepBulkPoint;

typedef struct {
   ClgRepBulkKey          key;
   const ClgRepBulkPoint* pts;
   UInt                   npts;
} ClgRepBulkCurve;

/* Assert that the generated table has every key in index order. Call once
 * at startup before clg_rep_bulk_marginal. */
void clg_rep_bulk_init(void);

/* Marginal bulk cost of the element at `remaining_bytes` (bytes still to
 * execute before this element, a nonzero multiple of elem_bytes), in
 * centi-cycles. The key must be a classified (op, width) pair with a
 * CLG_REP_DIR_* and CLG_REP_ALIGN_* value; beyond the last calibrated
 * point the last segment's slope is extended. */
ULong clg_rep_bulk_marginal(unsigned op, unsigned elem_width,
                            unsigned direction, unsigned align_class,
                            ULong remaining_bytes, ULong elem_bytes);

/* Classify the amd64 guest instruction in [bytes, bytes+len) by its
 * encoding (no Capstone needed). Always initialises *out; out->op is
 * CLG_REP_OP_NONE unless it is a rep-prefixed MOVS or STOS. */
void clg_rep_bulk_classify(const unsigned char* bytes, unsigned len,
                           ClgRepBulkInfo* out);

#endif /* CLG_REP_BULK_H */
