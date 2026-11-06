/* A helper that builds its caller's stack frame and returns with SP below the
 * SP it was called with, like V8's Sparkplug prologue
 * (Builtins_BaselineOutOfLinePrologue). The return still ends the helper's
 * frame: the callees of with_prologue_* must stay under with_prologue_*. */

#include <stdio.h>

__attribute__((noinline)) int leaf_a(int x)
{
    __asm__ volatile("" : "+r"(x));
    return x + 1;
}

__attribute__((noinline)) int leaf_b(int x)
{
    __asm__ volatile("" : "+r"(x));
    return x + 2;
}

#if defined(__x86_64__)
int with_prologue_a(int x);
int with_prologue_b(int x);

/* frame_prologue pops its return address, pushes the caller's frame (saved
 * rbp and two slots), pushes the return address back and returns. The caller
 * runs its body on that frame and tears it down itself. */
__asm__(
    ".text\n"
    ".type frame_prologue, @function\n"
    "frame_prologue:\n"
    "  pop %r11\n"
    "  push %rbp\n"
    "  mov %rsp, %rbp\n"
    "  push $1\n"
    "  push $2\n"
    "  push %r11\n"
    "  ret\n"
    ".size frame_prologue, .-frame_prologue\n"

    ".globl with_prologue_a\n"
    ".type with_prologue_a, @function\n"
    "with_prologue_a:\n"
    "  call frame_prologue\n"
    "  call leaf_a\n"
    "  mov %rbp, %rsp\n"
    "  pop %rbp\n"
    "  ret\n"
    ".size with_prologue_a, .-with_prologue_a\n"

    ".globl with_prologue_b\n"
    ".type with_prologue_b, @function\n"
    "with_prologue_b:\n"
    "  call frame_prologue\n"
    "  call leaf_b\n"
    "  mov %rbp, %rsp\n"
    "  pop %rbp\n"
    "  ret\n"
    ".size with_prologue_b, .-with_prologue_b\n");

int main(void)
{
    printf("%d\n", with_prologue_a(1) + with_prologue_b(2));
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
