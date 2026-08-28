/* Finding the file an `import` names.
   ----------------------------------------------------------------
   `import "math"` used to parse into a variable declaration named
   "import math" that nothing ever looked at: no file was read, no name was
   bound, and the C backend emitted `long import math.sb;` -- which does not
   compile. The standard library was six files nothing could load.

   An import is resolved and spliced at parse time: the module is read, lexed
   and parsed, and its top-level statements are inserted into the importing
   program in place of the import. The program every backend receives is flat,
   which is what the ten of them already handle -- a module's functions are
   just functions, and a module's `let` is a global like any other. Nothing
   downstream of the parser needs to know modules exist.

   Where a module is looked for, in order:

     1. next to the file doing the importing, so a program's own helpers work
        without configuration
     2. each directory in $SUB_PATH (colon-separated; semicolon on Windows)
     3. `stdlib/` beside the executable, which is how the release tarball is
        laid out
     4. `../lib/sub/stdlib` relative to the executable, for a prefix install
        that puts binaries in bin/

   A name may be written with or without the .sb extension. */
#ifndef SUB_MODULE_H
#define SUB_MODULE_H

/* Remember which file is being parsed, so imports inside it resolve relative
   to its directory. Call before parsing; pass NULL when there is no file
   (a REPL line, or source from a string), and imports fall back to the
   working directory. */
void module_set_source(const char *path);

/* The path currently being parsed, or NULL. */
const char *module_current_source(void);

/* Resolve `name` to a readable file. Returns a malloc'd path the caller
   frees, or NULL when nothing matched -- in which case `tried` (if given) is
   filled with a newline-separated list of the paths that were looked at, for
   an error message worth reading. */
char *module_resolve(const char *name, const char *importing_file,
                     char **tried);

/* Read a whole file into a malloc'd string the caller frees, or NULL. */
char *module_read(const char *path);

/* Whether this file has already been spliced into the program being parsed.
   Importing the same module twice is common and harmless -- two modules that
   both want `math` -- but splicing it twice would redeclare every function in
   it. Marks the file as seen when it was not. */
int module_already_included(const char *resolved_path);

/* Whether the file is on the current import chain, which would be a cycle. */
int module_in_progress(const char *resolved_path);
void module_push(const char *resolved_path);
void module_pop(void);

/* Forget every included and in-progress module. Call between compilations --
   the interpreter parses more than one program per run. */
void module_reset(void);

#endif
