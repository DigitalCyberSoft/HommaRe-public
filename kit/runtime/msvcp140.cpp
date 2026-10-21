// msvcp140.cpp - MSVCP140.dll, the C++ standard library's compiled part, as
// far as a game needs it.
//
// Heroes V imports only what one std::wofstream needs - the wide streambuf,
// ios and ostream members, the codecvt<wchar_t, char> facet and the locale
// plumbing behind it - and its only user is the developer console log
// ("console.txt"), written when a console switch is on. So these are minimal:
// _Fiopen declines to open a file, which leaves the stream in its failed
// state, and every member returns what keeps a failed stream consistent
// (constructors and flush return `this`, the facet converts nothing). A use
// beyond that logs once, by name, so it shows up in a run log.
//
// Calling conventions are MSVC's: members are thiscall (`this` in ECX, the
// callee pops its stack arguments), static members and free functions cdecl.
#include "imports.h"
#include "memory.h"
#include "ucrt_internal.h"

namespace {

void note(const char *what) {
    log_once(what, "msvcp140: %s (the console log stream; modelled as never opening)", what);
}
void p_return_this(X86 *c) {
    set_eax(c, c->r[R_ECX]);
}
void p_none(X86 *) {}
void p_zero(X86 *c) {
    set_eax(c, 0);
}
// std::_Fiopen(const char *name, int mode, int prot): no file, so the
// basic_filebuf stays closed and its stream sets failbit.
void p_Fiopen(X86 *c) {
    LOGW("msvcp140: _Fiopen(\"%s\") declined: the C++ file streams are not modelled",
         gm_str(arg(c, 0)).c_str());
    set_eax(c, 0);
}
// basic_ios::setstate/clear(iostate, bool reraise): the failed state is the
// only one a stream here ever has, so there is nothing to record.
void p_setstate(X86 *) {
    note("basic_ios::setstate");
}
void p_clear(X86 *) {
    note("basic_ios::clear");
}
// basic_streambuf<wchar_t>::xsputn/xsgetn/sputn(ptr, streamsize): nothing moves.
void p_count_zero64(X86 *c) {
    note("basic_streambuf transfer");
    set_eax64(c, 0);
}
// sputc returns WEOF: the closed buffer accepts nothing.
void p_weof(X86 *c) {
    set_eax(c, 0xffffu);
}
// basic_streambuf::getloc(locale *result): the result holds the global locale.
uint32_t g_locimp = 0;
uint32_t locimp() {
    if (!g_locimp)
        g_locimp = heap_alloc(0x40, true); // _Locimp with no facets and no parent
    return g_locimp;
}
void p_getloc(X86 *c) {
    uint32_t out = arg(c, 0);
    wr32(out, locimp());
    set_eax(c, out);
}
void p_getgloballocale(X86 *c) {
    set_eax(c, locimp());
}
// locale::id::operator size_t(): ids are handed out on first use, after the
// standard facets' own.
uint32_t g_next_id = 64;
void p_locale_id(X86 *c) {
    uint32_t self = c->r[R_ECX];
    if (!rd32(self))
        wr32(self, ++g_next_id);
    set_eax(c, rd32(self));
}
// codecvt<wchar_t, char, mbstate_t>::_Getcat(const facet **psave, const locale *):
// a facet object whose virtual slots all return `this`, so the stream's own
// bookkeeping (reference counts, its deleting destructor) can call them.
uint32_t g_facet = 0;
void p_facet_slot(X86 *c) {
    set_eax(c, c->r[R_ECX]);
}
void p_getcat(X86 *c) {
    uint32_t psave = arg(c, 0);
    if (!g_facet) {
        uint32_t vtable = heap_alloc(16 * 4, true);
        uint32_t slot = imports_alloc_trampoline("MSVCP140.dll", "<facet virtual>", p_facet_slot, 1);
        for (uint32_t i = 0; i < 16; ++i)
            wr32(vtable + 4 * i, slot);
        g_facet = heap_alloc(0x20, true);
        wr32(g_facet, vtable);
        wr32(g_facet + 4, 1); // _Myrefs
    }
    if (psave && !rd32(psave))
        wr32(psave, g_facet);
    set_eax(c, 2); // _X_CTYPE, the category codecvt belongs to
}
// codecvt_base::always_noconv(): true, so the stream writes wchar_t as is and
// never asks the facet to convert.
void p_always_noconv(X86 *c) {
    set_eax(c, 1);
}
// codecvt::in/out/unshift: noconv (3), nothing converted.
void p_noconv(X86 *c) {
    note("codecvt conversion");
    set_eax(c, 3);
}
void p_uncaught_exception(X86 *c) {
    set_eax(c, 0);
}
void p_xlength_error(X86 *c) {
    LOGW("msvcp140: std::_Xlength_error(\"%s\"), from %08x", gm_str(arg(c, 0)).c_str(), rd32(c->r[R_ESP]));
    ucrt_exit_process(c, 3);
}

#define P "MSVCP140.dll"
#define WSB "?$basic_streambuf@_WU?$char_traits@_W@std@@@std@@"
#define WIOS "?$basic_ios@_WU?$char_traits@_W@std@@@std@@"
#define WOS "?$basic_ostream@_WU?$char_traits@_W@std@@@std@@"
#define CVT "?$codecvt@_WDU_Mbstatet@@@std@@"
const ImportShim shims[] = {
    {P, "??0" WSB "IAE@XZ", 0, p_return_this},
    {P, "??1" WSB "UAE@XZ", 0, p_none},
    {P, "?_Init@" WSB "IAEXXZ", 0, p_none},
    {P, "?_Pninc@" WSB "IAEPA_WXZ", 0, p_zero},
    {P, "?showmanyc@" WSB "MAE_JXZ", 0, p_count_zero64},
    {P, "?xsputn@" WSB "MAE_JPB_W_J@Z", 3, p_count_zero64},
    {P, "?xsgetn@" WSB "MAE_JPA_W_J@Z", 3, p_count_zero64},
    {P, "?sputn@" WSB "QAE_JPB_W_J@Z", 3, p_count_zero64},
    {P, "?sputc@" WSB "QAEG_W@Z", 1, p_weof},
    {P, "?getloc@" WSB "QBE?AVlocale@2@XZ", 1, p_getloc},
    {P, "??0" WIOS "IAE@XZ", 0, p_return_this},
    {P, "??1" WIOS "UAE@XZ", 0, p_none},
    {P, "?setstate@" WIOS "QAEXH_N@Z", 2, p_setstate},
    {P, "?clear@" WIOS "QAEXH_N@Z", 2, p_clear},
    // basic_ostream(streambuf *, bool isstd) plus the hidden most-derived
    // flag a constructor of a class with a virtual base takes.
    {P, "??0" WOS "QAE@PAV?$basic_streambuf@_WU?$char_traits@_W@std@@@1@_N@Z", 3, p_return_this},
    {P, "??1" WOS "UAE@XZ", 0, p_none},
    {P, "?flush@" WOS "QAEAAV12@XZ", 0, p_return_this},
    {P, "?_Osfx@" WOS "QAEXXZ", 0, p_none},
    {P, "?_Getcat@" CVT "SAIPAPBVfacet@locale@2@PBV42@@Z", ARGC_CDECL, p_getcat},
    {P, "?in@" CVT "QBEHAAU_Mbstatet@@PBD1AAPBDPA_W3AAPA_W@Z", 8, p_noconv},
    {P, "?out@" CVT "QBEHAAU_Mbstatet@@PB_W1AAPB_WPAD3AAPAD@Z", 8, p_noconv},
    {P, "?unshift@" CVT "QBEHAAU_Mbstatet@@PAD1AAPAD@Z", 4, p_noconv},
    {P, "?always_noconv@codecvt_base@std@@QBE_NXZ", 0, p_always_noconv},
    {P, "?_Getgloballocale@locale@std@@CAPAV_Locimp@12@XZ", ARGC_CDECL, p_getgloballocale},
    {P, "??Bid@locale@std@@QAEIXZ", 0, p_locale_id},
    {P, "??0_Lockit@std@@QAE@H@Z", 1, p_return_this},
    {P, "??1_Lockit@std@@QAE@XZ", 0, p_none},
    {P, "?uncaught_exception@std@@YA_NXZ", ARGC_CDECL, p_uncaught_exception},
    {P, "?_Xlength_error@std@@YAXPBD@Z", ARGC_CDECL, p_xlength_error},
    {P, "?_Fiopen@std@@YAPAU_iobuf@@PBDHH@Z", ARGC_CDECL, p_Fiopen},
};
} // namespace

void msvcp140_register() {
    imports_register(shims, sizeof shims / sizeof shims[0]);
    // codecvt<wchar_t, char>::id is a variable (a locale::id, one size_t).
    imports_register_data(P, "?id@" CVT "2V0locale@2@A", 4);
}
