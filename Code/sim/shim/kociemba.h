// kociemba.h — desktop shim.
//
// The Tier-1 simulator replaces CubeSystem, so nothing ever asks for a real
// solution; VirtualCube.cpp is compiled only for its state model, and it is the
// one caller of solve(). This stub exists so that file links.
//
// The real solver is portable C++ and compiles on a host unmodified, so moving
// to it is a build-list change rather than a code change. Signature kept
// identical to Code/libraries/kociemba/kociemba.h for exactly that reason.
#ifndef KOCIEMBA_H_SIM
#define KOCIEMBA_H_SIM

namespace kociemba {
    void set_memory(void* mem479 = nullptr, void* mem248 = nullptr);
    const char* solve(const char* facelets, int maxDepth = 24, int timeOut = 10000,
                      int useSeparator = 0);
}
#endif
