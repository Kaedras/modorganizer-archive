#pragma once

#ifdef __unix__
#include "linux/fileio.h"
#else
#include "win32/fileio.h"
#endif
