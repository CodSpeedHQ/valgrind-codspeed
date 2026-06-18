/*--------------------------------------------------------------------*/
/*--- Capstone bridge for cycle estimation: handle + libc shims.   ---*/
/*---                                        cycledecode_capstone.c ---*/
/*--------------------------------------------------------------------*/

/* CodSpeed extension. See cycledecode_capstone.h.
 *
 * This translation unit owns the Capstone handle and the foreign-library ABI
 * hack that lets Capstone link into a -nodefaultlibs Valgrind tool: the plain
 * libc symbols Capstone references are provided here by forwarding to
 * coregrind's own freestanding libc (the vgPlain_* functions, already linked
 * into every tool; coregrind/m_main.c does the same for memcpy/memset/memmove).
 */

#include "cycledecode_capstone.h"

#if defined(CLG_WITH_CAPSTONE)

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------*/
/*--- libc for Capstone: forward to Valgrind's own libc     -*/
/*------------------------------------------------------------*/

/* The tool links -nodefaultlibs, so the plain libc names Capstone references
 * must be provided in-tree. Rather than reimplement them, forward to
 * coregrind's freestanding libc (the vgPlain_* symbols, linked into every
 * tool); coregrind/m_main.c forwards memcpy/memmove/memset the same way.
 *
 * The pub_tool_* headers are deliberately not included here (to avoid clashes
 * with Capstone's types), so the VG_() macro is unavailable and the symbols
 * are named directly: VG_(x) expands to vgPlain_##x. On the LP64 targets this
 * tool builds for, char==HChar, unsigned long==SizeT==size_t, int==Int and
 * unsigned int==UInt, so these prototypes are ABI-identical to coregrind's
 * definitions and resolve intra-binary at link time. */

extern void* vgPlain_malloc(const char* cc, unsigned long n);
extern void*
vgPlain_calloc(const char* cc, unsigned long n, unsigned long elem);
extern void* vgPlain_realloc(const char* cc, void* p, unsigned long n);
extern void  vgPlain_free(void* p);
extern unsigned int
vgPlain_vsnprintf(char* buf, int size, const char* fmt, va_list ap);
extern unsigned int  vgPlain_vsprintf(char* buf, const char* fmt, va_list ap);
extern unsigned int  vgPlain_vprintf(const char* fmt, va_list ap);
extern unsigned long vgPlain_strlen(const char* s);
extern int           vgPlain_strcmp(const char* a, const char* b);
extern int   vgPlain_strncmp(const char* a, const char* b, unsigned long n);
extern char* vgPlain_strcpy(char* d, const char* s);
extern char* vgPlain_strncpy(char* d, const char* s, unsigned long n);
extern char* vgPlain_strchr(const char* s, char c);
extern char* vgPlain_strrchr(const char* s, char c);
extern char* vgPlain_strstr(const char* h, const char* n);

static const char* const CLG_CD_CC = "clg.cycledecode";

void* malloc(size_t n) { return vgPlain_malloc(CLG_CD_CC, n); }
void* calloc(size_t nmemb, size_t sz)
{
   return vgPlain_calloc(CLG_CD_CC, nmemb, sz);
}
void* realloc(void* p, size_t n) { return vgPlain_realloc(CLG_CD_CC, p, n); }
void  free(void* p) { vgPlain_free(p); }

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap)
{
   return (int)vgPlain_vsnprintf(buf, size, fmt, ap);
}
int sprintf(char* buf, const char* fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   int r = (int)vgPlain_vsprintf(buf, fmt, ap);
   va_end(ap);
   return r;
}

/* Capstone calls printf/puts only on internal fatal-error paths; forward them
 * to the Valgrind log so the failure is visible rather than swallowed. */
int printf(const char* fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   unsigned int r = vgPlain_vprintf(fmt, ap);
   va_end(ap);
   return (int)r;
}
int puts(const char* s) { return printf("%s\n", s); }

/* Capstone's SStream references stderr/fwrite on a buffer-overflow guard in the
 * op_str text path (which this code never reads). Valgrind has no FILE* layer,
 * so stderr is a sentinel and fwrite routes the bytes to the Valgrind log fd,
 * making such an overflow visible rather than swallowed. */
extern int vgPlain_write(int fd, const void* buf, int count);
FILE*      stderr = 0;
size_t     fwrite(const void* p, size_t size, size_t nmemb, FILE* f)
{
   (void)f;
   vgPlain_write(2, p, (int)(size * nmemb));
   return nmemb;
}

size_t strlen(const char* s) { return vgPlain_strlen(s); }
int    strcmp(const char* a, const char* b) { return vgPlain_strcmp(a, b); }
int    strncmp(const char* a, const char* b, size_t n)
{
   return vgPlain_strncmp(a, b, n);
}
char* strcpy(char* d, const char* s) { return vgPlain_strcpy(d, s); }
char* strncpy(char* d, const char* s, size_t n)
{
   return vgPlain_strncpy(d, s, n);
}
char* strchr(const char* s, int c) { return vgPlain_strchr(s, (char)c); }
char* strrchr(const char* s, int c) { return vgPlain_strrchr(s, (char)c); }
char* strstr(const char* h, const char* n) { return vgPlain_strstr(h, n); }

/*------------------------------------------------------------*/
/*--- Capstone handle: open / decode                        -*/
/*------------------------------------------------------------*/

static csh      handle;
static cs_insn* insn;

ClgCdInit clg_cs_bridge_init(void)
{
   static const cs_opt_mem mem = {malloc, calloc, realloc, free, vsnprintf};

   if (cs_option(0, CS_OPT_MEM, (size_t)&mem) != CS_ERR_OK)
      return CLG_CD_ERR_MEM_OPT;
   if (cs_open(CLG_CS_ARCH, CLG_CS_MODE, &handle) != CS_ERR_OK)
      return CLG_CD_ERR_OPEN;
   if (cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK)
      return CLG_CD_ERR_DETAIL;
   insn = cs_malloc(handle);
   if (!insn)
      return CLG_CD_ERR_INSN_ALLOC;
   return CLG_CD_OK;
}

const cs_insn* clg_cs_bridge_decode(const unsigned char* bytes, unsigned len)
{
   const uint8_t* code = (const uint8_t*)bytes;
   size_t         size = len;
   uint64_t       addr = 0;

   if (!cs_disasm_iter(handle, &code, &size, &addr, insn))
      return 0;
   return insn;
}

#endif /* CLG_WITH_CAPSTONE */
