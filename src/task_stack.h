#ifndef TASK_STACK_H
#define TASK_STACK_H

#include <stdbool.h>
#include "task.h"

//==========================================================================//
//                                                                          //
//    A stack of recently used Task objects (LIFO)                          //
//                                                                          //
//==========================================================================//

typedef struct task_stack TaskStack;

TaskStack *task_stack_new(void);
void task_stack_delete(TaskStack *stack);
bool task_stack_empty(TaskStack *stack);

void task_stack_push(TaskStack *stack, Task *task);
Task *task_stack_pop(TaskStack *stack);

#endif // TASK_STACK_H
