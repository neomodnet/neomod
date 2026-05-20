#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
// Macros to mark strings for translation (and get translated version)

#include "config.h"
#ifdef HAVE_LIBINTL

#define _INTL_REDIRECT_MACROS
#include <libintl.h>
#include <locale.h>

#define _(String) gettext(String)

// You could edit the tformat macro to be a template function, and use compile-time checks
// for the syntax of the format string, but it's probably not worth the overhead.
#define tformat(String, args...) fmt::format(fmt::runtime(gettext(String)), args)

#else

// Building without libintl/gettext: fall back to just returning the english string
#define _(String) (String)
#define tformat(args...) fmt::format(args)

#endif  // HAVE_LIBINTL
