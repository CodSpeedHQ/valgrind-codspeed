/* Shim so Python (via ctypes) can issue callgrind client requests.
   The requests themselves are inline asm and unreachable from pure
   Python; this file just wraps them in regular C functions. */

#include "../callgrind.h"

void add_obj_skip(const char* path)
{
   CALLGRIND_ADD_OBJ_SKIP(path);
}

void start_instr(void)
{
   CALLGRIND_START_INSTRUMENTATION;
}

void stop_instr(void)
{
   CALLGRIND_STOP_INSTRUMENTATION;
}
