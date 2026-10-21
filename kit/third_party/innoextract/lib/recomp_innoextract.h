/* recomp_innoextract.h - innoextract 1.9 as a library, for the iOS launcher,
 * which cannot run the Mac's innoextract helper as a separate process. The
 * same actions the Mac launcher asks the helper for (--gog-game-id, --list,
 * --extract --include /<path>...), in-process, one call at a time (calls from
 * several threads wait for each other). Built by build-ios.sh. */
#ifndef RECOMP_INNOEXTRACT_H
#define RECOMP_INNOEXTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RECOMP_INNO_OK = 0,
    RECOMP_INNO_FAILED = 1,
    RECOMP_INNO_CANCELLED = 2
};

/* One file the installer holds: its path in the install folder ("bin/x.exe",
 * '/'-separated, as the Mac helper's --list prints it). */
typedef void (*recomp_inno_file_fn)(void *ctx, const char *path);

/* Extraction so far: `current` names the file being unpacked when it changes
 * (NULL otherwise), `bytes` the bytes written in all. Called every 80 KiB or
 * so; returning 0 cancels the extraction. */
typedef int (*recomp_inno_progress_fn)(void *ctx, const char *current, uint64_t bytes);

/* The GOG.com game id of `setup` (empty when it has none) into `id`. */
int recomp_inno_gog_id(const char *setup, char *id, size_t id_size, char *error, size_t error_size);

/* Every file path the installer holds, one call each. */
int recomp_inno_list(const char *setup, recomp_inno_file_fn file, void *ctx, char *error, size_t error_size);

/* Unpacks the `count` files named by `paths` (install-folder paths, matched
 * without case like the helper's --include /<path>) from `setup` and the
 * setup-N.bin slices beside it into `output_dir`. */
int recomp_inno_extract(const char *setup, const char *output_dir, const char *const *paths, size_t count,
                        recomp_inno_progress_fn progress, void *ctx, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
