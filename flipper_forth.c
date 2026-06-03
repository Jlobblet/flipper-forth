#include <lib/forth/forth.h>
#include <stdint.h>
#include <core/core_defines.h>

int32_t
flipper_forth_entry(void* p) {
    UNUSED(p);
    run();
    return 0;
}
