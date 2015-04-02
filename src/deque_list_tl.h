#ifndef DEQUE_LIST_TL_H
#define DEQUE_LIST_TL_H

#include <stdbool.h>
#include "task.h"
#include "overload_deque_list_tl_steal_half.h"
#include "overload_deque_list_tl_prepend.h"

//==========================================================================//
//                                                                          //
//    A list-based thread-local work-stealing deque                         //
//                                                                          //
//    Tasks are stored in a doubly linked list -> unbounded                 //
//                                                                          //
//==========================================================================//

typedef struct deque_list_tl DequeListTL;

DequeListTL *deque_list_tl_new(void);
void deque_list_tl_delete(DequeListTL *dq);
Task *deque_list_tl_task_new(DequeListTL *dq);
void deque_list_tl_task_cache(DequeListTL *dq, Task *task);
bool deque_list_tl_empty(DequeListTL *dq);
unsigned int deque_list_tl_num_tasks(DequeListTL *dq);

void deque_list_tl_push(DequeListTL *dq, Task *task);
Task *deque_list_tl_pop(DequeListTL *dq);
Task *deque_list_tl_pop_child(DequeListTL *dq, Task *parent);
Task *deque_list_tl_steal(DequeListTL *dq);
Task *deque_list_tl_steal_many(DequeListTL *dq, Task **tail, int max,
		                       int *stolen);
Task *deque_list_tl_steal_half(DequeListTL *dq, Task **tail, int *stolen);
Task *deque_list_tl_steal_half(DequeListTL *dq, int *stolen);
DequeListTL *deque_list_tl_prepend(DequeListTL *dq, Task *head, Task *tail,
                                   unsigned int len);
DequeListTL *deque_list_tl_prepend(DequeListTL *dq, Task *head,
		                           unsigned int len);

#endif // DEQUE_LIST_TL_H
