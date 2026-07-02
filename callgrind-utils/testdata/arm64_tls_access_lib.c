// Shared-library half of arm64_tls_access.c. A `__thread` variable defined
// in a separate .so (rather than the main executable) can't be relaxed by
// the linker down to the cheap Local-Exec TP-relative model -- accessing it
// from the main executable forces the real TLS-descriptor path (a
// GOT-loaded {resolver, arg} pair plus `blr`), which is what exercises
// `_dl_tlsdesc_return`/`_dl_tlsdesc_undefweak`/`_dl_tlsdesc_dynamic`.
__thread int tls_counter;

__attribute__((noinline)) int touch_tls(int delta) {
    tls_counter += delta;
    return tls_counter;
}
