// trace_funcs.cpp - RECOMP_TRACE_FUNCS: log entry into chosen guest functions.
//
// A diagnostic, not a mod: RECOMP_TRACE_FUNCS="0095d670,0095d690" installs a
// runtime-owned BEFORE hook on each listed function start (any hookable
// symbol) and prints one line per call - the function, ECX (`this` for a
// thiscall), the return address and the first three stack arguments - so a
// bring-up can see whether a code path runs at all without writing a mod.
// Each function logs its first RECOMP_TRACE_FUNCS_MAX calls (default 50) and
// then every 1000th, with its running count. An entry may add a pointer chain
// to read at entry: "0067c240:ecx.90.14" also prints the dword at
// [[ecx+0x90]+0x14] (registers eax..edi or esp; offsets in hex).
#include "mods_internal.h"
#include "../runtime/guest.h"
#include "../platform/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {
struct Traced {
    uint32_t addr;
    uint64_t calls;
    int reg = -1;                  // pop_cpu_v1 register index for the chain, -1 for none
    std::vector<uint32_t> offsets; // added before each read
};

uint32_t cpu_reg(const pop_cpu_v1 *cpu, int reg) {
    const uint32_t regs[] = {cpu->eax, cpu->ecx, cpu->edx, cpu->ebx,
                             cpu->esp, cpu->ebp, cpu->esi, cpu->edi};
    return regs[reg];
}
std::vector<Traced> g_traced;
uint64_t g_max = 50;

void on_entry(const PopModApi *, pop_cpu_v1 *cpu, PopHookInvocation *, void *user) {
    Traced &t = g_traced[(size_t)(uintptr_t)user];
    uint64_t n = ++t.calls;
    if (n > g_max && n % 1000 != 0)
        return;
    uint32_t sp = cpu->esp, ret = 0, a[3] = {0, 0, 0};
    if (gm_valid(sp, 16)) {
        ret = rd32(sp);
        for (int i = 0; i < 3; ++i)
            a[i] = rd32(sp + 4 + 4 * (uint32_t)i);
    }
    char chain[48] = "";
    if (t.reg >= 0) {
        uint32_t v = cpu_reg(cpu, t.reg);
        bool ok = true;
        for (uint32_t off : t.offsets) {
            if (!gm_valid(v + off, 4)) {
                ok = false;
                break;
            }
            v = rd32(v + off);
        }
        if (ok)
            snprintf(chain, sizeof chain, " chain=%08x (%d)", v, (int32_t)v);
        else
            snprintf(chain, sizeof chain, " chain=invalid");
    }
    fprintf(stderr,
            "[trace] %08x call %llu: ecx=%08x eax=%08x edx=%08x ret=%08x args %08x %08x %08x%s\n",
            t.addr, (unsigned long long)n, cpu->ecx, cpu->eax, cpu->edx, ret, a[0], a[1], a[2],
            chain);
}
// RECOMP_TRACE_FUNCS_RET=1: also the value each traced call returns, for
// the calls the entry line was printed for.
void on_return(const PopModApi *, pop_cpu_v1 *cpu, PopHookInvocation *, void *user) {
    const Traced &t = g_traced[(size_t)(uintptr_t)user];
    if (t.calls > g_max && t.calls % 1000 != 0)
        return;
    fprintf(stderr, "[trace] %08x call %llu returns eax=%08x edx=%08x\n", t.addr,
            (unsigned long long)t.calls, cpu->eax, cpu->edx);
}
} // namespace

bool mods_trace_funcs_init() {
    const char *spec = recomp_env("TRACE_FUNCS");
    if (!spec || !*spec)
        return true;
    if (const char *m = recomp_env("TRACE_FUNCS_MAX"))
        g_max = strtoull(m, nullptr, 10);
    std::string text = spec;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        std::string item = text.substr(start, end - start);
        start = end + 1;
        Traced t{};
        t.addr = (uint32_t)strtoul(item.c_str(), nullptr, 16);
        if (!t.addr)
            continue;
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
            static const char *const names[] = {"eax", "ecx", "edx", "ebx",
                                                "esp", "ebp", "esi", "edi"};
            std::string chain = item.substr(colon + 1);
            for (int r = 0; r < 8; ++r)
                if (chain.compare(0, 3, names[r]) == 0)
                    t.reg = r;
            size_t dot = chain.find('.');
            while (t.reg >= 0 && dot != std::string::npos) {
                size_t next = chain.find('.', dot + 1);
                t.offsets.push_back((uint32_t)strtoul(
                    chain.substr(dot + 1, next == std::string::npos ? std::string::npos
                                                                    : next - dot - 1)
                        .c_str(),
                    nullptr, 16));
                dot = next;
            }
        }
        g_traced.push_back(t);
    }
    // Installed only once the vector stops growing: each hook keeps its slot's index.
    for (size_t i = 0; i < g_traced.size(); ++i) {
        uint32_t id = 0;
        PopModStatus st = mods_hook_install_ex(MODS_OWNER_RUNTIME, g_traced[i].addr, 0, on_entry,
                                               POP_HOOK_BEFORE, POP_HOOK_NO_GAME_VIEW,
                                               (void *)(uintptr_t)i, &id);
        fprintf(stderr, "[trace] %08x: %s\n", g_traced[i].addr,
                st == POP_OK ? "tracing entries" : "not a hookable function start");
        const char *ret = recomp_env("TRACE_FUNCS_RET");
        if (st == POP_OK && ret && *ret && *ret != '0')
            mods_hook_install_ex(MODS_OWNER_RUNTIME, g_traced[i].addr, 0, on_return,
                                 POP_HOOK_AFTER, POP_HOOK_NO_GAME_VIEW, (void *)(uintptr_t)i,
                                 &id);
    }
    return true;
}
