#ifndef __PRIV_INLTAB_LOOKUP_H
#define __PRIV_INLTAB_LOOKUP_H

#include "pub_core_basics.h"
#include "pub_tool_hashtable.h"

extern VgHashTable *VG_(inltab_lookup_new)(void);
extern void VG_(inltab_lookup_insert)(VgHashTable *ht, Addr addr, Word inl_idx);
extern Bool VG_(inltab_lookup_get)(VgHashTable *ht, Addr addr, Word *inl_idx,
                                   const DebugInfo *di);
extern void VG_(inltab_lookup_cleanup)(VgHashTable *ht);

#endif