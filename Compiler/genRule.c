#include "genRule.h"

static void generateMatchingCode(Rule *rule);
static void emitNodeFilteringCode(RuleNode *left_node, int indent);
static void emitRootNodeMatcher(Rule *rule, RuleNode *left_node, SearchOp *next_op);
static void emitNodeMatcher(Rule *rule, RuleNode *left_node, SearchOp *next_op);
static void emitNodeFromEdgeMatcher(Rule *rule, RuleNode *left_node, char type, SearchOp *next_op);
static void emitNodeMatchResultCode(RuleNode *node, SearchOp *next_op, int indent);
static void emitEdgeMatcher(Rule *rule, RuleEdge *left_edge, SearchOp *next_op);
static void emitLoopEdgeMatcher(Rule *rule, RuleEdge *left_edge, SearchOp *next_op);
static void emitEdgeFromNodeMatcher(Rule *rule, RuleEdge *left_edge, bool source,
                                    bool initialise, bool exit, SearchOp *next_op);
static void emitEdgeMatchResultCode(int index, SearchOp *next_op, int indent);
static void emitNextMatcherCall(SearchOp *next_operation);

FILE *header = NULL;
FILE *file = NULL;
Searchplan *searchplan = NULL;

void generateRules(List *declarations)
{
   while(declarations != NULL)
   {
      GPDeclaration *decl = declarations->declaration;
      switch(decl->type)
      {
         case MAIN_DECLARATION:
              break;

         case PROCEDURE_DECLARATION:
              if(decl->procedure->local_decls != NULL)
                 generateRules(decl->procedure->local_decls);
              break;

         case RULE_DECLARATION:
         {
              Rule *rule = transformRule(decl->rule);
              /* Annotate the AST's rule declaration node with information about
               * the rule. This is used when generating code to execute the GP 2
               * program. */
              decl->rule->empty_lhs = rule->lhs == NULL;
              decl->rule->is_predicate = isPredicate(rule);
              generateRuleCode(rule, decl->rule->is_predicate);
              freeRule(rule);
              break;
         }
         default: 
              print_to_log("Error (generateRules): Unexpected declaration type "
                           "%d at AST node %d\n", decl->type, decl->id);
              break;
      }
      declarations = declarations->next;
   }
}

/* Create a C module to match and apply the rule. */
void generateRuleCode(Rule *rule, bool predicate)
{
   /* Create files runtime/<rule name>.h and runtime/<rule name>.c */
   int length = strlen(rule->name) + 11;

   char header_name[length];
   char file_name[length];
   strcpy(header_name, "runtime/");
   strcpy(file_name, "runtime/");
   strcat(header_name, rule->name);
   strcat(file_name, rule->name);
   strcat(header_name, ".h");
   strcat(file_name, ".c");

   header = fopen(header_name, "w");
   if(header == NULL) { 
      perror(header_name);
      exit(1);
   }  
   file = fopen(file_name, "w");
   if(file == NULL) { 
      perror(file_name);
      exit(1);
   }

   PTH("#include \"globals.h\"\n"
       "#include \"graph.h\"\n"
       "#include \"graphStacks.h\"\n"
       "#include \"macros.h\"\n"
       "#include \"hostParser.h\"\n"
       "#include \"morphism.h\"\n\n");
   PTF("#include \"%s.h\"\n\n", rule->name);

   if(rule->condition != NULL)
   {
      /* The condition is iterated over three times.
       * The first iteration declares and initialises the runtime global boolean
       * varables, one for each predicate in the condition.
       * The second iteration writes the function to evaluate the condition.
       * The third iteration writes the functions to evaluate the predicates. */
      generateConditionVariables(rule->condition);
      PTF("\n");
      generateConditionEvaluator(rule->condition, false);
      generatePredicateEvaluators(rule, rule->condition);
   }
   if(rule->lhs != NULL) 
   {
      generateMatchingCode(rule);
      if(!predicate)
      {
         if(rule->rhs == NULL) generateRemoveLHSCode(rule->name);
         else generateApplicationCode(rule);
      }
   }
   else
   {
      if(rule->rhs != NULL) generateAddRHSCode(rule);
   }
   fclose(header);
   fclose(file);
   return;
}

static void generateMatchingCode(Rule *rule)
{
   searchplan = generateSearchplan(rule->lhs); 
   if(searchplan->first == NULL)
   {
      print_to_log("Error: empty searchplan. Aborting.\n");
      freeSearchplan(searchplan);
      return;
   }
   SearchOp *operation = searchplan->first;
   /* Iterator over the searchplan to print the prototypes of the matching functions. */
   while(operation != NULL)
   {
      char type = operation->type;
      switch(type)
      {
         case 'n':
         case 'r':
              PTF("static bool match_n%d(Morphism *morphism);\n", operation->index);
              break;

         case 'i': 
         case 'o': 
         case 'b':
              PTF("static bool match_n%d(Morphism *morphism, Edge *host_edge);\n",
                  operation->index);
              break;

         case 'e': 
         case 's': 
         case 't':
         case 'l':
              PTF("static bool match_e%d(Morphism *morphism);\n", operation->index);
              break;

         default:
              print_to_log("Error (generateMatchingCode): Unexpected "
                           "operation type %c.\n", operation->type);
              break;
      }
      operation = operation->next;
   }
   /* Generate the global variables used during rule matching. */
   PTH("bool match%s(Morphism *morphism);\n\n", rule->name);
   PTF("static int left_nodes = %d, left_edges = %d;\n", 
       rule->lhs->node_index, rule->lhs->edge_index);
   PTF("static int matched_nodes[%d];\n", rule->lhs->node_index);
   if(rule->lhs->edge_index > 0) 
      PTF("static int matched_edges[%d];\n\n", rule->lhs->edge_index);
   else PTF("\n");
   
   /* Generate the main matching function which sets up the runtime matching 
    * environment and calls the first matching function. */
   PTF("bool match%s(Morphism *morphism)\n{\n", rule->name);
   PTFI("if(left_nodes > host->number_of_nodes ||\n", 3);
   PTFI("left_edges > host->number_of_edges) return false;\n\n", 6);
   PTFI("int count;\n", 3);
   PTFI("for(count = 0; count < left_nodes; count++) matched_nodes[count] = -1;\n", 3);
   if(rule->lhs->edge_index > 0) 
      PTFI("for(count = 0; count < left_edges; count++) matched_edges[count] = -1;\n", 3);
   char item = searchplan->first->is_node ? 'n' : 'e';
   PTF("\n");
   PTFI("if(match_%c%d(morphism)) return true;\n", 3, item, searchplan->first->index);
   PTFI("else\n", 3);
   PTFI("{\n", 3);
   PTFI("initialiseMorphism(morphism);\n", 6);
   PTFI("return false;\n", 6);
   PTFI("}\n}\n\n", 3);

   /* Iterator over the searchplan to print the definitions of the matching functions. */
   operation = searchplan->first;
   RuleNode *node = NULL;
   RuleEdge *edge = NULL;
   while(operation != NULL)
   {
      switch(operation->type)
      {        
         case 'r': 
              node = getRuleNode(rule->lhs, operation->index);
              emitRootNodeMatcher(rule, node, operation->next);
              break;

         case 'n': 
              node = getRuleNode(rule->lhs, operation->index);
              emitNodeMatcher(rule, node, operation->next);
              break;

         case 'i': 
         case 'o': 
         case 'b':
              node = getRuleNode(rule->lhs, operation->index);
              emitNodeFromEdgeMatcher(rule, node, operation->type, operation->next);
              break;

         case 'e': 
              edge = getRuleEdge(rule->lhs, operation->index);
              emitEdgeMatcher(rule, edge, operation->next);
              break;

         case 'l':
              edge = getRuleEdge(rule->lhs, operation->index);
              emitLoopEdgeMatcher(rule, edge, operation->next);
              break;

         case 's': 
              edge = getRuleEdge(rule->lhs, operation->index);
              if(edge->bidirectional) 
              {
                 emitEdgeFromNodeMatcher(rule, edge, true, true, false, operation->next);
                 emitEdgeFromNodeMatcher(rule, edge, false, false, true, operation->next);
              }
              else emitEdgeFromNodeMatcher(rule, edge, true, true, true, operation->next);
              break;

         case 't':
              edge = getRuleEdge(rule->lhs, operation->index);
              if(edge->bidirectional) 
              {
                 emitEdgeFromNodeMatcher(rule, edge, false, true, false, operation->next);
                 emitEdgeFromNodeMatcher(rule, edge, true, false, true, operation->next);
              }
              else emitEdgeFromNodeMatcher(rule, edge, false, true, true, operation->next);
              break;
         
         default:
              print_to_log("Error (generateMatchingCode): Unexpected "
                           "operation type %c.\n", operation->type);
              break;
      }
      operation = operation->next;
   }
   freeSearchplan(searchplan);
}

/* The emitMatcher functions in this module take an LHS item and emit a function 
 * that searches for a matching host item. The generated code queries the host graph
 * for the appropriate item or list of items according to the LHS item and the
 * searchplan operation from which the code is generated.
 *
 * Several checks are made by each function to check if a host item matches
 * the LHS-item in the order presented below.
 * The host item must:
 * (1) Not have been matched already (GP2 requires injective matching).
 *     A boolean array, indexed by host indices, is maintained at runtime to 
 *     facilitate this check. The arrays are named matched_nodes and 
 *     matched_edges.
 * (2) Have the same mark as the the LHS-item.
 * (3) [Nodes only] Have degree compatibility with the LHS-node. For instance,
 *     if the candidate host node has fewer outgoing edges than the rule node,
 *     no match is possible.
 * (4) Have label compatibility with the LHS-item. This is the last step because
 *     label matching is, more computationally demanding than the other steps
 * These are generated by the emitNodeMatchingCode function.
 *
 * If a valid host item is found, the generated code pushes its index to the
 * appropriate morphism stack and calls the function for the following 
 * searchplan operation (see emitNextMatcherCall). If there are no operations 
 * left, code is generated to return true. */

/* Generates code to perform the first three checks described above.
 * The IF_INVALID_NODE macros perform degree checking. They are defined
 * in macros.h. */
static void emitNodeFilteringCode(RuleNode *left_node, int indent)
{
   if(left_node->label.mark != ANY)
      PTFI("if(host_node->label.mark != %d) candidate_node = false;\n",
           indent, left_node->label.mark);
   /* The node is deleted by the rule if it has no corresponding node in the interface. */
   if(left_node->interface == NULL)
        PTFI("IF_INVALID_DANGLING_NODE(%d, %d, %d) candidate_node = false;\n", indent,
             left_node->indegree, left_node->outdegree, left_node->bidegree);
   else PTFI("IF_INVALID_NODE(%d, %d, %d) candidate_node = false;\n", indent,
             left_node->indegree, left_node->outdegree, left_node->bidegree); 
   PTFI("int index;\n", indent);
   PTFI("for(index = 0; index < left_nodes; index++)\n", indent);
   PTFI("{\n", indent);
   PTFI("if(matched_nodes[index] == host_node->index)\n", indent + 3);
   PTFI("{\n", indent + 3);
   PTFI("candidate_node = false;\n", indent + 6);
   PTFI("break;\n", indent + 6);
   PTFI("}\n", indent + 3);
   PTFI("}\n", indent);
}

static void emitRootNodeMatcher(Rule *rule, RuleNode *left_node, SearchOp *next_op)
{
   PTF("static bool match_n%d(Morphism *morphism)\n{\n", left_node->index);
   PTFI("RootNodes *nodes = getRootNodeList(host);\n", 3);   
   PTFI("while(nodes != NULL)\n   {\n", 3);
   PTFI("Node *host_node = getNode(host, nodes->index);\n", 6);
   PTFI("if(host_node == NULL) continue;\n\n", 6);
   PTFI("bool candidate_node = true;\n", 6);
   /* Set the candidate_node flag. */
   emitNodeFilteringCode(left_node, 6);
   PTFI("if(!candidate_node)\n", 6);
   PTFI("{\n", 6);
   PTFI("nodes = nodes->next;\n", 9);
   PTFI("continue;\n", 9);
   PTFI("}\n", 6);

   PTFI("Label label = host_node->label;\n", 6);
   PTFI("bool match = false;\n", 6);
   if(hasListVariable(left_node->label))
      generateVariableListMatchingCode(rule, left_node->label, 6);
   else generateFixedListMatchingCode(rule, left_node->label, 6);
   emitNodeMatchResultCode(left_node, next_op, 6);
   PTFI("nodes = nodes->next;\n", 6);
   PTFI("}\n", 3);
   PTFI("return false;\n", 3);
   PTF("}\n\n");
}

/* The rule node is matched "in isolation", in that it is not the source or
 * target of a previously-matched edge. In this case, the candidate host
 * graph nodes are obtained from the appropriate label class tables. */
static void emitNodeMatcher(Rule *rule, RuleNode *left_node, SearchOp *next_op)
{
   PTF("static bool match_n%d(Morphism *morphism)\n{\n", left_node->index);
   #ifdef LABEL_CLASS_INDEXING
      /* Generates code to iterate over marks and/or label classes and call the
       * function to get the label class table. It returns the number of for loops
       * generated (0 - 2). */
      int loops = generateIteratorCode(left_node->label, true);
      int indent = 3 * (loops + 2);

      /* The table may not exist. If the generated code is not in a loop (i.e. if
       * there is only one table being considered), return false, otherwise
       * continue. */
      if(loops == 0) PTFI("if(table == NULL) return false;\n", indent - 3);
      else PTFI("if(table == NULL) continue;\n", indent - 3);
      PTFI("int items_index;\n", indent - 3);
      PTFI("for(items_index = 0; items_index < table->index; items_index++)\n", indent - 3);
      PTFI("{\n", indent - 3);
      PTFI("int host_index = table->items[items_index];\n", indent);
   #else
      PTFI("int host_index;\n", 3);
      PTFI("for(host_index = 0; host_index < host->node_index; host_index++)\n", 3);
      PTFI("{\n", 3);
      int indent = 6;
   #endif

   /* Fixed code. The indentation is determined by generateIteratorCode, which 
    * generates 0-2 for loops depending on the rule label. */
   PTFI("Node *host_node = getNode(host, host_index);\n", indent);
   PTFI("if(host_node == NULL) continue;\n\n", indent);
   PTFI("bool candidate_node = true;\n", indent);
   /* Set the candidate_node flag. */
   emitNodeFilteringCode(left_node, indent);
   PTFI("if(!candidate_node) continue;\n", indent);

   PTFI("Label label = host_node->label;\n", indent);
   PTFI("bool match = false;\n", indent);
   if(hasListVariable(left_node->label))
      generateVariableListMatchingCode(rule, left_node->label, indent);
   else generateFixedListMatchingCode(rule, left_node->label, indent);
   emitNodeMatchResultCode(left_node, next_op, indent);

   /* Close the for loops iterating over the label class table and over the label
    * class table array. */
   #ifdef LABEL_CLASS_INDEXING
      if(loops == 2) PTFI("}\n", 9);
      if(loops >= 1) PTFI("}\n", 6);
   #endif
   PTFI("}\n", 3);
   PTFI("return false;\n", 3);
   PTF("}\n\n");
}

/* Matching a node from a matched incident edge always follow an edge match in
 * the searchplan. The generated function takes the host edge matched by  
 * the previous searchplan function as one of its arguments. It gets the
 * appropriate host node (source or target of the host edge) and checks if this
 * node is compatible with the rule node. 
 * The type argument is either 'i', 'o', or 'b'. */
static void emitNodeFromEdgeMatcher(Rule *rule, RuleNode *left_node, char type, SearchOp *next_op)
{
   PTF("static bool match_n%d(Morphism *morphism, Edge *host_edge)\n{\n", left_node->index);
   if(type == 'i' || type == 'b') 
        PTFI("Node *host_node = getNode(host, getTarget(host_edge));\n\n", 3);
   else PTFI("Node *host_node = getNode(host, getSource(host_edge));\n\n", 3);
   PTFI("bool candidate_node = true;\n", 3);
   /* Set the candidate_node flag. */
   emitNodeFilteringCode(left_node, 3);
   /* If the above check fails and the edge is bidirectional, check the other 
    * node incident to the host edge. Otherwise return false. */
   if(type == 'b')
   {
      PTFI("if(!candidate_node)\n", 3);
      PTFI("{\n", 3); 
      PTFI("/* Matching from bidirectional edge: check the second incident node. */\n", 6);
      if(type == 'i' || type == 'b') 
           PTFI("host_node = getNode(host, getSource(host_edge));\n\n", 6);
      else PTFI("host_node = getNode(host, getTarget(host_edge));\n\n", 6);
      PTFI("candidate_node = true;\n", 6);
      emitNodeFilteringCode(left_node, 6);
      PTFI("if(!candidate_node) return false;\n", 6);
      PTFI("}\n\n", 3);
   }
   else PTFI("if(!candidate_node) return false;\n\n", 3);
 
   PTFI("Label label = host_node->label;\n", 3);
   PTFI("bool match = false;\n", 3);
   if(hasListVariable(left_node->label))
      generateVariableListMatchingCode(rule, left_node->label, 3);
   else generateFixedListMatchingCode(rule, left_node->label, 3);

   emitNodeMatchResultCode(left_node, next_op, 3);
   PTFI("return false;\n", 3);
   PTF("}\n\n");
}

/* Generates code to test the result of label matching a node. If the label
 * matching succeeds, any predicates in which the node participates are evaluated
 * and the condition checked. If everything succeeds, the morphism and matched_nodes
 * array are updated, and matching continues. If not, any runtime boolean variables
 * modified by predicate evaluation are reset, and any assignments made during label
 * matching are undone. */
static void emitNodeMatchResultCode(RuleNode *node, SearchOp *next_op, int indent)
{
   PTFI("if(match)\n", indent);
   PTFI("{\n", indent);
   PTFI("addNodeMap(morphism, %d, host_node->index, new_assignments);\n",
        indent + 3, node->index);
   PTFI("matched_nodes[%d] = host_node->index;\n", indent + 3, node->index);
   if(node->predicates != NULL)
   {
      PTFI("/* Update global booleans representing the node's predicates. */\n", indent + 3);
      int index;
      for(index = 0; index < node->predicate_count; index++)
         PTFI("evaluatePredicate%d(morphism);\n", indent + 3, 
              node->predicates[index]->bool_id);
      PTFI("bool condition = false;\n", indent + 3);
      if(next_op != NULL) PTFI("bool next_match_result = false;\n", indent + 3);
      PTFI("condition = evaluateCondition();\n", indent + 3);
      PTFI("if(condition)", indent + 3);
      if(next_op == NULL)
      { 
         PTF("\n");
         PTFI("{\n", indent + 3);
         PTFI("/* All items matched! */\n", indent + 6);
         PTFI("return true;\n", indent + 6);
         PTFI("}\n", indent + 3);
      }
      else
      {
         PTF(" next_match_result = ");
         emitNextMatcherCall(next_op);
         PTF(";\n");
         PTFI("if(condition && next_match_result) return true;\n", indent + 3);           
      }
      PTFI("else\n", indent + 3);
      PTFI("{\n", indent + 3);  
      PTFI("/* Reset the boolean variables in the predicates of this node. */\n", 
            indent + 6);
      for(index = 0; index < node->predicate_count; index++)
      { 
         Predicate *predicate = node->predicates[index];
         if(predicate->negated) PTFI("b%d = false;\n", indent + 6, predicate->bool_id);
         else PTFI("b%d = true;\n", indent + 6, predicate->bool_id);
      }
      PTFI("removeNodeMap(morphism);\n", indent + 6);
      PTFI("matched_nodes[%d] = -1;\n", indent + 6, node->index);  
      PTFI("}\n", indent + 3);
   }
   else
   {
      if(next_op == NULL)
      {
         PTFI("/* All items matched! */\n", indent + 3);
         PTFI("return true;\n", indent + 3);
      }
      else
      {
         PTFI("if(", indent + 3);
         emitNextMatcherCall(next_op); 
         PTF(") return true;\n");            
         PTFI("else\n", indent + 3);
         PTFI("{\n", indent + 3);  
         PTFI("removeNodeMap(morphism);\n", indent + 6);
         PTFI("matched_nodes[%d] = -1;\n", indent + 6, node->index);  
         PTFI("}\n", indent + 3);
      }
   }
   PTFI("}\n", indent);
   /* Print the else branch of the "if(match)" printed at the top of this function. */
   PTFI("else removeAssignments(morphism, new_assignments);\n", indent);
}

/* The rule edge is matched "in isolation", in that it is not incident to a
 * previously-matched node. In this case, the candidate host graph edges
 * are obtained from the appropriate label class tables. */
static void emitEdgeMatcher(Rule *rule, RuleEdge *left_edge, SearchOp *next_op)
{
   PTF("static bool match_e%d(Morphism *morphism)\n{\n", left_edge->index);
   #ifdef LABEL_CLASS_INDEXING
      /* Generates code to iterate over marks and/or label classes and call the
       * function to get the label class table. It returns the number of for loops
       * generated (0 - 2). */
      int loops = generateIteratorCode(left_edge->label, false);
      int indent = 3 * (loops + 2);

      /* The table may not exist. If the generated code is not in a loop (i.e. if
       * there is only one table being considered), return false, otherwise
       * continue. */
      if(loops == 0) PTFI("if(table == NULL) return false;\n", indent - 3);
      else PTFI("if(table == NULL) continue;\n", indent - 3);
      PTFI("int items_index;\n", indent - 3);
      PTFI("for(items_index = 0; items_index < table->index; items_index++)\n", indent - 3);
      PTFI("{\n", indent - 3);
      PTFI("int host_index = table->items[items_index];\n", indent);
   #else
      PTFI("int host_index;\n", 3);
      PTFI("for(host_index = 0; host_index < host->edge_index; host_index++)\n", 3);
      PTFI("{\n", 3);
      int indent = 6;
   #endif
   /* Fixed code. The indentation is determined by generateIteratorCode, which 
    * generates 0-2 for loops depending on the rule label. */
   PTFI("Edge *host_edge = getEdge(host, host_index);\n", indent);
   PTFI("if(host_edge == NULL) continue;\n\n", indent);
   if(left_edge->label.mark != ANY) 
      PTFI("if(host_edge->label.mark != %d) continue;\n", 6, left_edge->label.mark);
   PTFI("bool candidate_edge = true;\n", 6);
   PTFI("int index;\n", 6);
   PTFI("for(index = 0; index < left_edges; index++)\n", 6);
   PTFI("{\n", 6);
   PTFI("if(matched_edges[index] == host_edge->index)\n", 9);
   PTFI("{\n", 9);
   PTFI("candidate_edge = false;\n", 12);
   PTFI("break;\n", 12);
   PTFI("}\n", 9);
   PTFI("}\n", 6);
   PTFI("if(!candidate_edge) continue;\n", 6);
   PTFI("Label label = host_edge->label;\n", indent);
   PTFI("bool match = false;\n", indent);
   if(hasListVariable(left_edge->label))
      generateVariableListMatchingCode(rule, left_edge->label, indent);
   else generateFixedListMatchingCode(rule, left_edge->label, indent);
   emitEdgeMatchResultCode(left_edge->index, next_op, indent);

   /* Close the for loops iterating over the label class table and over the label
    * class table array. */
   #ifdef LABEL_CLASS_INDEXING
      if(loops == 2) PTFI("}\n", 9);
      if(loops >= 1) PTFI("}\n", 6);
   #endif
   PTFI("}\n", 3);
   PTFI("return false;\n", 3);
   PTF("}\n\n");
}

static void emitLoopEdgeMatcher(Rule *rule, RuleEdge *left_edge, SearchOp *next_op)
{
   PTF("static bool match_e%d(Morphism *morphism)\n{\n", left_edge->index);
   PTFI("/* Matching a loop. */\n", 3);
   PTFI("int node_index = lookupNode(morphism, %d);\n", 3, left_edge->source->index);
   PTFI("if(node_index < 0) return false;\n", 3);
   PTFI("Node *host_node = getNode(host, node_index);\n\n", 3);

   PTFI("int counter;\n", 3);
   PTFI("for(counter = host_node->out_index - 1; counter >= 0; counter--)\n", 3);
   PTFI("{\n", 3);
   PTFI("Edge *host_edge = getEdge(host, getOutEdge(host_node, counter));\n", 6);
   PTFI("if(host_edge == NULL) continue;\n", 6);
   PTFI("if(host_edge->source != host_edge->target) continue;\n\n", 6);
   if(left_edge->label.mark != ANY)
      PTFI("if(host_edge->label.mark != %d) continue;\n", 6, left_edge->label.mark);
   PTFI("bool candidate_edge = true;\n", 6);
   PTFI("int index;\n", 6);
   PTFI("for(index = 0; index < left_edges; index++)\n", 6);
   PTFI("{\n", 6);
   PTFI("if(matched_edges[index] == host_edge->index)\n", 9);
   PTFI("{\n", 9);
   PTFI("candidate_edge = false;\n", 12);
   PTFI("break;\n", 12);
   PTFI("}\n", 9);
   PTFI("}\n", 6);
   PTFI("if(!candidate_edge) continue;\n", 6);
   PTFI("Label label = host_edge->label;\n", 6);
   PTFI("bool match = false;\n", 6);
   if(hasListVariable(left_edge->label))
      generateVariableListMatchingCode(rule, left_edge->label, 6);
   else generateFixedListMatchingCode(rule, left_edge->label, 6);
   emitEdgeMatchResultCode(left_edge->index, next_op, 6);
   PTFI("}\n", 3);
   PTFI("return false;\n}\n\n", 3);
}

/* The following function matches a rule edge from one of its matched incident
 * nodes. Unlike matching a node from a matched incident edge, the LHS-node from
 * which this LHS-edge is matched may not necessarily be the previously matched 
 * node in the searchplan. The generated code uses the index of the incident nodes
 * of the LHS-edge to find the host node to which it has been matched. The candidate
 * host edges come from the edges lists of that node. 
 *
 * Called for various searchplan operations: matching an edge from its source,
 * matching an edge from its target, and matching bidirectional edges. In 
 * particular, the bidirectional edge code is generated by two calls to this
 * function. The skeleton of the generated code is largely the same for these 
 * operations, but different parts of the code printed by this function are required
 * for different operations. This is controlled by the three boolean flags:
 *
 * source - When set, the generated code searches from the match of the source of 
 *          the rule edge. Otherwise, it searches from the match of the target.
 * initialise - When set, this prints the header of the generated matching function.
 *              This is set in all cases except for the second call in the generation
 *              of bidirectional edge matching code.
 * exit - When set, this prints the return statement of the generated matching function.
 *        This is set in all cases except for the first call in the generation of
 *        bidirectional edge matching code. */
static void emitEdgeFromNodeMatcher(Rule *rule, RuleEdge *left_edge, bool source,
                                    bool initialise, bool exit, SearchOp *next_op)
{
   int start_index = source ? left_edge->source->index : left_edge->target->index;
   int end_index = source ? left_edge->target->index : left_edge->source->index;
   string end_node_type = source ? "target" : "source";

   if(initialise)
   {
      PTF("static bool match_e%d(Morphism *morphism)\n{\n", left_edge->index);
      PTFI("/* Start node is the already-matched node from which the candidate\n", 3);
      PTFI("   edges are drawn. End node may or may not have been matched already. */\n", 3);
      PTFI("int start_index = lookupNode(morphism, %d);\n", 3, start_index);
      PTFI("int end_index = lookupNode(morphism, %d);\n", 3, end_index);
      PTFI("if(start_index < 0) return false;\n", 3);
      PTFI("Node *host_node = getNode(host, start_index);\n\n", 3);
      PTFI("int counter;\n", 3);
   }
   if(source)
   {
      PTFI("for(counter = host_node->out_index - 1; counter >= 0; counter--)\n", 3);
      PTFI("{\n", 3);
      PTFI("Edge *host_edge = getEdge(host, getOutEdge(host_node, counter));\n", 6);
   }
   else
   {
      PTFI("for(counter = host_node->in_index - 1; counter >= 0; counter--)\n", 3);
      PTFI("{\n", 3);
      PTFI("Edge *host_edge = getEdge(host, getInEdge(host_node, counter));\n", 6);
   }

   PTFI("if(host_edge == NULL) continue;\n\n", 6);
   if(left_edge->label.mark != ANY)
      PTFI("if(host_edge->label.mark != %d) continue;\n", 6, left_edge->label.mark);
   PTFI("if(host_edge->source == host_edge->target) continue;\n", 6);
   PTFI("bool candidate_edge = true;\n", 6);
   PTFI("int index;\n", 6);
   PTFI("for(index = 0; index < left_edges; index++)\n", 6);
   PTFI("{\n", 6);
   PTFI("if(matched_edges[index] == host_edge->index)\n", 9);
   PTFI("{\n", 9);
   PTFI("candidate_edge = false;\n", 12);
   PTFI("break;\n", 12);
   PTFI("}\n", 9);
   PTFI("}\n", 6);
   PTFI("if(!candidate_edge) continue;\n", 6);

   PTFI("/* If the end node has been matched, check that the %s of the\n", 6, end_node_type);
   PTFI(" * host edge is the image of the end node. */\n", 6);
   PTFI("if(end_index >= 0)\n", 6);
   PTFI("{\n", 6);
   PTFI("if(host_edge->%s != end_index) continue;\n", 9, end_node_type);
   PTFI("}\n", 6);
   PTFI("/* Otherwise, the %s of the host edge should be unmatched. */\n", 6, end_node_type);
   PTFI("else\n", 6);
   PTFI("{\n", 6);
   PTFI("for(index = 0; index < left_nodes; index++)\n", 9);
   PTFI("{\n", 9);
   PTFI("if(matched_nodes[index] == host_edge->%s)\n", 12, end_node_type);
   PTFI("{\n", 12);
   PTFI("candidate_edge = false;\n", 15);
   PTFI("break;\n", 15);
   PTFI("}\n", 12);
   PTFI("}\n", 9);
   PTFI("}\n", 6);
   PTFI("if(!candidate_edge) continue;\n", 6);

   PTFI("Label label = host_edge->label;\n", 6);
   PTFI("bool match = false;\n", 6);
   if(hasListVariable(left_edge->label))
      generateVariableListMatchingCode(rule, left_edge->label, 6);
   else generateFixedListMatchingCode(rule, left_edge->label, 6);
   emitEdgeMatchResultCode(left_edge->index, next_op, 6);
   PTFI("}\n", 3);

   if(exit) PTFI("return false;\n}\n\n", 3);
}

/* Generates code to test the result of label matching a edge. If the label matching
 * succeeds, the morphism and matched_edges array are updated, and matching
 * continues. If not,  any assignments made during label matching are undone. */
static void emitEdgeMatchResultCode(int index, SearchOp *next_op, int indent)
{
   PTFI("if(match)\n", indent);
   PTFI("{\n", indent);
   PTFI("addEdgeMap(morphism, %d, host_edge->index, new_assignments);\n", indent + 3, index);
   PTFI("matched_edges[%d] = host_edge->index;\n", indent + 3, index);
   if(next_op == NULL)
   {
      PTFI("/* All items matched! */\n", indent);
      PTFI("return true;\n", indent);
   }
   else
   {
      PTFI("if(", indent + 3);
      emitNextMatcherCall(next_op); 
      PTF(") return true;\n");           
      PTFI("else\n", indent + 3);
      PTFI("{\n", indent + 3);                              
      PTFI("removeEdgeMap(morphism);\n", indent + 6);
      PTFI("matched_edges[%d] = -1;\n", indent + 6, index); 
      PTFI("}\n", indent + 3);
   } 
   PTFI("}\n", indent);
   PTFI("else removeAssignments(morphism, new_assignments);\n", indent);
}

static void emitNextMatcherCall(SearchOp *next_operation)
{
   switch(next_operation->type)
   {
      case 'n':
      case 'r':
           PTF("match_n%d(morphism)", next_operation->index);
           break;

      case 'i':
      case 'o':
      case 'b':
           PTF("match_n%d(morphism, host_edge)", next_operation->index);
           break;
  
      case 'e':
      case 's':
      case 't':
      case 'l':
           PTF("match_e%d(morphism)", next_operation->index);
           break;

      default:
           print_to_log("Error (emitNextMatcherCall): Unexpected "
                        "operation type %c.\n", next_operation->type);
           break;
   }
}

void generateRemoveLHSCode(string rule_name)
{
   PTH("void apply%s(Morphism *morphism, bool record_changes);\n", rule_name);
   PTF("void apply%s(Morphism *morphism, bool record_changes)\n{\n", rule_name);

   PTFI("int count;\n", 3);
   PTFI("for(count = 0; count < morphism->edges; count++)\n", 3);
   PTFI("{\n", 3);                        
   PTFI("if(record_changes)\n", 6);
   PTFI("{\n", 6);
   PTFI("Edge *edge = getEdge(host, morphism->edge_map[count].host_index);\n", 9); 
   PTFI("pushRemovedEdge(edge->label, edge->source, edge->target);\n", 9);  
   PTFI("}\n", 6);
   PTFI("removeEdge(host, morphism->edge_map[count].host_index, !record_changes);\n", 6);
   PTFI("}\n", 3);
                                                                           
   PTFI("for(count = 0; count < morphism->nodes; count++)\n", 3);
   PTFI("{\n", 3);                        
   PTFI("if(record_changes)\n", 6);
   PTFI("{\n", 6);
   PTFI("Node *node = getNode(host, morphism->node_map[count].host_index);\n", 9); 
   PTFI("pushRemovedNode(node->root, node->label);\n", 9);  
   PTFI("}\n", 6);
   PTFI("removeNode(host, morphism->node_map[count].host_index, !record_changes);\n", 6);
   PTFI("}\n", 6);
   PTFI("initialiseMorphism(morphism);\n}\n\n", 3);
}

void generateAddRHSCode(Rule *rule)
{
   PTH("void apply%s(bool record_changes);\n", rule->name);
   PTF("void apply%s(bool record_changes)\n{\n", rule->name);
   PTFI("int index;\n", 3);
   PTFI("Label label;\n\n", 3);
   /* Generate code to retrieve the values assigned to the variables in the
    * matching phase. */
   PTFI("/* Get the values of variables used in rule application. */\n", 3);
   int index;
   for(index = 0; index < rule->variable_index; index++)
   {
      Variable variable = rule->variables[index];
      if(variable.used_by_rule) generateVariableCode(variable.name, variable.type);
   }
   PTF("\n");
   /* Flag to prevent repeated writing of "label = blank_label" when
    * consecutive blank nodes are added to the graph. */
   bool blank_label = false;
   if(rule->adds_edges)
   {
      PTFI("/* Array of host node indices indexed by RHS node index. */\n", 3);
      PTFI("int map[%d];\n\n", 3, rule->rhs->node_index);
   }
   for(index = 0; index < rule->rhs->node_index; index++)
   {
      /* Add each node to the host graph. If the rule adds edges, extra
       * code is emitted to maintain a rule-to-host index map so that
       * the correct edges are added. */
      RuleNode *node = getRuleNode(rule->rhs, index);
      if(node->label.mark == NONE && node->label.length == 0)
      {
         if(!blank_label)
         {
            PTFI("label = blank_label;\n", 3);
            blank_label = true;
         }
      }
      else generateLabelEvaluationCode(node->label, true, index, 0, 3);
      PTFI("index = addNode(host, %d, label);\n", 3, node->root);
      if(rule->adds_edges) PTFI("map[%d] = index;\n", 3, node->index);
      PTFI("if(record_changes) pushAddedNode(index);\n", 3);
   }
   PTF("\n");
   for(index = 0; index < rule->rhs->edge_index; index++)
   {
      RuleEdge *edge = getRuleEdge(rule->rhs, index);
      if(edge->label.mark == NONE && edge->label.length == 0)
      {
         if(!blank_label)
         {
            PTFI("label = blank_label;\n", 3);
            blank_label = true;
         }
      }
      else generateLabelEvaluationCode(edge->label, false, index, 0, 3);
      /* The host-source and host-target of added edges are taken from the 
       * map populated in the previous loop. */
      PTFI("index = addEdge(host, label, map[%d], map[%d]);\n",
           3, edge->source->index, edge->target->index);
      PTFI("if(record_changes) pushAddedEdge(index);\n", 3);
   }     
   PTF("}\n");
   return;
}

void generateApplicationCode(Rule *rule)
{
   PTH("void apply%s(Morphism *morphism, bool record_changes);\n", rule->name);
   PTF("void apply%s(Morphism *morphism, bool record_changes)\n{\n", rule->name);
   
   /* Generate code to retrieve the values assigned to the variables in the
    * matching phase. */
   int index;
   PTFI("/* Get the values of variables and degree operators used in rule application. */\n", 3);
   for(index = 0; index < rule->variable_index; index++)
   {
      Variable variable = rule->variables[index];
      if(variable.used_by_rule) generateVariableCode(variable.name, variable.type);
   }
   bool node_index_declared = false;
   for(index = 0; index < rule->lhs->node_index; index++)
   {
      RuleNode *node = getRuleNode(rule->lhs, index);
      if(node->indegree_arg || node->outdegree_arg)
      {
         if(!node_index_declared) 
         {
            PTFI("int node_index = lookupNode(morphism, %d);\n", 3, index);
            node_index_declared = true;
         }
         else PTFI("node_index = lookupNode(morphism, %d);\n", 3, index);
         if(node->indegree_arg) 
            PTFI("int indegree%d = getIndegree(host, node_index);\n", 3, index);
         if(node->outdegree_arg) 
            PTFI("int outdegree%d = getOutdegree(host, node_index);\n", 3, index);
      }
   }
   PTF("\n");
   bool label_declared = false, host_edge_index_declared = false,
        host_node_index_declared = false;
   /* Host graph modifications are performed in the following order: 
    * (1) Delete/relabel edges.
    * (2) Delete/relabel nodes.
    * (3) Add nodes.
    * (4) Add edges.
    *
    * Nodes must be added before edges are added, because the incident nodes of
    * added edges may be added nodes. Edges must be deleted before nodes are
    * deleted because deleting nodes first may leave dangling edges. */

   /* Variable passed to generateLabelEvaluationCode. */
   int list_count = 0;
   /* (1) Delete/relabel edges. */
   for(index = 0; index < rule->lhs->edge_index; index++)
   {
      RuleEdge *edge = getRuleEdge(rule->lhs, index);
      if(edge->interface == NULL) 
      {
         if(!host_edge_index_declared)
         {
            PTFI("int host_edge_index;\n", 3);
            host_edge_index_declared = true;
         }
         /* Generate code to remove the edge. */
         PTFI("host_edge_index = lookupEdge(morphism, %d);\n", 3, index);
         PTFI("if(record_changes)\n   {\n", 3);
         PTFI("Edge *edge = getEdge(host, host_edge_index);\n", 6);
         PTFI("pushRemovedEdge(edge->label, edge->source, edge->target);\n   }\n", 6);
         PTFI("removeEdge(host, host_edge_index, !record_changes);\n\n", 3);   
      }
      else
      {
         if(edge->interface->relabelled)
         {
            if(!host_edge_index_declared)
            {
               PTFI("int host_edge_index;\n", 3);
               host_edge_index_declared = true;
            }
            /* Generate code to relabel the edge. */
            PTFI("host_edge_index = lookupEdge(morphism, %d);\n", 3, index);
            Label label = edge->interface->label;
            PTFI("if(record_changes)\n", 3);
            PTFI("{\n", 3);
            PTFI("Label label_e%d = getEdgeLabel(host, host_edge_index);\n", 6, index);
            PTFI("pushRelabelledEdge(host_edge_index, label_e%d);\n", 6, index);
            PTFI("}\n", 3);
            if(label.length == 0 && label.mark == NONE)
                PTFI("relabelEdge(host, host_edge_index, blank_label, !record_changes);\n", 3);
            else
            {
               if(!label_declared) 
               {
                  PTFI("Label label;\n", 3);
                  label_declared = true;
               }
               generateLabelEvaluationCode(label, false, list_count++, 0, 3);
               PTFI("relabelEdge(host, host_edge_index, label, !record_changes);\n\n", 3);
            }
         }
      }
   }
   /* (2) Delete/relabel nodes. */
   for(index = 0; index < rule->lhs->node_index; index++)
   { 
      RuleNode *node = getRuleNode(rule->lhs, index);
      if(node->interface == NULL) 
      {
         if(!host_node_index_declared)
         {
            PTFI("int host_node_index;\n", 3);
            host_node_index_declared = true;
         }
         /* Generate code to remove the node. */
         PTFI("host_node_index = lookupNode(morphism, %d);\n", 3, index);
         PTFI("if(record_changes)\n", 3);
         PTFI("{\n", 3);
         PTFI("Node *node = getNode(host, host_node_index);\n", 6);
         PTFI("pushRemovedNode(node->root, node->label);\n", 6);
         PTFI("}\n", 3);
         PTFI("removeNode(host, host_node_index, !record_changes);\n\n", 3);   
      }
      else
      {
         RuleNode *rhs_node = node->interface;
         if(rhs_node->relabelled)
         {
            Label label = rhs_node->label;
            if(!host_node_index_declared)
            {
               PTFI("int host_node_index;\n", 3);
               host_node_index_declared = true;
            }
            PTFI("host_node_index = lookupNode(morphism, %d);\n", 3, index);
            PTFI("Label label_n%d = getNodeLabel(host, host_node_index);\n", 3, index);
            PTFI("if(record_changes) pushRelabelledNode(host_node_index, label_n%d);\n", 3, index);
            if(label.length == 0 && label.mark == NONE)
               PTFI("relabelNode(host, host_node_index, blank_label, !record_changes);\n", 3);
            else
            {
               if(!label_declared) 
               {
                  PTFI("Label label;\n", 3);
                  label_declared = true;
               }
               generateLabelEvaluationCode(label, true, list_count++, 0, 3);
               PTFI("relabelNode(host, host_node_index, label, !record_changes);\n\n", 3);
            }
         }
         if(rhs_node->root_changed)
         {
            if(!host_node_index_declared)
            {
               PTFI("int host_node_index;\n", 3);
               host_node_index_declared = true;
            }
            PTFI("host_node_index = lookupNode(morphism, %d);\n", 3, index);
            /* The root is changed in two cases:
             * (1) The LHS node is rooted and the RHS node is non-rooted.
             * (2) The LHS node is non-rooted, the RHS node is rooted, and
             *     the matched host node is non-rooted. */

            /* Case (1) */
            if(node->root && !node->interface->root) 
            {
               PTFI("if(record_changes) pushChangedRootNode(host_node_index);\n", 6);
               PTFI("changeRoot(host, host_node_index);\n", 3);
            }
            /* Case (2) */
            if(!node->root && node->interface->root) 
            {
               PTFI("Node *node%d = getNode(host, host_node_index);\n", 3, index);
               PTFI("if(!node%d->root)\n", 3, index);
               PTFI("{\n", 3);
               PTFI("if(record_changes) pushChangedRootNode(host_node_index);\n", 6);
               PTFI("changeRoot(host, host_node_index);\n", 6);
               PTFI("}\n", 3);
            }
         }
      }
   }
   /* If both nodes and edges are added by the rule, the host indices of the 
    * added nodes need to be recorded in case the added edges require them. */
   if(rule->adds_nodes && rule->adds_edges)
   {
      PTFI("/* Array of host node indices indexed by RHS node index. */\n", 3);
      PTFI("int rhs_node_map[%d];\n\n", 3, rule->rhs->node_index);
   }
   /* (3) Add nodes. */
   for(index = 0; index < rule->rhs->node_index; index++)
   { 
      RuleNode *node = getRuleNode(rule->rhs, index);
      if(node->interface != NULL) continue;
      if(!host_node_index_declared)
      {
         PTFI("int host_node_index;\n", 3);
         host_node_index_declared = true;
      }
      if(node->label.length == 0 && node->label.mark == NONE)
         PTFI("host_node_index = addNode(host, %d, blank_label);\n", 3, node->root);
      else
      {
         if(!label_declared) 
         {
            PTFI("Label label;\n", 3);
            label_declared = true;
         }
         generateLabelEvaluationCode(node->label, true, list_count++, 0, 3);
         PTFI("host_node_index = addNode(host, %d, label);\n", 3, node->root);
      }
      if(rule->adds_edges) PTFI("rhs_node_map[%d] = host_node_index;\n", 3, node->index);
      PTFI("if(record_changes) pushAddedNode(host_node_index);\n\n", 3);
   }   
   /* (4) Add edges. */
   bool source_target_declared = false;
   for(index = 0; index < rule->rhs->edge_index; index++)
   { 
      RuleEdge *edge = getRuleEdge(rule->rhs, index);
      if(edge->interface != NULL) continue;
      if(!host_edge_index_declared)
      {
         PTFI("int host_edge_index;\n", 3);
         host_edge_index_declared = true;
      }
      if(!source_target_declared)
      {
         PTFI("int source, target;\n", 3);
         source_target_declared = true;
      }
      /* The source and target edges are either nodes preserved by the rule or 
       * nodes added by the rule. 
       * The host node indices of preserved nodes are obtained from the morphism.
       * The host node indices of added nodes are obtained from rhs_node_map. */
      if(edge->source->interface != NULL)
           PTFI("source = lookupNode(morphism, %d);\n", 3, edge->source->interface->index);
      else PTFI("source = rhs_node_map[%d];\n", 3, edge->source->index);

      if(edge->target->interface != NULL)
           PTFI("target = lookupNode(morphism, %d);\n", 3, edge->target->interface->index);
      else PTFI("target = rhs_node_map[%d];\n", 3, edge->target->index);

      if(edge->label.length == 0 && edge->label.mark == NONE)
         PTFI("host_edge_index = addEdge(host, blank_label, source, target);\n", 3);
      else
      {
         if(!label_declared) 
         {
            PTFI("Label label;\n", 3);
            label_declared = true;
         }
         generateLabelEvaluationCode(edge->label, false, list_count++, 0, 3);
         PTFI("host_edge_index = addEdge(host, label, source, target);\n", 3);
      }
      PTFI("if(record_changes) pushAddedEdge(host_edge_index);\n\n", 3);
   }
   PTFI("/* Reset the morphism. */\n", 3);
   PTFI("initialiseMorphism(morphism);\n}\n\n", 3);
}

