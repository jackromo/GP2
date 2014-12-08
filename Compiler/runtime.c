#include "runtime.h"

FILE *log_file;

int main() {
  
   log_file = stdout;

   /* ListElement *int1 = malloc(sizeof(ListElement));
   if(int1 == NULL) {
      printf("OUT OF MEMORY.\n"); 
      exit(0);
    }
   int1->type = INTEGER_CONSTANT;
   int1->value.number = 1;

   GList *list = g_list_prepend(NULL, int1);
   
   Label *label = malloc(sizeof(Label));
   if(label == NULL) {
      printf("OUT OF MEMORY.\n"); 
      exit(0);
    }
   label->mark = NONE;
   label->list = list;
   label->list_length = 1;
   label->has_list_variable = false;

   ListElement *int2 = malloc(sizeof(ListElement));
   if(int2 == NULL) {
      printf("OUT OF MEMORY.\n"); 
      exit(0);
    }
   int2->type = INTEGER_CONSTANT;
   int2->value.number = 2;

   GList *list2 = g_list_prepend(NULL, int2);
   
   Label *label2 = malloc(sizeof(Label));
   if(label2 == NULL) {
      printf("OUT OF MEMORY.\n"); 
      exit(0);
    }
   label2->mark = NONE;
   label2->list = list2;
   label2->list_length = 1;
   label2->has_list_variable = false; */


   Label *empty = newBlankLabel();  
   Label *empty2 = newBlankLabel();  
   Label *empty3 = newBlankLabel(); 
   Label *empty4 = newBlankLabel();
   Label *empty5 = newBlankLabel();
   Label *empty6 = newBlankLabel();
   Label *empty7 = newBlankLabel();
   Label *empty8 = newBlankLabel();
   Label *empty9 = newBlankLabel();

   Node *hn1 = newNode(false, empty);
   Node *hn2 = newNode(false, empty2);
   Node *hn3 = newNode(true, empty3);
   Node *hn4 = newNode(false, empty4);
   Node *hn5 = newNode(false, empty5);
   Edge *he1 = newEdge(false, empty6, hn1, hn2); 
   Edge *he2 = newEdge(false, empty7, hn2, hn3); 
   Edge *he3 = newEdge(false, empty8, hn3, hn4); 
   Edge *he4 = newEdge(false, empty9, hn3, hn5); 

   Graph *host = newGraph();
   addNode(host, hn1);
   addNode(host, hn2);
   addNode(host, hn3);
   addNode(host, hn4);
   addNode(host, hn5);
   addEdge(host, he1);
   addEdge(host, he2);
   addEdge(host, he3);
   addEdge(host, he4);

   Morphism *morphism = match_r1(host);

   printMorphism(morphism);
   freeMorphism(morphism);

   if(host) freeGraph(host);
   
   return 0;
}
