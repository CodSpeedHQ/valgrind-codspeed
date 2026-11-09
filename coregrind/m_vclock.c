
/*--------------------------------------------------------------------*/
/*--- A virtual clock driven by guest instructions.      m_vclock.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_core_basics.h"
#include "pub_core_vki.h"
#include "pub_core_libcbase.h"
#include "pub_core_vclock.h"

/* Guest code runs under the big lock, so a plain read-modify-write
   from generated code is safe. */
ULong VG_(vclock_ns) = 0;

/* CLOCK_REALTIME at virtual time zero: 2024-01-01T00:00:00Z. */
#define VCLOCK_EPOCH_SECS 1704067200ULL
#define NS_PER_SEC        1000000000ULL

#if defined(VG_BIGENDIAN)
#  define VCLOCK_IEND Iend_BE
#else
#  define VCLOCK_IEND Iend_LE
#endif

/*--- Instrumentation ---*/

/* Emit VG_(vclock_ns) += n_insns * VG_VCLOCK_NS_PER_INSN as flat IR. */
static void add_elapsed ( IRSB* bb, UInt n_insns )
{
   IRExpr* addr = mkIRExpr_HWord((HWord)&VG_(vclock_ns));
   IRTemp  cur  = newIRTemp(bb->tyenv, Ity_I64);
   IRTemp  next = newIRTemp(bb->tyenv, Ity_I64);
   ULong   delta = (ULong)n_insns * VG_VCLOCK_NS_PER_INSN;

   addStmtToIRSB(bb, IRStmt_WrTmp(cur,
                        IRExpr_Load(VCLOCK_IEND, Ity_I64, addr)));
   addStmtToIRSB(bb, IRStmt_WrTmp(next,
                        IRExpr_Binop(Iop_Add64, IRExpr_RdTmp(cur),
                                     IRExpr_Const(IRConst_U64(delta)))));
   addStmtToIRSB(bb, IRStmt_Store(VCLOCK_IEND, addr, IRExpr_RdTmp(next)));
}

/* Dirty helpers that read the virtual TSC (see LibVEX_SetVirtualTSC). */
static Bool reads_tsc ( const IRStmt* st )
{
   const HChar* name;
   if (st->tag != Ist_Dirty)
      return False;
   name = st->Ist.Dirty.details->cee->name;
   return VG_(strcmp)(name, "amd64g_dirtyhelper_RDTSC") == 0
          || VG_(strcmp)(name, "amd64g_dirtyhelper_RDTSCP") == 0
          || VG_(strcmp)(name, "x86g_dirtyhelper_RDTSC") == 0
          || VG_(strcmp)(name, "arm64g_dirtyhelper_MRS_CNTVCT_EL0") == 0;
}

/* Instructions are counted by their IMarks. The count accumulated since
   the previous update is flushed before every conditional exit, before
   every TSC read and at the end of the block, so any path through the
   block adds exactly the instructions it executed and a TSC read sees
   them. Tools keep IMarks, so this runs correctly after their
   instrumentation. */
IRSB* VG_(vclock_instrument) ( void* closureV,
                               IRSB* sb_in,
                               const VexGuestLayout* layout,
                               const VexGuestExtents* vge,
                               const VexArchInfo* vai,
                               IRType gWordTy,
                               IRType hWordTy )
{
   IRSB* bb = deepCopyIRSBExceptStmts(sb_in);
   UInt  pending = 0;
   Int   i;

   for (i = 0; i < sb_in->stmts_used; i++) {
      IRStmt* st = sb_in->stmts[i];
      if (st->tag == Ist_IMark)
         pending++;
      if (pending > 0 && (st->tag == Ist_Exit || reads_tsc(st))) {
         add_elapsed(bb, pending);
         pending = 0;
      }
      addStmtToIRSB(bb, st);
   }
   if (pending > 0)
      add_elapsed(bb, pending);
   return bb;
}

/*--- Clocks ---*/

typedef enum { ClockUnknown, ClockRealtime, ClockElapsed } ClockKind;

static ClockKind clock_kind ( Int clockid )
{
   switch (clockid) {
      case VKI_CLOCK_REALTIME:
      case VKI_CLOCK_REALTIME_COARSE:
      case VKI_CLOCK_REALTIME_ALARM:
      case VKI_CLOCK_TAI:
         return ClockRealtime;
      case VKI_CLOCK_MONOTONIC:
      case VKI_CLOCK_MONOTONIC_RAW:
      case VKI_CLOCK_MONOTONIC_COARSE:
      case VKI_CLOCK_BOOTTIME:
      case VKI_CLOCK_BOOTTIME_ALARM:
      case VKI_CLOCK_PROCESS_CPUTIME_ID:
      case VKI_CLOCK_THREAD_CPUTIME_ID:
         return ClockElapsed;
      default:
         return ClockUnknown;
   }
}

static ULong clock_now_ns ( ClockKind kind )
{
   ULong ns = VG_(vclock_ns);
   if (kind == ClockRealtime)
      ns += VCLOCK_EPOCH_SECS * NS_PER_SEC;
   return ns;
}

Bool VG_(vclock_gettime) ( Int clockid, struct vki_timespec* ts )
{
   ClockKind kind = clock_kind(clockid);
   ULong ns;
   if (kind == ClockUnknown)
      return False;
   ns = clock_now_ns(kind);
   ts->tv_sec  = ns / NS_PER_SEC;
   ts->tv_nsec = ns % NS_PER_SEC;
   return True;
}

/* The kernel refuses to sleep on the coarse clocks and on another
   thread's CPU clock. */
Bool VG_(vclock_can_sleep) ( Int clockid )
{
   switch (clockid) {
      case VKI_CLOCK_REALTIME:
      case VKI_CLOCK_REALTIME_ALARM:
      case VKI_CLOCK_TAI:
      case VKI_CLOCK_MONOTONIC:
      case VKI_CLOCK_BOOTTIME:
      case VKI_CLOCK_BOOTTIME_ALARM:
      case VKI_CLOCK_PROCESS_CPUTIME_ID:
         return True;
      default:
         return False;
   }
}

/* The clock never exceeds this, so the realtime offset cannot wrap. */
#define VCLOCK_MAX_NS (~0ULL - VCLOCK_EPOCH_SECS * NS_PER_SEC)

/* Saturate instead of wrapping for absurdly long sleeps. */
static ULong timespec_to_ns ( const struct vki_timespec* ts )
{
   ULong secs = (ULong)ts->tv_sec;
   if (secs > (~0ULL - (ULong)ts->tv_nsec) / NS_PER_SEC)
      return ~0ULL;
   return secs * NS_PER_SEC + (ULong)ts->tv_nsec;
}

void VG_(vclock_sleep) ( Int clockid, Bool absolute,
                         const struct vki_timespec* req )
{
   ULong target = timespec_to_ns(req);
   if (absolute) {
      ULong now = clock_now_ns(clock_kind(clockid));
      if (target <= now)
         return;
      target -= now;
   }
   if (target > VCLOCK_MAX_NS - VG_(vclock_ns))
      VG_(vclock_ns) = VCLOCK_MAX_NS;
   else
      VG_(vclock_ns) += target;
}

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
