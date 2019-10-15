#ifndef DEQUE_H
#define DEQUE_H

#include <stdbool.h>
#include "overload_deque_pop.h"
#include "overload_deque_prepend.h"
#include "overload_deque_steal_half.h"
#include "overload_deque_steal_many.h"
#include "task.h"

//==========================================================================//
//                                                                          //
//    A list-based thread-local work-stealing deque                         //
//                                                                          //
//    Tasks are stored in a doubly linked list -> unbounded                 //
//                                                                          //
//==========================================================================//

typedef struct deque Deque;

Deque *deque_new(void);
void deque_delete(Deque *dq);
Task *deque_task_new(Deque *dq);
void deque_task_cache(Deque *dq, Task *task);
bool deque_empty(Deque *dq);
unsigned int deque_num_tasks(Deque *dq);

void deque_push(Deque *dq, Task *task);
Task *deque_pop(Deque *dq);
Task *deque_pop(Deque *dq, Task *parent);
Task *deque_steal(Deque *dq);
Task *deque_steal_many(Deque *dq, Task **tail, int max, int *stolen);
Task *deque_steal_many(Deque *dq, int max, int *stolen);
Task *deque_steal_half(Deque *dq, Task **tail, int *stolen);
Task *deque_steal_half(Deque *dq, int *stolen);
Deque *deque_prepend(Deque *dq, Task *head, Task *tail, unsigned int len);
Deque *deque_prepend(Deque *dq, Task *head, unsigned int len);
Deque *deque_prepend(Deque *dq, Task *head);

#endif // DEQUE_H
