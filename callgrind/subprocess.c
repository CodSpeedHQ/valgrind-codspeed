/* Everything about processes other than this one: recording the children it
 * spawns, and what a spawned process inherits from its spawner.
 */

#include "global.h"

/*------------------------------------------------------------*/
/*--- Instrumentation state handover (--instr-atstart=inherit)---*/
/*------------------------------------------------------------*/

/* A traced exec replaces the image (and resets valgrind) but keeps the PID, so
 * runtime state can be handed over through a file whose name is derived from
 * the PID alone: each process advertises "instrumentation is on" by keeping
 * <tmpdir>/callgrind-instr-<pid> in existence, and a fresh valgrind started
 * with --instr-atstart=inherit adopts the state advertised for its own PID. A
 * fork child inherits the in-memory state with the address space and just
 * republishes it under its new PID.
 *
 * The file is empty: its existence is the whole message.
 */

static void instr_state_path(HChar* buf, Int size)
{
   VG_(snprintf)(buf, size, "%s/callgrind-instr-%d",
                 VG_(tmpdir)(), VG_(getpid)());
}

void CLG_(publish_instr_state)(Bool on)
{
   HChar path[256];
   SysRes res;

   if (CLG_(clo).instrument_atstart != instr_atstart_inherit) return;

   instr_state_path(path, sizeof path);
   if (!on) {
      VG_(unlink)(path);
      return;
   }
   res = VG_(open)(path, VKI_O_CREAT|VKI_O_WRONLY,
                   VKI_S_IRUSR|VKI_S_IWUSR);
   if (sr_isError(res)) {
      VG_(message)(Vg_UserMsg,
                   "warning: cannot create instrumentation state file %s\n",
                   path);
      return;
   }
   VG_(close)(sr_Res(res));
}

Bool CLG_(inherited_instr_state)(void)
{
   HChar path[256];
   struct vg_stat st;

   instr_state_path(path, sizeof path);
   return !sr_isError(VG_(stat)(path, &st));
}

/*------------------------------------------------------------*/
/*--- --obj-skip handover for objects added at runtime      ---*/
/*------------------------------------------------------------*/

/* CALLGRIND_ADD_OBJ_SKIP() adds an object to skip after startup, so unlike an
 * --obj-skip on the original command line it is not something a traced exec
 * would replay to the child. Appending it to VG_(args_for_valgrind) folds it
 * into the same argv the core already reconstructs for exec'd children, so
 * the child picks it up as an ordinary --obj-skip option.
 */
void CLG_(publish_obj_skip)(const HChar* obj_name)
{
   HChar* arg = VG_(malloc)("cl.subprocess.objskip.1",
                            VG_(strlen)(obj_name) + sizeof("--obj-skip="));
   VG_(sprintf)(arg, "--obj-skip=%s", obj_name);
   VG_(addToXA)(VG_(args_for_valgrind), &arg);
}

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

   /* the state file of the parent's PID belongs to the parent; advertise the
    * inherited in-memory state under our own PID */
   CLG_(publish_instr_state)(CLG_(instrument_state));

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
