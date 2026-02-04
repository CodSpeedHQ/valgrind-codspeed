/*--------------------------------------------------------------------*/
/*--- Tracegrind data structures, functions.               global.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2004-2017 Josef Weidendorfer
      josef.weidendorfer@gmx.de

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

#ifndef TG_GLOBAL
#define TG_GLOBAL

#include "pub_tool_basics.h"
#include "pub_tool_vki.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_xarray.h"
#include "pub_tool_clientstate.h"
#include "pub_tool_machine.h"      // VG_(fnptr_to_fnentry)

#include "events.h" // defines TG_ macro
#include "costs.h"


/*------------------------------------------------------------*/
/*--- Tracegrind compile options                           --- */
/*------------------------------------------------------------*/

/* Enable debug output */
#define TG_ENABLE_DEBUG 1

/* Enable experimental features? */
#define TG_EXPERIMENTAL 0


/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

#define DEFAULT_OUTFORMAT   "tracegrind.out.%p"

/* If and how to collect syscall time.
   systime_no : do not collect systime
   systime_msec : collect syscount, systime elapsed, milli second precision.
   systime_usec : collect syscount, systime elapsed, micro second precision.
   systime_nsec : collect syscount, systime elapsed, systime cpu, nano second
                  precision.  */
typedef enum {
   systime_no,
   systime_msec,
   systime_usec,
   systime_nsec
} Collect_Systime;

typedef struct _CommandLineOptions CommandLineOptions;
struct _CommandLineOptions {

  /* Dump format options */
  const HChar* out_format;  /* Format string for tracegrind output file name */
  Bool combine_dumps;       /* Dump trace parts into same file? */
  Bool compress_strings;
  Bool compress_events;
  Bool compress_pos;
  Bool mangle_names;
  Bool compress_mangled;
  Bool dump_line;
  Bool dump_instr;
  Bool dump_bb;
  Bool dump_bbs;         /* Dump basic block information? */
  
  /* Dump generation options */
  ULong dump_every_bb;     /* Dump every xxx BBs. */
  
  /* Collection options */
  Bool separate_threads; /* Separate threads in dump? */
  Int  separate_callers; /* Separate dependent on how many callers? */
  Int  separate_recursions; /* Max level of recursions to separate */
  Bool skip_plt;         /* Skip functions in PLT section? */
  Bool skip_direct_recursion; /* Increment direct recursions the level? */

  Bool collect_atstart;  /* Start in collecting state ? */
  Bool collect_jumps;    /* Collect (cond.) jumps in functions ? */

  Bool collect_alloc;    /* Collect size of allocated memory */
  Collect_Systime collect_systime;  /* Collect time for system calls */

  Bool collect_bus;      /* Collect global bus events */

  /* Instrument options */
  Bool instrument_atstart;  /* Instrument at start? */
  Bool simulate_cache;      /* Call into cache simulator ? */
  Bool simulate_branch;     /* Call into branch prediction simulator ? */

  /* Call graph generation */
  Bool pop_on_jump;       /* Handle a jump between functions as ret+call */
  Int objs_to_skip_count; /* Number of objects to skip */
  HChar** objs_to_skip;  /* List of objects to skip */

#if TG_ENABLE_DEBUG
  Int   verbose;
  ULong verbose_start;
#endif
};

/*------------------------------------------------------------*/
/*--- Constants                                            ---*/
/*------------------------------------------------------------*/

/* Minimum cache line size allowed */
#define MIN_LINE_SIZE   16


/*------------------------------------------------------------*/
/*--- Statistics                                           ---*/
/*------------------------------------------------------------*/

typedef struct _Statistics Statistics;
struct _Statistics {
  ULong call_counter;
  ULong jcnd_counter;
  ULong jump_counter;
  ULong rec_call_counter;
  ULong ret_counter;
  ULong bb_executions;

  Int  context_counter;
  Int  bb_retranslations;  

  Int  distinct_objs;
  Int  distinct_files;
  Int  distinct_fns;
  Int  distinct_contexts;
  Int  distinct_bbs;
  Int  distinct_jccs;
  Int  distinct_bbccs;
  Int  distinct_instrs;
  Int  distinct_skips;

  Int  bb_hash_resizes;
  Int  bbcc_hash_resizes;
  Int  jcc_hash_resizes;
  Int  cxt_hash_resizes;
  Int  fn_array_resizes;
  Int  call_stack_resizes;
  Int  fn_stack_resizes;

  Int  full_debug_BBs;
  Int  file_line_debug_BBs;
  Int  fn_name_debug_BBs;
  Int  no_debug_BBs;
  Int  bbcc_lru_misses;
  Int  jcc_lru_misses;
  Int  cxt_lru_misses;
  Int  bbcc_clones;
};


/*------------------------------------------------------------*/
/*--- Structure declarations                               ---*/
/*------------------------------------------------------------*/

typedef struct _Context     Context;
typedef struct _CC          CC;
typedef struct _BB          BB;
typedef struct _BBCC        BBCC;
typedef struct _jCC         jCC;
typedef struct _fCC         fCC;
typedef struct _fn_node     fn_node;
typedef struct _file_node   file_node;
typedef struct _obj_node    obj_node;
typedef struct _fn_config   fn_config;
typedef struct _call_entry  call_entry;
typedef struct _thread_info thread_info;

/* Costs of event sets. Aliases to arrays of 64-bit values */
typedef ULong* SimCost;  /* All events the simulator can produce */
typedef ULong* UserCost;
typedef ULong* FullCost; /* Simulator + User */


/* The types of control flow changes that can happen between
 * execution of two BBs in a thread.
 */
typedef enum {
  jk_None = 0,   /* no explicit change by a guest instruction */
  jk_Jump,       /* regular jump */
  jk_Call,
  jk_Return,
  jk_CondJump    /* conditional jump taken (only used as jCC type) */
} TgJumpKind;


/* JmpCall cost center
 * for subroutine call (from->bb->jmp_addr => to->bb->addr)
 *
 * Each BB has at most one CALL instruction. The list of JCC from
 * this call is a pointer to the list head (stored in BBCC), and
 * <next_from> in the JCC struct.
 *
 * For fast lookup, JCCs are reachable with a hash table, keyed by
 * the (from_bbcc,to) pair. <next_hash> is used for the JCC chain
 * of one hash table entry.
 *
 * Cost <sum> holds event counts for already returned executions.
 * <last> are the event counters at last enter of the subroutine.
 * <sum> is updated on returning from the subroutine by
 * adding the diff of <last> and current event counters to <sum>.
 *
 * After updating, <last> is set to current event counters. Thus,
 * events are not counted twice for recursive calls (TODO: True?)
 */

struct _jCC {
  TgJumpKind jmpkind; /* jk_Call, jk_Jump, jk_CondJump */
  jCC* next_hash;   /* for hash entry chain */
  jCC* next_from;   /* next JCC from a BBCC */
  BBCC *from, *to;  /* call arc from/to this BBCC */
  UInt jmp;         /* jump no. in source */

  ULong call_counter; /* no wraparound with 64 bit */
  ULong creation_seq; /* creation order sequence number for correct dump order */

  FullCost cost; /* simulator + user counters */
};


/* 
 * Info for one instruction of a basic block.
 */
typedef struct _InstrInfo InstrInfo;
struct _InstrInfo {
  UInt instr_offset;
  UInt instr_size;
  UInt cost_offset;
  EventSet* eventset;
};



/*
 * Info for a side exit in a BB
 */
typedef struct _CJmpInfo CJmpInfo;
struct _CJmpInfo {
  UInt instr;          /* instruction index for BB.instr array */
  TgJumpKind jmpkind; /* jump kind when leaving BB at this side exit */
};


/**
 * An instrumented basic block (BB).
 *
 * BBs are put into a resizable hash to allow for fast detection if a
 * BB is to be retranslated but cost info is already available.
 * The key for a BB is a (object, offset) tupel making it independent
 * from possibly multiple mappings of the same ELF object.
 *
 * At the beginning of each instrumented BB,
 * a call to setup_bbcc(), specifying a pointer to the
 * according BB structure, is added.
 *
 * As cost of a BB has to be distinguished depending on the context,
 * multiple cost centers for one BB (struct BBCC) exist and the according
 * BBCC is set by setup_bbcc.
 */
struct _BB {
  obj_node*  obj;         /* ELF object of BB */
  PtrdiffT   offset;      /* offset of BB in ELF object file */
  BB*        next;       /* chaining for a hash entry */

  VgSectKind sect_kind;  /* section of this BB, e.g. PLT */
  UInt       instr_count;
  
  /* filled by TG_(get_fn_node) if debug info is available */
  fn_node*   fn;          /* debug info for this BB */
  UInt       line;
  Bool       is_entry;    /* True if this BB is a function entry */
        
  BBCC*      bbcc_list;  /* BBCCs for same BB (see next_bbcc in BBCC) */
  BBCC*      last_bbcc;  /* Temporary: Cached for faster access (LRU) */

  /* filled by TG_(instrument) if not seen before */
  UInt       cjmp_count;  /* number of side exits */
  CJmpInfo*  jmp;         /* array of info for condition jumps,
			   * allocated directly after this struct */
  Bool       cjmp_inverted; /* is last side exit actually fall through? */

  UInt       instr_len;
  UInt       cost_count;
  InstrInfo  instr[0];   /* info on instruction sizes and costs */
};



/**
 * Function context
 *
 * Basic blocks are always executed in the scope of a context.
 * A function context is a list of function nodes representing
 * the call chain to the current context: I.e. fn[0] is the
 * function we are currently in, fn[1] has called fn[0], and so on.
 * Recursion levels are used for fn[0].
 *
 * To get a unique number for a full execution context, use
 *  rec_index = min(<fn->rec_separation>,<active>) - 1;
 *  unique_no = <number> + rec_index
 *
 * For each Context, recursion index and BB, there can be a BBCC.
 */
struct _Context {
    UInt size;        // number of function dependencies
    UInt base_number; // for context compression & dump array
    Context* next;    // entry chaining for hash
    UWord hash;       // for faster lookup...
    fn_node* fn[0];
};


/*
 * Cost info for a side exits from a BB
 */
typedef struct _JmpData JmpData;
struct _JmpData {
    ULong ecounter; /* number of times the BB was left at this exit */
    jCC*  jcc_list; /* JCCs used for this exit */
};


/*
 * Basic Block Cost Center
 *
 * On demand, multiple BBCCs will be created for the same BB
 * dependent on command line options and:
 * - current function (it's possible that a BB is executed in the
 *   context of different functions, e.g. in manual assembler/PLT)
 * - current thread ID
 * - position where current function is called from
 * - recursion level of current function
 *
 * The cost centres for the instructions of a basic block are
 * stored in a contiguous array.
 * They are distinguishable by their tag field.
 */
struct _BBCC {
    BB*      bb;           /* BB for this cost center */

    Context* cxt;          /* execution context of this BBCC */
    ThreadId tid;          /* only for assertion check purpose */
    UInt     rec_index;    /* Recursion index in rec->bbcc for this bbcc */
    BBCC**   rec_array;    /* Variable sized array of pointers to 
			    * recursion BBCCs. Shared. */
    ULong    ret_counter;  /* how often returned from jccs of this bbcc;
			    * used to check if a dump for this BBCC is needed */
    
    BBCC*    next_bbcc;    /* Chain of BBCCs for same BB */
    BBCC*    lru_next_bbcc; /* BBCC executed next the last time */
    
    jCC*     lru_from_jcc; /* Temporary: Cached for faster access (LRU) */
    jCC*     lru_to_jcc;   /* Temporary: Cached for faster access (LRU) */
    FullCost skipped;      /* cost for skipped functions called from 
			    * jmp_addr. Allocated lazy */
    
    BBCC*    next;         /* entry chain in hash */
    ULong*   cost;         /* start of 64bit costs for this BBCC */
    ULong    ecounter_sum; /* execution counter for first instruction of BB */
    JmpData  jmp[0];
};


/* the <number> of fn_node, file_node and obj_node are for compressed dumping
 * and a index into the dump boolean table and fn_info_table
 */

struct _fn_node {
  HChar*     name;
  UInt       number;
  Context*   last_cxt; /* LRU info */
  Context*   pure_cxt; /* the context with only the function itself */
  file_node* file;     /* reverse mapping for 2nd hash */
  fn_node* next;

  Bool dump_before :1;
  Bool dump_after :1;
  Bool zero_before :1;
  Bool toggle_collect :1;
  Bool skip :1;
  Bool obj_skip_checked : 1;
  Bool pop_on_jump : 1;

  Bool is_malloc :1;
  Bool is_realloc :1;
  Bool is_free :1;

  Int  group;
  Int  separate_callers;
  Int  separate_recursions;
#if TG_ENABLE_DEBUG
  Int  verbosity; /* Stores old verbosity level while in function */
#endif
};

/* Quite arbitrary fixed hash sizes */

#define   N_OBJ_ENTRIES         47
#define  N_FILE_ENTRIES         53
#define    N_FN_ENTRIES         87

struct _file_node {
   HChar*     name;
   fn_node*   fns[N_FN_ENTRIES];
   UInt       number;
   obj_node*  obj;
   file_node* next;
};

/* If an object is dlopened multiple times, we hope that <name> is unique;
 * <start> and <offset> can change with each dlopen, and <start> is
 * zero when object is unmapped (possible at dump time).
 */
struct _obj_node {
   const HChar* name;
   UInt       last_slash_pos;

   Addr       start;  /* Start address of text segment mapping */
   SizeT      size;   /* Length of mapping */
   PtrdiffT   offset; /* Offset between symbol address and file offset */

   file_node* files[N_FILE_ENTRIES];
   UInt       number;
   obj_node*  next;
};

/* an entry in the callstack
 *
 * <nonskipped> is 0 if the function called is not skipped (usual case).
 * Otherwise, it is the last non-skipped BBCC. This one gets all
 * the calls to non-skipped functions and all costs in skipped 
 * instructions.
 */
struct _call_entry {
    jCC* jcc;           /* jCC for this call */
    FullCost enter_cost; /* cost event counters at entering frame */
    Addr sp;            /* stack pointer directly after call */
    Addr ret_addr;      /* address to which to return to
			 * is 0 on a simulated call */
    BBCC* nonskipped;   /* see above */
    Context* cxt;       /* context before call */
    Int fn_sp;          /* function stack index before call */
};


/*
 * Execution state of main thread or a running signal handler in
 * a thread while interrupted by another signal handler.
 * As there's no scheduling among running signal handlers of one thread,
 * we only need a subset of a full thread state:
 * - event counter
 * - collect state
 * - last BB, last jump kind, last nonskipped BB
 * - callstack pointer for sanity checking and correct unwinding
 *   after exit
 */
typedef struct _exec_state exec_state;
struct _exec_state {

  /* the signum of the handler, 0 for main thread context
   */
  Int sig;
  
  /* the old call stack pointer at entering the signal handler */
  Int orig_sp;
  
  FullCost cost;
  Bool     collect;
  Context* cxt;
  
  /* number of conditional jumps passed in last BB */
  Int   jmps_passed;
  BBCC* bbcc;      /* last BB executed */
  BBCC* nonskipped;

  Int call_stack_bottom; /* Index into fn_stack */
};

/* Global state structures */
typedef struct _bb_hash bb_hash;
struct _bb_hash {
  UInt size, entries;
  BB** table;
};

typedef struct _cxt_hash cxt_hash;
struct _cxt_hash {
  UInt size, entries;
  Context** table;
};  

/* Thread specific state structures, i.e. parts of a thread state.
 * There are variables for the current state of each part,
 * on which a thread state is copied at thread switch.
 */
typedef struct _bbcc_hash bbcc_hash;
struct _bbcc_hash {
  UInt size, entries;
  BBCC** table;
};

typedef struct _jcc_hash jcc_hash;
struct _jcc_hash {
  UInt size, entries;
  jCC** table;
  jCC* spontaneous;
};

typedef struct _fn_array fn_array;
struct _fn_array {
  UInt size;
  UInt* array;
};

typedef struct _call_stack call_stack;
struct _call_stack {
  UInt size;
  Int sp;
  call_entry* entry;
};

typedef struct _fn_stack fn_stack;
struct _fn_stack {
  UInt size;
  fn_node **bottom, **top;
};

/* The maximum number of simultaneous running signal handlers per thread.
 * This is the number of execution states storable in a thread.
 */
#define MAX_SIGHANDLERS 10

typedef struct _exec_stack exec_stack;
struct _exec_stack {
  Int sp; /* > 0 if a handler is running */
  exec_state* entry[MAX_SIGHANDLERS];
};

/* Thread State 
 *
 * This structure stores thread specific info while a thread is *not*
 * running. See function switch_thread() for save/restore on thread switch.
 *
 * If --separate-threads=no, BBCCs and JCCs can be shared by all threads, i.e.
 * only structures of thread 1 are used.
 * This involves variables fn_info_table, bbcc_table and jcc_table.
 */
struct _thread_info {

  /* state */
  fn_stack fns;       /* function stack */
  call_stack calls;   /* context call arc stack */
  exec_stack states;  /* execution states interrupted by signals */

  /* dump statistics */
  FullCost lastdump_cost;    /* Cost at last dump */
  FullCost sighandler_cost;

  /* thread specific data structure containers */
  fn_array fn_active;
  jcc_hash jccs;
  bbcc_hash bbccs;
};

/* Structs used for dumping */

/* Address position inside of a BBCC:
 * This includes
 * - the address offset from the BB start address
 * - file/line from debug info for that address (can change inside a BB)
 */
typedef struct _AddrPos AddrPos;
struct _AddrPos {
    Addr addr;
    Addr bb_addr;
    file_node* file;
    UInt line;
};

/* a simulator cost entity that can be written out in one line */
typedef struct _AddrCost AddrCost;
struct _AddrCost {
    AddrPos p;
    SimCost cost;
};

/* A function in an execution context */
typedef struct _FnPos FnPos;
struct _FnPos {
    file_node* file;
    fn_node* fn;
    obj_node* obj;
    Context* cxt;
    int rec_index;
    UInt line;
};

/*------------------------------------------------------------*/
/*--- Cache simulator interface                            ---*/
/*------------------------------------------------------------*/

struct cachesim_if
{
    void (*print_opts)(void);
    Bool (*parse_opt)(const HChar* arg);
    void (*post_clo_init)(void);
    void (*clear)(void);
    void (*dump_desc)(VgFile *fp);
    void (*printstat)(Int,Int,Int);
    void (*add_icost)(SimCost, BBCC*, InstrInfo*, ULong);
    void (*finish)(void);
    
    void (*log_1I0D)(InstrInfo*) VG_REGPARM(1);
    void (*log_2I0D)(InstrInfo*, InstrInfo*) VG_REGPARM(2);
    void (*log_3I0D)(InstrInfo*, InstrInfo*, InstrInfo*) VG_REGPARM(3);

    void (*log_1I1Dr)(InstrInfo*, Addr, Word) VG_REGPARM(3);
    void (*log_1I1Dw)(InstrInfo*, Addr, Word) VG_REGPARM(3);

    void (*log_0I1Dr)(InstrInfo*, Addr, Word) VG_REGPARM(3);
    void (*log_0I1Dw)(InstrInfo*, Addr, Word) VG_REGPARM(3);

    // function names of helpers (for debugging generated code)
    const HChar *log_1I0D_name, *log_2I0D_name, *log_3I0D_name;
    const HChar *log_1I1Dr_name, *log_1I1Dw_name;
    const HChar *log_0I1Dr_name, *log_0I1Dw_name;
};

// Event groups
#define EG_USE   0
#define EG_IR    1
#define EG_DR    2
#define EG_DW    3
#define EG_BC    4
#define EG_BI    5
#define EG_BUS   6
#define EG_ALLOC 7
#define EG_SYS   8

struct event_sets {
    EventSet *base, *full;
};

#define fullOffset(group) (TG_(sets).full->offset[group])


/*------------------------------------------------------------*/
/*--- Functions                                            ---*/
/*------------------------------------------------------------*/

/* from clo.c */

void TG_(set_clo_defaults)(void);
void TG_(update_fn_config)(fn_node*);
Bool TG_(process_cmd_line_option)(const HChar*);
void TG_(print_usage)(void);
void TG_(print_debug_usage)(void);

/* from sim.c */
void TG_(init_eventsets)(void);

/* from main.c */
Bool TG_(get_debug_info)(Addr, const HChar **dirname,
                          const HChar **filename,
                          const HChar **fn_name, UInt*, DebugInfo**);
void TG_(collectBlockInfo)(IRSB* bbIn, UInt*, UInt*, Bool*);
void TG_(set_instrument_state)(const HChar*,Bool);
void TG_(dump_profile)(const HChar* trigger,Bool only_current_thread);
void TG_(zero_all_cost)(Bool only_current_thread);
Int TG_(get_dump_counter)(void);
void TG_(fini)(Int exitcode);

/* from bb.c */
void TG_(init_bb_hash)(void);
bb_hash* TG_(get_bb_hash)(void);
BB*  TG_(get_bb)(Addr addr, IRSB* bb_in, Bool *seen_before);
void TG_(delete_bb)(Addr addr);

static __inline__ Addr bb_addr(BB* bb)
 { return bb->offset + bb->obj->offset; }
static __inline__ Addr bb_jmpaddr(BB* bb)
 { UInt off = (bb->instr_count > 0) ? bb->instr[bb->instr_count-1].instr_offset : 0;
   return off + bb->offset + bb->obj->offset; }

/* from fn.c */
void TG_(init_fn_array)(fn_array*);
void TG_(copy_current_fn_array)(fn_array* dst);
fn_array* TG_(get_current_fn_array)(void);
void TG_(set_current_fn_array)(fn_array*);
UInt* TG_(get_fn_entry)(Int n);

void      TG_(init_obj_table)(void);
obj_node* TG_(get_obj_node)(DebugInfo* si);
file_node* TG_(get_file_node)(obj_node*, const HChar *dirname,
                               const HChar* filename);
fn_node*  TG_(get_fn_node)(BB* bb);

/* from bbcc.c */
void TG_(init_bbcc_hash)(bbcc_hash* bbccs);
void TG_(copy_current_bbcc_hash)(bbcc_hash* dst);
bbcc_hash* TG_(get_current_bbcc_hash)(void);
void TG_(set_current_bbcc_hash)(bbcc_hash*);
void TG_(forall_bbccs)(void (*func)(BBCC*));
void TG_(zero_bbcc)(BBCC* bbcc);
BBCC* TG_(get_bbcc)(BB* bb);
BBCC* TG_(clone_bbcc)(BBCC* orig, Context* cxt, Int rec_index);
void TG_(setup_bbcc)(BB* bb) VG_REGPARM(1);


/* from jumps.c */
void TG_(init_jcc_hash)(jcc_hash*);
void TG_(copy_current_jcc_hash)(jcc_hash* dst);
void TG_(set_current_jcc_hash)(jcc_hash*);
jCC* TG_(get_jcc)(BBCC* from, UInt, BBCC* to);

/* from callstack.c */
void TG_(init_call_stack)(call_stack*);
void TG_(copy_current_call_stack)(call_stack* dst);
void TG_(set_current_call_stack)(call_stack*);
call_entry* TG_(get_call_entry)(Int n);

void TG_(push_call_stack)(BBCC* from, UInt jmp, BBCC* to, Addr sp, Bool skip);
void TG_(pop_call_stack)(void);
Int TG_(unwind_call_stack)(Addr sp, Int);

/* from context.c */
void TG_(init_fn_stack)(fn_stack*);
void TG_(copy_current_fn_stack)(fn_stack*);
void TG_(set_current_fn_stack)(fn_stack*);

void TG_(init_cxt_table)(void);
Context* TG_(get_cxt)(fn_node** fn);
void TG_(push_cxt)(fn_node* fn);

/* from threads.c */
void TG_(init_threads)(void);
thread_info** TG_(get_threads)(void);
thread_info* TG_(get_current_thread)(void);
void TG_(switch_thread)(ThreadId tid);
void TG_(forall_threads)(void (*func)(thread_info*));
void TG_(run_thread)(ThreadId tid);

void TG_(init_exec_state)(exec_state* es);
void TG_(init_exec_stack)(exec_stack*);
void TG_(copy_current_exec_stack)(exec_stack*);
void TG_(set_current_exec_stack)(exec_stack*);
void TG_(pre_signal)(ThreadId tid, Int sigNum, Bool alt_stack);
void TG_(post_signal)(ThreadId tid, Int sigNum);
void TG_(run_post_signal_on_call_stack_bottom)(void);

/* from dump.c */
void TG_(init_dumps)(void);

/*------------------------------------------------------------*/
/*--- Exported global variables                            ---*/
/*------------------------------------------------------------*/

extern CommandLineOptions TG_(clo);
extern Statistics TG_(stat);
extern EventMapping* TG_(dumpmap);

/* Function active counter array, indexed by function number */
extern UInt* TG_(fn_active_array);
extern Bool TG_(instrument_state);
 /* min of L1 and LL cache line sizes */
extern Int TG_(min_line_size);
extern call_stack TG_(current_call_stack);
extern fn_stack   TG_(current_fn_stack);
extern exec_state TG_(current_state);
extern ThreadId   TG_(current_tid);
extern FullCost   TG_(total_cost);
extern struct cachesim_if TG_(cachesim);
extern struct event_sets  TG_(sets);

// set by setup_bbcc at start of every BB, and needed by log_* helpers
extern Addr   TG_(bb_base);
extern ULong* TG_(cost_base);


/*------------------------------------------------------------*/
/*--- Debug output                                         ---*/
/*------------------------------------------------------------*/

#if TG_ENABLE_DEBUG

#define TG_DEBUGIF(x) \
  if (UNLIKELY( (TG_(clo).verbose >x) && \
                (TG_(stat).bb_executions >= TG_(clo).verbose_start)))

#define TG_DEBUG(x,format,args...)   \
    TG_DEBUGIF(x) {                  \
      TG_(print_bbno)();	      \
      VG_(printf)(format,##args);     \
    }

#define TG_ASSERT(cond)              \
    if (UNLIKELY(!(cond))) {          \
      TG_(print_context)();          \
      TG_(print_bbno)();	      \
      tl_assert(cond);                \
     }

#else
#define TG_DEBUGIF(x) if (0)
#define TG_DEBUG(x...) {}
#define TG_ASSERT(cond) tl_assert(cond);
#endif

/* from debug.c */
void TG_(print_bbno)(void);
void TG_(print_context)(void);
void TG_(print_jcc)(int s, jCC* jcc);
void TG_(print_bbcc)(int s, BBCC* bbcc);
void TG_(print_bbcc_fn)(BBCC* bbcc);
void TG_(print_execstate)(int s, exec_state* es);
void TG_(print_eventset)(int s, EventSet* es);
void TG_(print_cost)(int s, EventSet*, ULong* cost);
void TG_(print_bb)(int s, BB* bb);
void TG_(print_bbcc_cost)(int s, BBCC*);
void TG_(print_cxt)(int s, Context* cxt, int rec_index);
void TG_(print_short_jcc)(jCC* jcc);
void TG_(print_stackentry)(int s, int sp);
void TG_(print_addr)(Addr addr);
void TG_(print_addr_ln)(Addr addr);

void* TG_(malloc)(const HChar* cc, UWord s, const HChar* f);
void* TG_(free)(void* p, const HChar* f);
#if 0
#define TG_MALLOC(_cc,x) TG_(malloc)((_cc),x,__FUNCTION__)
#define TG_FREE(p)       TG_(free)(p,__FUNCTION__)
#else
#define TG_MALLOC(_cc,x) VG_(malloc)((_cc),x)
#define TG_FREE(p)       VG_(free)(p)
#endif

#endif /* TG_GLOBAL */
