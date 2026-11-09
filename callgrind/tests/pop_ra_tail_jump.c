/* A callee that pops its return address, pushes an argument and the return
 * address back, then tail-jumps to the real target, like V8's
 * Builtins_InterpreterPushUndefinedAndArgsThenCall. With --pop-on-jump the
 * target must stay under the caller: the SP rise already ended the callee's
 * frame, so the jump has no frame of its own to replace. */

#include <stdio.h>

__attribute__((noinline)) int leaf(int x)
{
    __asm__ volatile("" : "+r"(x));
    return x + 1;
}

#if defined(__x86_64__)
int caller(int x);

/* The taken branch after the pop starts a new block, so callgrind sees SP
 * above the callee's frame before the jump, as with V8's argument loop.
 * target drops the pushed argument on return, as V8 callees do. */
__asm__(
    ".text\n"
    ".type push_arg_then_jump, @function\n"
    "push_arg_then_jump:\n"
    "  pop %r11\n"
    "  test %rsp, %rsp\n"
    "  jnz 1f\n"
    "  ud2\n"
    "1:\n"
    "  push $0\n"
    "  push %r11\n"
    "  jmp target\n"
    ".size push_arg_then_jump, .-push_arg_then_jump\n"

    ".type target, @function\n"
    "target:\n"
    "  sub $8, %rsp\n"
    "  call leaf\n"
    "  add $8, %rsp\n"
    "  ret $8\n"
    ".size target, .-target\n"

    ".globl caller\n"
    ".type caller, @function\n"
    "caller:\n"
    "  sub $8, %rsp\n"
    "  call push_arg_then_jump\n"
    "  add $8, %rsp\n"
    "  ret\n"
    ".size caller, .-caller\n");

int main(void)
{
    printf("%d\n", caller(1));
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
