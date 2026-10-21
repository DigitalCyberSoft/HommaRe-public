// vcruntime.cpp - VCRUNTIME140.dll: the compiler's own runtime support for a
// Visual C++ 2015+ program - the memory and string intrinsics it calls out
// of line, RTTI (typeid, dynamic_cast, type_info), std::exception's message
// storage, and the C++ exception entry points.
//
// RTTI follows the MSVC x86 layout: an object's vftable[-1] is its
// RTTICompleteObjectLocator {signature, offset, cdOffset, TypeDescriptor *,
// ClassHierarchyDescriptor *}; a TypeDescriptor is {vftable, spare, char
// name[]} and two are the same type when their decorated names are equal.
//
// Exceptions: _CxxThrowException raises 0xE06D7363 through the kit's SEH
// dispatch, as the real one calls RaiseException. The frame handlers
// (__CxxFrameHandler3, _except_handler3/4) do not yet run catch blocks or
// __except filters: they decline, so a throw ends the run with the kit's
// unhandled-exception report naming the thrown type. That is enough to learn
// whether the game throws during normal play before modelling MSVC's
// FuncInfo/unwind-map machinery.
#include "imports.h"
#include "memory.h"
#include "seh.h"
#include "ucrt_internal.h"
#include "win32.h"
#include <string>

namespace {

// -------------------------------------------------- memory and strings --
void v_memcpy(X86 *c) {
    uint32_t d = arg(c, 0), s = arg(c, 1), n = arg(c, 2);
    if (n && gm_valid(d, n) && gm_valid(s, n))
        memmove(gm_ptr(d), gm_ptr(s), n); // overlapping memcpy behaves as memmove on Windows too
    recomp_track_range(d, n);
    set_eax(c, d);
}
void v_memset(X86 *c) {
    uint32_t d = arg(c, 0), n = arg(c, 2);
    if (n && gm_valid(d, n))
        memset(gm_ptr(d), (int)(arg(c, 1) & 0xff), n);
    recomp_track_range(d, n);
    set_eax(c, d);
}
void v_memchr(X86 *c) {
    uint32_t s = arg(c, 0), n = arg(c, 2);
    uint8_t ch = (uint8_t)arg(c, 1);
    for (uint32_t i = 0; i < n; ++i)
        if (rd8(s + i) == ch) {
            set_eax(c, s + i);
            return;
        }
    set_eax(c, 0);
}
void v_strchr(X86 *c) {
    uint32_t s = arg(c, 0);
    uint8_t ch = (uint8_t)arg(c, 1);
    for (;; ++s) {
        uint8_t v = rd8(s);
        if (v == ch) {
            set_eax(c, s);
            return;
        }
        if (!v)
            break;
    }
    set_eax(c, 0);
}
void v_strrchr(X86 *c) {
    uint32_t s = arg(c, 0), found = 0;
    uint8_t ch = (uint8_t)arg(c, 1);
    for (;; ++s) {
        uint8_t v = rd8(s);
        if (v == ch)
            found = s;
        if (!v)
            break;
    }
    set_eax(c, found);
}
void v_strstr(X86 *c) {
    uint32_t s = arg(c, 0);
    std::string needle = gm_str(arg(c, 1));
    if (needle.empty()) {
        set_eax(c, s);
        return;
    }
    for (; rd8(s); ++s) {
        size_t i = 0;
        while (i < needle.size() && rd8(s + (uint32_t)i) == (uint8_t)needle[i])
            ++i;
        if (i == needle.size()) {
            set_eax(c, s);
            return;
        }
    }
    set_eax(c, 0);
}

// ---------------------------------------------------------------- RTTI --
// A TypeDescriptor's decorated name (".?AVFoo@@") starts at +8.
std::string type_name(uint32_t td) {
    return td ? gm_str(td + 8, 1024) : std::string();
}
// Type descriptors from different modules are equal by their decorated names.
// Compared in place: building both names as strings for every base class
// descriptor a dynamic_cast walks cost ~3% of the game thread on a busy map.
bool same_type(uint32_t a, uint32_t b) {
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    for (uint32_t i = 0; i < 1024; ++i) {
        const uint64_t pa = (uint64_t)a + 8 + i, pb = (uint64_t)b + 8 + i;
        const uint8_t ca = pa < GUEST_SIZE ? g_mem[pa] : 0, cb = pb < GUEST_SIZE ? g_mem[pb] : 0;
        if (ca != cb)
            return false;
        if (!ca)
            return true;
    }
    return true;
}
// The complete object locator of a polymorphic object, or 0.
uint32_t locator_of(uint32_t obj) {
    if (!obj || !gm_valid(obj, 4))
        return 0;
    uint32_t vft = rd32(obj);
    if (!vft || !gm_valid(vft - 4, 4))
        return 0;
    uint32_t col = rd32(vft - 4);
    return col && gm_valid(col, 20) ? col : 0;
}
// The start of the complete object `obj` is a subobject of.
uint32_t complete_object(uint32_t obj, uint32_t col) {
    uint32_t base = obj - rd32(col + 4);
    uint32_t cd = rd32(col + 8);
    if (cd)
        base -= rd32(obj - cd); // a vtordisp: the subobject is reached through a virtual base
    return base;
}
// A base class descriptor's PMD applied to the complete object.
uint32_t apply_pmd(uint32_t complete, uint32_t bcd) {
    int32_t mdisp = (int32_t)rd32(bcd + 8), pdisp = (int32_t)rd32(bcd + 12),
            vdisp = (int32_t)rd32(bcd + 16);
    uint32_t p = complete;
    if (pdisp >= 0)
        p += (uint32_t)((int32_t)rd32(rd32(complete + (uint32_t)pdisp) + (uint32_t)vdisp) + pdisp);
    return p + (uint32_t)mdisp;
}

// Whether `src` is a public, unambiguous base of the class a base class
// descriptor names, by that class's own hierarchy (BCD_HASPCHD: its class
// hierarchy descriptor follows the attributes). Without one, assume it is.
bool downcast_visible(uint32_t bcd, uint32_t src) {
    if (!(rd32(bcd + 20) & 0x40)) // BCD_HASPCHD
        return true;
    uint32_t chd = rd32(bcd + 24);
    if (!chd || !gm_valid(chd, 16))
        return true;
    uint32_t count = rd32(chd + 8), bases = rd32(chd + 12);
    for (uint32_t i = 1; i < count && i < 4096; ++i) {
        uint32_t sub = rd32(bases + 4 * i);
        if (same_type(rd32(sub), src))
            return !(rd32(sub + 20) & 0x3); // BCD_NOTVISIBLE | BCD_AMBIGUOUS
    }
    return false;
}

void v_RTtypeid(X86 *c) {
    uint32_t col = locator_of(arg(c, 0));
    if (!col)
        LOGW("vcruntime: typeid of %08x, which has no RTTI (std::bad_typeid)", arg(c, 0));
    set_eax(c, col ? rd32(col + 12) : 0);
}
// __RTDynamicCast(inptr, VfDelta, SrcType, TargetType, isReference)
void v_RTDynamicCast(X86 *c) {
    uint32_t in = arg(c, 0), target = arg(c, 3);
    bool is_ref = arg(c, 4) != 0;
    if (!in) {
        set_eax(c, 0);
        return;
    }
    // As vcruntime's own: the locator and the complete object come from the
    // vfptr at `inptr` itself; VfDelta only moves the source pointer, which
    // the single and multiple inheritance searches below do not need.
    uint32_t obj = in;
    uint32_t col = locator_of(obj);
    uint32_t result = 0;
    if (col) {
        uint32_t complete = complete_object(obj, col);
        uint32_t chd = rd32(col + 16);
        uint32_t count = rd32(chd + 8), bases = rd32(chd + 12);
        // Base class descriptors: {TypeDescriptor *, numContainedBases,
        // PMD {mdisp, pdisp, vdisp}, attributes}. Entry 0 is the class itself;
        // entries i+1..i+numContainedBases are the bases of entry i.
        // A downcast first: the source subobject is a base of exactly one
        // target object, and a public one. The target itself may be a private
        // or protected base of the complete object: HoMM V's CObjectivesSystem
        // has its IObjectivesCheckingCallback so, and a save restores the
        // pointer to it by a cast from the shared CObjectBase.
        const uint32_t src = arg(c, 2), src_at = in - arg(c, 1);
        for (uint32_t i = 0; src && i < count && i < 4096; ++i) {
            uint32_t bcd = rd32(bases + 4 * i);
            if (!same_type(rd32(bcd), target))
                continue;
            uint32_t contained = rd32(bcd + 4);
            for (uint32_t j = 1; j <= contained && i + j < count; ++j) {
                uint32_t sub = rd32(bases + 4 * (i + j));
                if (!same_type(rd32(sub), src) || apply_pmd(complete, sub) != src_at ||
                    !downcast_visible(bcd, src))
                    continue;
                uint32_t found = apply_pmd(complete, bcd);
                if (result && result != found) {
                    result = 0; // the source is in more than one target: ambiguous
                    i = count;
                    break;
                }
                result = found;
            }
        }
        // Otherwise a cross-cast: a public, unambiguous target in the complete object.
        for (uint32_t i = 0; !result && i < count && i < 4096; ++i) {
            uint32_t bcd = rd32(bases + 4 * i);
            if (!same_type(rd32(bcd), target))
                continue;
            uint32_t attributes = rd32(bcd + 20);
            if (attributes & 0x3) // BCD_NOTVISIBLE | BCD_AMBIGUOUS
                continue;
            result = apply_pmd(complete, bcd);
            break;
        }
    }
    if (!result && is_ref)
        LOGW("vcruntime: dynamic_cast<%s&> of %08x failed (std::bad_cast not modelled)",
             type_name(target).c_str(), in);
    if (!result && log_level() >= 2) {
        // Once per call site, object type and target: a null cast is often
        // right, and this is how to tell a right one from a wrong one.
        std::string have = col ? type_name(rd32(col + 12)) : std::string("<no RTTI>");
        char key[600];
        snprintf(key, sizeof key, "rtdc %08x %s %s", rd32(c->r[R_ESP]), have.c_str(),
                 type_name(target).c_str());
        log_once(key, "vcruntime: dynamic_cast at %08x of a %s (vfdelta %d) to %s: null",
                 rd32(c->r[R_ESP]), have.c_str(), (int32_t)arg(c, 1), type_name(target).c_str());
    }
    set_eax(c, result);
}

// __std_type_info_data { const char *_UndecoratedName; char _DecoratedName[]; }
// sits 4 bytes into a type_info (after its vftable).
void v_type_info_compare(X86 *c) {
    uint32_t a = arg(c, 0), b = arg(c, 1);
    if (a == b) {
        set_eax(c, 0);
        return;
    }
    std::string x = gm_str(a + 5, 1024), y = gm_str(b + 5, 1024); // past the '.'
    int r = x.compare(y);
    set_eax(c, (uint32_t)(r < 0 ? -1 : r > 0 ? 1 : 0));
}
// An undecorated spelling of ".?AVName@Outer@@" -> "class Outer::Name". Names
// with template arguments keep their decorated form: the game only prints them.
std::string undecorate(const std::string &d) {
    if (d.size() < 6 || d.compare(0, 3, ".?A") != 0)
        return d;
    const char *kind = d[3] == 'V' ? "class " : d[3] == 'U' ? "struct " : d[3] == 'W' ? "enum " : "";
    std::string body = d.substr(4);
    if (body.size() < 2 || body.compare(body.size() - 2, 2, "@@") != 0 || body.find('?') != std::string::npos)
        return d;
    body.resize(body.size() - 2);
    std::string out;
    size_t end = body.size();
    while (end > 0) {
        size_t at = body.rfind('@', end - 1);
        size_t start = at == std::string::npos ? 0 : at + 1;
        if (!out.empty())
            out += "::";
        out += body.substr(start, end - start);
        if (at == std::string::npos)
            break;
        end = at;
    }
    return kind + out;
}
void v_type_info_name(X86 *c) {
    uint32_t data = arg(c, 0);
    uint32_t cached = rd32(data);
    if (!cached) {
        std::string name = undecorate(gm_str(data + 4, 1024));
        cached = heap_alloc((uint32_t)name.size() + 1);
        if (cached) {
            gm_put_str(cached, name.c_str(), (uint32_t)name.size() + 1);
            wr32(data, cached);
        }
    }
    set_eax(c, cached);
}

// __std_exception_data { const char *_What; bool _DoFree; }
void v_exception_copy(X86 *c) {
    uint32_t from = arg(c, 0), to = arg(c, 1);
    uint32_t what = rd32(from);
    bool owned = rd8(from + 4) != 0;
    if (owned && what) {
        std::string text = gm_str(what);
        uint32_t copy = heap_alloc((uint32_t)text.size() + 1);
        if (copy) {
            gm_put_str(copy, text.c_str(), (uint32_t)text.size() + 1);
            wr32(to, copy);
            wr8(to + 4, 1);
            return;
        }
    }
    wr32(to, what);
    wr8(to + 4, 0);
}
void v_exception_destroy(X86 *c) {
    uint32_t data = arg(c, 0);
    if (rd8(data + 4))
        heap_free(rd32(data));
    wr32(data, 0);
    wr8(data + 4, 0);
}

// ---------------------------------------------------------- exceptions --
[[noreturn]] void fatal(X86 *c, const char *what, uint32_t code) {
    LOGW("vcruntime: %s (return address %08x)", what, rd32(c->r[R_ESP]));
    ucrt_exit_process(c, code);
}
void v_purecall(X86 *c) {
    fatal(c, "pure virtual function call", 0xc0000025u);
}
void v_std_terminate(X86 *c) {
    fatal(c, "std::terminate", 3);
}
// _CxxThrowException(object, ThrowInfo *): RaiseException(0xE06D7363,
// EXCEPTION_NONCONTINUABLE, 3, {0x19930520, object, ThrowInfo}).
void v_CxxThrowException(X86 *c) {
    uint32_t object = arg(c, 0), info = arg(c, 1);
    uint32_t params = heap_alloc(12);
    wr32(params, 0x19930520u);
    wr32(params + 4, object);
    wr32(params + 8, info);
    std::string what = win32_describe_cxx_throw(0xe06d7363u, 3, params);
    LOGW("vcruntime: C++ throw from %08x: %s", rd32(c->r[R_ESP]), what.c_str());
    if (recomp_seh_raise(c, 0xe06d7363u, 1, 3, params))
        return;
    fatal(c, "unhandled C++ exception", 0xe06d7363u);
}
// The frame handlers decline (ExceptionContinueSearch), so dispatch walks on.
void v_frame_handler_decline(X86 *c) {
    uint32_t record = arg(c, 0);
    uint32_t code = record && gm_valid(record, 4) ? rd32(record) : 0;
    log_once("vcruntime-frame-handler", "vcruntime: a C++/SEH frame handler declined exception %08x "
             "(catch blocks and __except filters are not modelled yet)", code);
    set_eax(c, 1);
}
// _except_handler4_common(cookie, check, record, frame, context, dispatcher)
void v_except_handler4_common(X86 *c) {
    uint32_t record = arg(c, 2);
    uint32_t code = record && gm_valid(record, 4) ? rd32(record) : 0;
    log_once("vcruntime-eh4", "vcruntime: an __except frame declined exception %08x "
             "(__except filters are not modelled yet)", code);
    set_eax(c, 1);
}
uint32_t g_current_exception = 0;
void v_current_exception(X86 *c) {
    if (!g_current_exception)
        g_current_exception = heap_alloc(8, true);
    set_eax(c, g_current_exception);
}
void v_current_exception_context(X86 *c) {
    if (!g_current_exception)
        g_current_exception = heap_alloc(8, true);
    set_eax(c, g_current_exception + 4);
}

#define VC "VCRUNTIME140.dll"
const ImportShim shims[] = {
    {VC, "memcpy", ARGC_CDECL, v_memcpy},
    {VC, "memmove", ARGC_CDECL, v_memcpy},
    {VC, "memset", ARGC_CDECL, v_memset},
    {VC, "memchr", ARGC_CDECL, v_memchr},
    {VC, "strchr", ARGC_CDECL, v_strchr},
    {VC, "strrchr", ARGC_CDECL, v_strrchr},
    {VC, "strstr", ARGC_CDECL, v_strstr},
    {VC, "__RTtypeid", ARGC_CDECL, v_RTtypeid},
    {VC, "__RTDynamicCast", ARGC_CDECL, v_RTDynamicCast},
    {VC, "__std_type_info_compare", ARGC_CDECL, v_type_info_compare},
    {VC, "__std_type_info_name", ARGC_CDECL, v_type_info_name},
    {VC, "__std_exception_copy", ARGC_CDECL, v_exception_copy},
    {VC, "__std_exception_destroy", ARGC_CDECL, v_exception_destroy},
    {VC, "_purecall", ARGC_CDECL, v_purecall},
    {VC, "__std_terminate", ARGC_CDECL, v_std_terminate},
    {VC, "_CxxThrowException", 2, v_CxxThrowException},
    {VC, "__CxxFrameHandler3", ARGC_CDECL, v_frame_handler_decline},
    {VC, "_except_handler3", ARGC_CDECL, v_frame_handler_decline},
    {VC, "_except_handler4_common", ARGC_CDECL, v_except_handler4_common},
    {VC, "__current_exception", ARGC_CDECL, v_current_exception},
    {VC, "__current_exception_context", ARGC_CDECL, v_current_exception_context},
};
} // namespace

void vcruntime_register() {
    imports_register(shims, sizeof shims / sizeof shims[0]);
}
