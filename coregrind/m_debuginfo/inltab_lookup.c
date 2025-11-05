/* VgHashTable-based implementation for inline lookup table.
   Maps addresses to inline function info indices. */

#include "pub_core_basics.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcassert.h"
#include "pub_core_mallocfree.h"
#include "pub_core_hashtable.h"
#include "priv_storage.h"

/* Node structure for VgHashTable
   Extends VgHashNode with our key (Addr) and value (Word) */
typedef struct {
   VgHashNode node;  /* Must be first for VgHashTable API */
   Word value;       /* The inline table index */
} InlLookupNode;

/* Binary search for an inline function entry containing address 'a'.
   Uses the sorted inltab array to efficiently find candidates.
   Returns the index of the matching entry, or -1 if not found.

   The inltab is sorted by addr_lo, so we:
   1. Binary search to find entries where addr_lo <= a
   2. Scan backwards to find an entry that contains address a
      (this finds the innermost inline function if multiple are nested)
*/
static Word binary_search_inltab(const DebugInfo* di, Addr a) {
   if (di == NULL || di->inltab == NULL || di->inltab_used == 0)
      return -1;

   Word lo = 0, hi = di->inltab_used;

   /* Binary search: find the first entry where addr_lo > a */
   while (lo < hi) {
      Word mid = lo + (hi - lo) / 2;
      if (a < di->inltab[mid].addr_lo) {
         hi = mid;
      } else {
         lo = mid + 1;
      }
   }

   /* lo now points to the first entry where addr_lo > a.
      Search backwards from lo-1 to find an entry that contains address a */
   for (Word i = lo - 1; i >= 0; i--) {
      if (di->inltab[i].addr_lo <= a && a < di->inltab[i].addr_hi) {
         /* Found it! Return the index */
         return i;
      }
      /* Since we're scanning backwards through sorted addr_lo entries,
         if we encounter an addr_hi <= a, we can stop as we've passed
         the potential range */
      if (di->inltab[i].addr_hi <= a) {
         break;
      }
   }

   return -1;
}

/* Public API for inline lookup table */

VgHashTable *VG_(inltab_lookup_new)(void) {
   return VG_(HT_construct)("inltab_lookup");
}

void VG_(inltab_lookup_insert)(VgHashTable *ht, Addr addr, Word inl_idx) {
   if (ht == NULL)
      ht = VG_(inltab_lookup_new)();

   /* Check if entry already exists */
   InlLookupNode *existing = VG_(HT_lookup)(ht, (UWord)addr);
   if (existing) {
      /* Update existing value */
      existing->value = inl_idx;
      return;
   }

   /* Allocate new node */
   InlLookupNode *node = VG_(malloc)("di.inltab_vghash.node", sizeof(InlLookupNode));
   if (!node)
      return;
   node->node.key = (UWord)addr;
   node->value = inl_idx;

   VG_(HT_add_node)(ht, node);
}

/* Enhanced lookup that combines hash table lookup with binary search fallback.
   First tries the hash table for exact addr_lo matches.
   If not found, uses binary search on the sorted inltab array.
   If found via binary search, caches the result in the hash table.
*/
Bool VG_(inltab_lookup_get)(VgHashTable *ht, Addr addr, Word *inl_idx,
                            const DebugInfo* di) {
   if (ht == NULL)
      ht = VG_(inltab_lookup_new)();

   /* Try hash table lookup first */
   InlLookupNode *node = VG_(HT_lookup)(ht, (UWord)addr);
   if (node) {
      *inl_idx = node->value;
      return True;
   }

   /* Hash table miss, try binary search on the inltab array */
   Word idx = binary_search_inltab(di, addr);
   if (idx >= 0) {
      /* Found via binary search, cache it for future lookups */
      *inl_idx = idx;
      VG_(inltab_lookup_insert)(ht, addr, idx);
      return True;
   }

   return False;
}

void VG_(inltab_lookup_cleanup)(VgHashTable *ht) {
   if (ht == NULL)
      return;

   /* Free all nodes */
   VG_(HT_destruct)(ht, VG_(free));
}
