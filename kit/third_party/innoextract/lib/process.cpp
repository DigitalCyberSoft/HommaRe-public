// process.cpp - stands in for innoextract's src/util/process.cpp, which runs
// helper programs (unrar for --gog's extra archives) and cannot be built for
// iOS (no fork, exec or system). The library never asks for --gog.
#include "util/process.hpp"

namespace util {

int run(const char * const args[]) {
	(void)args;
	return -1;
}

} // namespace util
