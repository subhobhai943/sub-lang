#include "codegen_switch.h"

/* Does this subtree contain a `break` that belongs to the switch?

   The search stops at anything that captures a `break` of its own -- a loop
   or a nested switch -- because a `break` in there is not ours. */
static int has_switch_break(ASTNode *n) {
    if (!n) return 0;

    switch (n->type) {
    case AST_BREAK_STMT:
        return 1;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
    case AST_FOR_STMT:
    case AST_ARRAY_ITERATION:
    case AST_SWITCH_STMT:
    case AST_FUNCTION_DECL:
    case AST_ARROW_FUNCTION:
        return 0;
    default:
        break;
    }

    for (int i = 0; i < n->child_count; i++)
        if (has_switch_break(n->children[i])) return 1;
    if (has_switch_break(n->left))      return 1;
    if (has_switch_break(n->right))     return 1;
    if (has_switch_break(n->condition)) return 1;

    /* A block keeps its statements in children[], and only falls back to the
       ->next chain off ->body when it has none. Following both would visit
       every statement once per sibling. */
    if (n->child_count == 0)
        for (ASTNode *s = n->body; s; s = s->next)
            if (has_switch_break(s)) return 1;

    return 0;
}

int body_has_free_break(ASTNode *body) {
    if (!body) return 0;
    for (int i = 0; i < body->child_count; i++)
        if (has_switch_break(body->children[i])) return 1;
    if (body->child_count == 0)
        for (ASTNode *s = body->body; s; s = s->next)
            if (has_switch_break(s)) return 1;
    return 0;
}

int switch_clause_breaks(ASTNode *clause) {
    return clause ? body_has_free_break(clause->body) : 0;
}

ASTNode *switch_default_clause(ASTNode *sw) {
    if (!sw) return NULL;
    for (int i = 0; i < sw->child_count; i++)
        if (sw->children[i] && sw->children[i]->type == AST_DEFAULT_CLAUSE)
            return sw->children[i];
    return NULL;
}
