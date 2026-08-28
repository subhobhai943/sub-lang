/* Shared shape of a `switch` clause, so that ten backends agree on what a
   case body means instead of each deciding for itself.

   The AST a backend receives is:

       AST_SWITCH_STMT   condition = the scrutinee
                         children  = the clauses, in source order
       AST_CASE_CLAUSE   children  = the match values (one or more)
                         body      = an AST_BLOCK of statements
       AST_DEFAULT_CLAUSE
                         body      = an AST_BLOCK of statements

   Cases do not fall through: the matching clause runs its own body and
   nothing else, and `default` runs when no case matched wherever it is
   written. So every backend can lower a switch to an if/else chain, which
   is what all of them do -- a target's own `switch` would constrain the
   scrutinee to types and to compile-time constants that SUB does not. */
#ifndef SUB_CODEGEN_SWITCH_H
#define SUB_CODEGEN_SWITCH_H

#include "../include/sub_compiler.h"

/* Whether a `break` inside this clause body still needs somewhere to jump.

   The parser drops a `break` in tail position, which is the shape nearly
   every case is written in and which means nothing once cases cannot fall
   through. What can be left is a `break` in the middle of a body, usually
   under an `if`. That one does have an effect -- skip the rest of the case
   -- and an if/else chain has no way to express it, so a backend that sees
   this wraps the body in a loop it runs once and lets `break` leave it. */
int switch_clause_breaks(ASTNode *clause);

/* The clause a switch runs when nothing matched, or NULL. */
ASTNode *switch_default_clause(ASTNode *sw);

/* Whether this body contains a `break` that nothing inside it catches -- one
   that belongs to whatever loop or switch encloses the body. A loop whose
   body has none, and whose condition is always true, never finishes. */
int body_has_free_break(ASTNode *body);

#endif
