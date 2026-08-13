/*
 * Not part of DrFX GodMode - the signal model lives in signal_model.h.
 *
 * This file exists only because "signal.h" sitting in src/ would shadow the C
 * standard library's <signal.h> (PlatformIO puts src/ on the include path, and
 * GCC searches -I directories before system ones even for angle-bracket
 * includes). Forwarding keeps the toolchain working. You can safely delete it.
 */
#pragma once

#if defined(__GNUC__)
#include_next <signal.h>
#endif
