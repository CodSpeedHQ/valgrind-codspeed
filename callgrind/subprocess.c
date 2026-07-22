/* Everything about processes other than this one: recording the children it
 * spawns, and what a spawned process inherits from its spawner.
 */

#include "global.h"

/*------------------------------------------------------------*/
/*--- Children spawned by this process                     ---*/
/*------------------------------------------------------------*/

/* Children spawned since the last dump, listed in the header of the part they
 * were spawned during, which is what attributes a child to the exact part of
 * its parent that spawned it. */
static Int* spawned = 0;
static Int n_spawned = 0;
static Int spawned_capacity = 0;

static void record_spawned_child(Int child_pid)
{
   if (n_spawned == spawned_capacity) {
      spawned_capacity = spawned_capacity ? spawned_capacity * 2 : 8;
      spawned = VG_(realloc)("cl.subprocess.spawn.1", spawned,
                             spawned_capacity * sizeof(Int));
   }
   spawned[n_spawned++] = child_pid;
}

/* "desc:" lines so other consumers of the format (kcachegrind,
 * callgrind_annotate) ignore them. */
void CLG_(print_spawned_children)(VgFile* fp)
{
   Int i;

   for (i = 0; i < n_spawned; i++)
      VG_(fprintf)(fp, "desc: Spawned pid: %d\n", spawned[i]);
}

void CLG_(forget_spawned_children)(void)
{
   n_spawned = 0;
}

/*------------------------------------------------------------*/
/*--- Fork                                                 ---*/
/*------------------------------------------------------------*/

/* Whether the syscall currently in flight is a fork, as announced by the core
 * through the atfork pre hook. The hooks do not carry the new child's pid, but
 * the syscall it is announcing returns it in the parent, so the pid is picked
 * up when that syscall returns. */
static Bool forking = False;

static void clg_atfork_pre(ThreadId tid)
{
   forking = True;
}

/* Costs accumulated before the fork belong to - and are dumped by - the
 * parent; zero everything so this process only reports its own work. Threads
 * other than the forking one do not exist in the child; zeroing their copied
 * state means they are skipped at dump time (zero delta). The part counter
 * restarts at 1, matching an exec'd child.
 */
static void clg_atfork_child(ThreadId tid)
{
   forking = False;
   CLG_(reset_dump_counter)();
   /* the inherited edges are the parent's; this process only reports the
    * children it spawns itself */
   CLG_(forget_spawned_children)();

   CLG_(zero_all_cost)(False);

   /* The fork itself is a syscall in flight: its start time was stamped in the
    * parent, but the thread CPU clock restarts in the child, so the delta
    * computed at the syscall exit would underflow. */
   CLG_(restamp_syscall_time)(tid);
}

void CLG_(syscall_return)(SysRes res)
{
   if (!forking) return;
   forking = False;

   /* A failed fork spawned nothing. A zero result is the child returning from
    * the fork, which cleared the flag in its atfork hook already. */
   if (sr_isError(res) || (Int)sr_Res(res) <= 0) return;

   record_spawned_child((Int)sr_Res(res));
}

void CLG_(init_subprocess)(void)
{
   VG_(atfork)(clg_atfork_pre, NULL, clg_atfork_child);
}

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
