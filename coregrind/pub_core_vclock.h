
/*--------------------------------------------------------------------*/
/*--- A virtual clock driven by guest instructions. pub_core_vclock.h ---*/
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

#ifndef __PUB_CORE_VCLOCK_H
#define __PUB_CORE_VCLOCK_H

//--------------------------------------------------------------------
// PURPOSE: With --deterministic-time=yes, every clock the client can
// read is derived from the number of guest instructions executed so
// far, so a given instruction stream always observes the same time.
// Host time never reaches the client.
//--------------------------------------------------------------------

#include "pub_core_basics.h"
#include "pub_core_vki.h"
#include "libvex.h"

/* The virtual CPU retires one instruction per nanosecond. The TSC
   (rdtsc, CNTVCT_EL0) ticks once per nanosecond as well. */
#define VG_VCLOCK_NS_PER_INSN  1ULL
#define VG_VCLOCK_TSC_HZ       1000000000ULL

/* Nanoseconds elapsed since the client started. Generated code adds
   executed instructions to it; sleeps add their duration. */
extern ULong VG_(vclock_ns);

/* VEX instrument2 pass: add the executed instruction count of each
   path through the superblock to VG_(vclock_ns). */
extern IRSB* VG_(vclock_instrument) ( void* closureV,
                                      IRSB* sb_in,
                                      const VexGuestLayout* layout,
                                      const VexGuestExtents* vge,
                                      const VexArchInfo* vai,
                                      IRType gWordTy,
                                      IRType hWordTy );

/* Fill *ts with the virtual time of CLOCKID. Returns False for clocks
   the kernel must serve (dynamic and unknown ids). */
extern Bool VG_(vclock_gettime) ( Int clockid, struct vki_timespec* ts );

/* Whether a sleep on CLOCKID is served here; otherwise the kernel
   handles it and reports the error it would normally give. */
extern Bool VG_(vclock_can_sleep) ( Int clockid );

/* Complete a sleep by moving the virtual clock forward. REQ is a valid
   timespec, relative to now, or absolute on CLOCKID when ABSOLUTE. */
extern void VG_(vclock_sleep) ( Int clockid, Bool absolute,
                                const struct vki_timespec* req );

#endif   // __PUB_CORE_VCLOCK_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
