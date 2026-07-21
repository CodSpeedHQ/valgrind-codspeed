
/*--------------------------------------------------------------------*/
/*--- The thread state.                     pub_tool_threadstate.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2017 Julian Seward
      jseward@acm.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#ifndef __PUB_TOOL_THREADSTATE_H
#define __PUB_TOOL_THREADSTATE_H

#include "pub_tool_basics.h"   // ThreadID

/* The maximum number of pthreads that we support. */
extern UInt VG_N_THREADS;

/* Special magic value for an invalid ThreadId.  It corresponds to
   LinuxThreads using zero as the initial value for
   pthread_mutex_t.__m_owner and pthread_cond_t.__c_waiting. */
#define VG_INVALID_THREADID ((ThreadId)(0))

/* Get the TID of the thread which currently has the CPU. */
extern ThreadId VG_(get_running_tid) ( void );

/* Get the name a thread set for itself (e.g. via prctl(PR_SET_NAME) or
   pthread_setname_np on the calling thread), or NULL if it has none. The
   returned pointer is owned by the core and only valid while the thread
   is alive. */
extern const HChar* VG_(get_thread_name) ( ThreadId tid );

/* Get the kernel thread id (LWP id) of a thread, or 0 if tid is not a valid
   live thread. Unlike ThreadId, this is the OS-level identity the rest of the
   system sees. */
extern Int VG_(get_thread_lwpid) ( ThreadId tid );

#endif   // __PUB_TOOL_THREADSTATE_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
