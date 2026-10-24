#include <cstdlib>
#include <dlfcn.h>
#include <typeinfo>

#include "is-logic-error.hh"

typedef void (*cxa_throw_type)(void *, std::type_info *, void (*)(void *));

extern "C" void __cxa_throw(void * exc, std::type_info * tinfo, void (*dest)(void *))
{
    if (is_logic_error(tinfo))
        abort_on_exception(exc, tinfo);

    static auto * orig = [] {
        auto * next = (cxa_throw_type) dlsym(RTLD_NEXT, "__cxa_throw");
        // --as-needed consumers can load the C++ runtime before libnixutil.
        if (!next)
            next = (cxa_throw_type) dlsym(RTLD_DEFAULT, "__cxa_throw");
        if (!next || next == &__cxa_throw)
            abort();
        return next;
    }();

    orig(exc, tinfo, dest);

    __builtin_unreachable();
}
