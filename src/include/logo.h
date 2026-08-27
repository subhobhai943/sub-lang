#ifndef SUB_LOGO_H
#define SUB_LOGO_H

/* SUB_VERSION comes from sub_compiler.h, which every includer of this header
   pulls in first. There used to be a fallback literal here for the
   standalone case; it went stale the moment the real one was bumped, and a
   banner printing a version nobody released is worse than a build error. */
#ifndef SUB_VERSION
#error "include sub_compiler.h before logo.h - SUB_VERSION lives there"
#endif

#define SUB_LOGO \
"\n" \
"  \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557\u2588\u2588\u2557   \u2588\u2588\u2557\u2588\u2588\u2588\u2588\u2588\u2588\u2557 \n" \
"  \u2588\u2588\u2554\u2550\u2550\u2550\u2550\u255d\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\n" \
"  \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d\n" \
"  \u255a\u2550\u2550\u2550\u2550\u2588\u2588\u2551\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u2557\n" \
"  \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2551\u255a\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d\n" \
"  \u255a\u2550\u2550\u2550\u2550\u2550\u2550\u255d \u255a\u2550\u2550\u2550\u2550\u2550\u255d \u255a\u2550\u2550\u2550\u2550\u2550\u255d \n" \
"\n" \
"  Simple Universal Builder  v" SUB_VERSION "\n" \
"  compile \u2022 transpile \u2022 interpret\n\n"

#endif
