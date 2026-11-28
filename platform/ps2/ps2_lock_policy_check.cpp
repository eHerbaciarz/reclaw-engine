// ReClaw PS2: std::shared_ptr must not use a mutex (= a kernel semaphore) in
// every control block (see ps2_atomic.c).
#include <memory>
static_assert(__gnu_cxx::__default_lock_policy == __gnu_cxx::_S_atomic,
              "PS2: shared_ptr would use a kernel semaphore per object");
