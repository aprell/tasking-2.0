#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include "task.h"

//==========================================================================//
//                                                                          //
//    A stack of recently used Task objects (LIFO)                          //
//                                                                          //
//==========================================================================//

typedef struct stack Stack;

Stack *stack_new(void);
void stack_delete(Stack *stack);
bool stack_empty(Stack *stack);

void stack_push(Stack *stack, Task *task);
Task *stack_pop(Stack *stack);

#endif // STACK_H
