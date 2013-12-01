/*////////////////////////////////////////////////////////////////////////////

                                pretty.h                               

                      Created on 18/9/13 by Chris Bak 

/////////////////////////////////////////////////////////////////////////// */

#include <glib.h> /* GHashTable, gpointer */
#include "ast.h" /* AST structure declarations */

void print_symbol(gpointer key, gpointer value, gpointer user_data);
void print_symbol_table(GHashTable *table);
int print_dot_ast(List *const gp_ast, char* file_name);
void print_location(YYLTYPE const loc);
void print_list(List * const list);
void print_declaration(GPDeclaration * const decl);
void print_statement(GPStatement * const stmt);
void print_condition(GPCondExp * const cond);
void print_atom(GPAtomicExp * const atom);
void print_procedure(GPProcedure * const proc);
void print_rule(GPRule * const rule);
void print_graph(GPGraph * const graph);
void print_node(GPNode * const node);
void print_edge(GPEdge * const edge);
void print_label(GPLabel * const label);
void print_position(GPPos * const pos);
