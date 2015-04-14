/* ///////////////////////////////////////////////////////////////////////////

  ==========
  AST Module
  ==========
                             
  Module for creating and processing GP2's abstract syntax tree. Contains enum 
  type definitions, AST node definitions, prototypes for AST constructors and 
  prototypes for AST freeing functions.

/////////////////////////////////////////////////////////////////////////// */

#ifndef INC_AST_H
#define INC_AST_H 

#include "error.h"
#include "globals.h"

/* The functions after each struct definition are AST node constructors. The
 * constructors are called from the Bison parser (gpparser.y) which provides 
 * the appropriate arguments from the semantic values and locations of symbols
 * in the rules it reduces. The functions assign the passed pointers to the
 * corresponding structure fields. 
 *
 * Strings, such as rule names and variable names, are dynamically allocated 
 * with strdup. This is because the pointer passed to the function is freed in 
 * gpparser.y immediately after the constructor call. Therefore a new allocation
 * is required to prevent a double free.
 */ 

/* Definition of AST list nodes. */
typedef enum {GLOBAL_DECLARATIONS = 0, LOCAL_DECLARATIONS, COMMANDS, 
              RULES, INT_DECLARATIONS, CHAR_DECLARATIONS, 
              STRING_DECLARATIONS, ATOM_DECLARATIONS,  
              LIST_DECLARATIONS, VARIABLE_LIST, INTERFACE_LIST, 
              NODE_LIST, EDGE_LIST, GP_LIST, EMPTY_LIST} ListType;

typedef struct List {
  int node_id;
  ListType list_type;  
  YYLTYPE location;  /* location of symbol in the source file */
  union {
    struct GPDeclaration *declaration; /* GLOBAL_DECLARATIONS, 
					* LOCAL_DECLARATIONS */
    struct GPCommand *command;       /* COMMANDS */
    struct {
       string rule_name;
       struct GPRule *rule;
    } rule_call;                       /* RULES */
    struct List *variables;            /* INT_DECLARATIONS, CHAR_DECLARATIONS,
                                        * STRING_DECLARATIONS, ATOM_DECLARATIONS */ 
    string variable_name; 	       /* VARIABLE_LIST */	  
    string node_id; 		       /* INTERFACE_LIST */	   
    struct GPNode *node; 	       /* NODE_LIST */   
    struct GPEdge *edge; 	       /* EDGE_LIST */   
    struct GPAtomicExp *atom;          /* GP_LIST */
  } value;
  struct List *next;
} List;

List *addASTDecl (ListType list_type, YYLTYPE location, 
	          struct GPDeclaration *declaration, struct List *next);
List *addASTCommand (YYLTYPE location, struct GPCommand *command, 
                     struct List *next);
List *addASTRule (YYLTYPE location, string rule_name, struct List *next);
List *addASTVariableDecl (ListType list_type, YYLTYPE location, 
                          struct List *variables, struct List *next);
List *addASTVariable (YYLTYPE location, string variable_name, struct List *next);
List *addASTNodeID (YYLTYPE location, string node_id, struct List *next);
List *addASTNode (YYLTYPE location, struct GPNode *node, struct List *next);
List *addASTEdge (YYLTYPE location, struct GPEdge *edge, struct List *next);
List *addASTAtom (YYLTYPE location, struct GPAtomicExp *atom, struct List *next);
List *addASTEmptyList (YYLTYPE location);


/* Definition of AST nodes for declarations. */
typedef enum {MAIN_DECLARATION = 0, PROCEDURE_DECLARATION, RULE_DECLARATION} DeclType;

typedef struct GPDeclaration {
  int node_id;
  DeclType decl_type;
  YYLTYPE location;
  union {
    struct GPCommand *main_program; 	/* MAIN_DECLARATION */
    struct GPProcedure *procedure; 	/* PROCEDURE_DECLARATION */
    struct GPRule *rule; 		/* RULE_DECLARATION */
  } value;
} GPDeclaration;

GPDeclaration *newASTMainDecl (YYLTYPE location, struct GPCommand *main_program);
GPDeclaration *newASTProcedureDecl (YYLTYPE location, struct GPProcedure *procedure);
GPDeclaration *newASTRuleDecl (YYLTYPE location, struct GPRule *rule);


/* Definition of AST nodes for GP commands. */
typedef enum {COMMAND_SEQUENCE = 0, RULE_CALL, RULE_SET_CALL, PROCEDURE_CALL, 
              IF_STATEMENT, TRY_STATEMENT, ALAP_STATEMENT, PROGRAM_OR, 
              SKIP_STATEMENT, FAIL_STATEMENT, BREAK_STATEMENT} CommandType;

typedef struct GPCommand {
  int node_id;
  CommandType command_type;
  YYLTYPE location;
  union {    
    struct List *commands; 		/* COMMAND_SEQUENCE */
    struct {
       string rule_name; 
       struct GPRule *rule;   
    } rule_call;                        /* RULE_CALL */
    struct List *rule_set; 		/* RULE_SET_CALL */
    struct { 
       string proc_name;
       struct GPProcedure *procedure; 
    } proc_call;                        /* PROCEDURE_CALL */
    struct {  
      struct GPCommand *condition;
      struct GPCommand *then_command; 
      struct GPCommand *else_command; 
      int restore_point;
      bool roll_back;
    } cond_branch; 			/* IF_STATEMENT, TRY_STATEMENT */
    struct {
       struct GPCommand *loop_body;
       int restore_point;
       bool roll_back;
       bool stop_recording;
    } loop_stmt;                        /* ALAP_STATEMENT */
    struct { 
      struct GPCommand *left_command; 
      struct GPCommand *right_command; 
    } or_stmt;			        /* PROGRAM_OR */
    /* skip, fail and break statements are represented by a GPCommand
     * containing only a type and location. */
  } value;
} GPCommand;

GPCommand *newASTCommandSequence(YYLTYPE location, struct List *cmd_seq);
GPCommand *newASTRuleCall(YYLTYPE location, string rule_name);
GPCommand *newASTRuleSetCall(YYLTYPE location, struct List *rule_set);
GPCommand *newASTProcCall(YYLTYPE location, string proc_name);
GPCommand *newASTCondBranch(CommandType statement_type, YYLTYPE location, 
	                      struct GPCommand *condition, 
                              struct GPCommand *then_stmt, 
	                      struct GPCommand *else_stmt);
GPCommand *newASTAlap(YYLTYPE location, struct GPCommand *loop_body);
GPCommand *newASTOrStmt(YYLTYPE location, struct GPCommand *left_stmt, 
	                  struct GPCommand *right_stmt);
GPCommand *newASTSkip(YYLTYPE location);
GPCommand *newASTFail(YYLTYPE location);
GPCommand *newASTBreak(YYLTYPE location);


/* Definition of AST nodes for conditional expressions.*/
typedef struct GPCondExp {
  int node_id;
  CondExpType exp_type;
  YYLTYPE location;
  union {
    string var; 		/* INT_CHECK, CHAR_CHECK, STRING_CHECK, 
				 * ATOM_CHECK */
    struct {
      string source; 
      string target; 
      struct GPLabel *label;
    } edge_pred; 		/* EDGE_PRED */

    struct { 
      struct List *left_list;
      struct List *right_list; 
    } list_cmp; 		/* EQUAL, NOT_EQUAL */

    struct { 
      struct GPAtomicExp *left_exp; 
      struct GPAtomicExp *right_exp; 
    } atom_cmp; 		/* GREATER, GREATER_EQUAL, LESS, LESS_EQUAL */

    struct GPCondExp *not_exp;  /* BOOL_NOT */

    struct { 
      struct GPCondExp *left_exp; 
      struct GPCondExp *right_exp; 
    } bin_exp; 			/* BOOL_OR, BOOL_AND */
  } value;
} GPCondExp;

GPCondExp *newASTSubtypePred(CondExpType exp_type, YYLTYPE location, 
                             string var);
GPCondExp *newASTEdgePred(YYLTYPE location, string source, string target,
	                  struct GPLabel *label);
GPCondExp *newASTListComparison(CondExpType exp_type, YYLTYPE location, 
	                        struct List *left_list, 
                                struct List *right_list);
GPCondExp *newASTAtomComparison(CondExpType exp_type, YYLTYPE location,
	                        struct GPAtomicExp *left_exp, 
                                struct GPAtomicExp *right_exp);
GPCondExp *newASTNotExp(YYLTYPE location, struct GPCondExp *not_exp);
GPCondExp *newASTBinaryExp(CondExpType exp_type, YYLTYPE location, 
	                   struct GPCondExp *left_exp, 
                           struct GPCondExp *right_exp);

/* Definition of AST nodes for atomic expressions. */
typedef struct GPAtomicExp {
  int node_id;
  AtomExpType exp_type;
  YYLTYPE location;
  union {
    string name;		  /* VARIABLE */
    int number; 	 	  /* INTEGER_CONSTANT */
    string string;		  /* STRING_CONSTANT */
    string node_id; 		  /* INDEGREE, OUTDEGREE */
    struct List *list_arg; 	  /* LIST_LENGTH */
    struct GPAtomicExp *str_arg;  /* STRING_LENGTH */
    struct GPAtomicExp *exp; 	  /* NEG */
    struct { 
      struct GPAtomicExp *left_exp;
      struct GPAtomicExp *right_exp;
    } bin_op; 		   	  /* ADD, SUBTRACT, MULTIPLY, DIVIDE, CONCAT */
  } value;
} GPAtomicExp;

GPAtomicExp *newASTVariable (YYLTYPE location, string name);
GPAtomicExp *newASTNumber (YYLTYPE location, int number);
GPAtomicExp *newASTCharacter (YYLTYPE location, string character);
GPAtomicExp *newASTString (YYLTYPE location, string string);
GPAtomicExp *newASTDegreeOp (AtomExpType exp_type, YYLTYPE location, 
                             string node_id);
GPAtomicExp *newASTListLength (YYLTYPE location, struct List *list_arg);
GPAtomicExp *newASTStringLength (YYLTYPE location, struct GPAtomicExp *str_arg);
GPAtomicExp *newASTNegExp (YYLTYPE location, struct GPAtomicExp *exp);
GPAtomicExp *newASTBinaryOp (AtomExpType exp_type, YYLTYPE location, 
	                     struct GPAtomicExp *left_exp, 
                             struct GPAtomicExp *right_exp);


typedef enum {PROCEDURE = 0, RULE, NODE_PAIR, GRAPH, NODE, EDGE, LABEL} ASTNodeType;

typedef struct GPProcedure {
  int node_id;
  ASTNodeType node_type;
  YYLTYPE location;
  string name; 
  struct List *local_decls; 
  struct GPCommand *commands;
} GPProcedure;

GPProcedure *newASTProcedure(YYLTYPE location, string name, 
                             struct List *local_decls, struct GPCommand *cmd_seq);


typedef struct GPRule {
  int node_id;
  ASTNodeType node_type;
  YYLTYPE location;
  string name; 
  struct List *variables;
  struct GPGraph *lhs;
  struct GPGraph *rhs;
  struct List *interface;
  struct GPCondExp *condition;
  int left_nodes;
  int left_edges;
  int variable_count;
  bool empty_lhs;
  bool is_predicate;
} GPRule;

GPRule *newASTRule(YYLTYPE location, string name, struct List *variables, 
                   struct GPGraph *lhs, struct GPGraph *rhs, 
                   struct List *interface, struct GPCondExp *condition);

typedef struct GPGraph {
  int node_id;
  ASTNodeType node_type;	
  YYLTYPE location;
  struct List *nodes;
  struct List *edges;
} GPGraph;

GPGraph *newASTGraph(YYLTYPE location, struct List *nodes, struct List *edges);


typedef struct GPNode {
  int node_id;
  ASTNodeType node_type;	
  YYLTYPE location; 
  bool root;
  string name; 
  struct GPLabel *label; 
} GPNode;

GPNode *newASTNode(YYLTYPE location, bool root, string name, 
                   struct GPLabel *label);


typedef struct GPEdge {
  int node_id;
  ASTNodeType node_type;	
  YYLTYPE location; 
  bool bidirectional; 
  string name; 
  string source; 
  string target; 
  struct GPLabel *label; 
} GPEdge;

GPEdge *newASTEdge(YYLTYPE location, bool bidirectional, string name, 
                   string source, string target, struct GPLabel *label);

typedef struct GPLabel {
  int node_id;
  ASTNodeType node_type; 
  YYLTYPE location; 
  MarkType mark;
  struct List *gp_list;
} GPLabel;

GPLabel *newASTLabel(YYLTYPE location, MarkType mark, struct List *gp_list);

void freeAST(List *ast);
void freeASTDeclaration(GPDeclaration *decl);
void freeASTCommand(GPCommand *stmt);
void freeASTCondition(GPCondExp *cond);
void freeASTAtomicExp(GPAtomicExp *atom);
void freeASTProcedure(GPProcedure *proc);
void freeASTRule(GPRule *rule);
void freeASTGraph(GPGraph *graph);
void freeASTNode(GPNode *node);
void freeASTEdge(GPEdge *edge);
void freeASTLabel(GPLabel *label);

#endif /* INC_AST_H */

