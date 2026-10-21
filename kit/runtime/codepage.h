// codepage.h - the guest's ANSI code page: what GetACP reports, what CP_ACP
// conversions use, and how ANSI file names map onto the host's UTF-8 ones.
//
// The code page is the game's (game.toml [game] ansi_codepage, 1252 unless the
// release was localised for another; RECOMP_ACP overrides it for a run). 1252
// keeps the kit's long-standing Latin-1 behaviour byte for byte; 1251 is the
// Cyrillic table a Russian release names its profiles and saves in.
#pragma once

#include <cstdint>
#include <string>

namespace win32cp {

// The ANSI code page in effect (1251 or 1252).
uint32_t acp();
// A code page argument as the conversion functions take it: CP_ACP (0) and
// CP_THREAD_ACP (3) are the ANSI page, CP_OEMCP (1) and CP_MACCP (2) Latin-1.
uint32_t resolve(uint32_t cp);
bool supported(uint32_t cp);

// One byte of an 8-bit code page as UTF-16, and back (false: not in the page).
uint16_t to_unicode(uint32_t cp, uint8_t b);
bool from_unicode(uint32_t cp, uint16_t w, uint8_t *b);

// ANSI text in the code page as UTF-8, and UTF-8 back into the code page
// ('?' for what the page cannot spell). For the Latin-1 (1252) page these are
// the identity, as they always were: guest bytes go to the host unchanged.
std::string ansi_to_utf8(const std::string &s);
std::string utf8_to_ansi(const std::string &s);

} // namespace win32cp
