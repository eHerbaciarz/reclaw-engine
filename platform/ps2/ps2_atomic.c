/*
 * ReClaw PS2: atomic operations for the EE (R5900), which has no LL/SC.
 *
 * std::shared_ptr would otherwise use a mutex in every control block, and
 * on the PS2 each mutex is a kernel semaphore: with a few hundred
 * shared_ptrs alive the kernel ran out of semaphores and the game stopped
 * while building the menu. With _GLIBCXX_HAVE_ATOMIC_LOCK_POLICY the counts
 * are updated atomically instead; the few operations GCC turns into library
 * calls are made atomic here by briefly disabling interrupts (one core).
 */
#include <kernel.h>
#include <stdint.h>
#include <stdbool.h>

static inline int reclaw_lock(void) { return DIntr(); }
static inline void reclaw_unlock(int was) { if (was) EIntr(); }

__attribute__((weak)) bool __atomic_compare_exchange_4(volatile void* p, void* expected, uint32_t desired,
                                                         bool weak, int success, int failure)
{
    (void)weak; (void)success; (void)failure;
    const int s = reclaw_lock();
    volatile uint32_t* v = (volatile uint32_t*)p;
    uint32_t* e = (uint32_t*)expected;
    bool ok = *v == *e;
    if (ok) *v = desired; else *e = *v;
    reclaw_unlock(s);
    return ok;
}

__attribute__((weak)) uint32_t __atomic_fetch_add_4(volatile void* p, uint32_t val, int model)
{
    (void)model;
    const int s = reclaw_lock();
    volatile uint32_t* v = (volatile uint32_t*)p;
    uint32_t old = *v;
    *v = old + val;
    reclaw_unlock(s);
    return old;
}

__attribute__((weak)) uint32_t __atomic_add_fetch_4(volatile void* p, uint32_t val, int model)
{
    return __atomic_fetch_add_4(p, val, model) + val;
}

__attribute__((weak)) uint32_t __atomic_fetch_sub_4(volatile void* p, uint32_t val, int model)
{
    return __atomic_fetch_add_4(p, (uint32_t)0 - val, model);
}

__attribute__((weak)) uint32_t __atomic_sub_fetch_4(volatile void* p, uint32_t val, int model)
{
    return __atomic_fetch_add_4(p, (uint32_t)0 - val, model) - val;
}

__attribute__((weak)) uint32_t __atomic_exchange_4(volatile void* p, uint32_t val, int model)
{
    (void)model;
    const int s = reclaw_lock();
    volatile uint32_t* v = (volatile uint32_t*)p;
    uint32_t old = *v;
    *v = val;
    reclaw_unlock(s);
    return old;
}

__attribute__((weak)) uint32_t __atomic_load_4(const volatile void* p, int model)
{
    (void)model;
    return *(const volatile uint32_t*)p;
}

__attribute__((weak)) void __atomic_store_4(volatile void* p, uint32_t val, int model)
{
    (void)model;
    *(volatile uint32_t*)p = val;
}

__attribute__((weak)) uint32_t __sync_val_compare_and_swap_4(volatile void* p, uint32_t oldval, uint32_t newval)
{
    const int s = reclaw_lock();
    volatile uint32_t* v = (volatile uint32_t*)p;
    uint32_t cur = *v;
    if (cur == oldval) *v = newval;
    reclaw_unlock(s);
    return cur;
}

__attribute__((weak)) bool __sync_bool_compare_and_swap_4(volatile void* p, uint32_t oldval, uint32_t newval)
{
    return __sync_val_compare_and_swap_4(p, oldval, newval) == oldval;
}

__attribute__((weak)) uint32_t __sync_fetch_and_add_4(volatile void* p, uint32_t val)
{
    return __atomic_fetch_add_4(p, val, 0);
}

__attribute__((weak)) uint32_t __sync_add_and_fetch_4(volatile void* p, uint32_t val)
{
    return __atomic_fetch_add_4(p, val, 0) + val;
}

__attribute__((weak)) uint32_t __sync_lock_test_and_set_4(volatile void* p, uint32_t val)
{
    return __atomic_exchange_4(p, val, 0);
}

__attribute__((weak)) void __sync_synchronize(void)
{
}
