#pragma once

#define SAFE_CLEANUP(ptr, deleter)                                           \
    do {                                                                     \
        auto& _p = (ptr);                                                    \
        if (_p) {                                                            \
            deleter(_p);                                                     \
            _p = nullptr;                                                    \
        }                                                                    \
    } while (0)

#define SAFE_RELEASE(p)         SAFE_CLEANUP(p, [](auto* x){ x->Release(); })
#define SAFE_FREE(p)            SAFE_CLEANUP(p, std::free)
#define SAFE_FREE_LIBRARY(h)    SAFE_CLEANUP(h, FreeLibrary)
#define SAFE_DELETE(p)          SAFE_CLEANUP(p, [](auto* x){ delete x; })
#define SAFE_DELETE_ARRAY(p)    SAFE_CLEANUP(p, [](auto* x){ delete[] x; })
#define SAFE_CLOSE_HANDLE(h)    SAFE_CLEANUP(h, [](auto x){ if (x != INVALID_HANDLE_VALUE) CloseHandle(x); })