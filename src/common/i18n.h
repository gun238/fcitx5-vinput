#pragma once

#ifdef _WIN32
inline const char *gettext(const char *message) { return message; }
#else
#include <libintl.h>
#endif

namespace vinput::i18n {
void Init();
}

#ifndef _
#define _(String) gettext(String)
#endif
