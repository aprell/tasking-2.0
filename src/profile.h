#ifndef PROFILE_H
#define PROFILE_H

#include "timer.h"

// Helper macros
#define unique_name_paste(id, n) id ##_## n
#define unique_name(id, n) unique_name_paste(id, n)
#define unique_var unique_name(i, __LINE__)

// BLOCK(a, b) { ... } is equivalent to a; { ... } b;
#define BLOCK(before, after) \
	int unique_var; \
	for (unique_var = ((before), 0); !unique_var; (after), unique_var++)

#define PROFILE_EXTERN_DECL_RUN_TASK       extern PRIVATE mytimer_t timer_run_tasks
#define PROFILE_EXTERN_DECL_ENQ_DEQ_TASK   extern PRIVATE mytimer_t timer_enq_deq_tasks
#define PROFILE_EXTERN_DECL_SEND_RECV_TASK extern PRIVATE mytimer_t timer_send_recv_tasks
#define PROFILE_EXTERN_DECL_SEND_RECV_REQ  extern PRIVATE mytimer_t timer_send_recv_sreqs
#define PROFILE_EXTERN_DECL_IDLE           extern PRIVATE mytimer_t timer_idle

#define PROFILE_DECL_RUN_TASK          PRIVATE mytimer_t timer_run_tasks
#define PROFILE_DECL_ENQ_DEQ_TASK      PRIVATE mytimer_t timer_enq_deq_tasks
#define PROFILE_DECL_SEND_RECV_TASK    PRIVATE mytimer_t timer_send_recv_tasks
#define PROFILE_DECL_SEND_RECV_REQ     PRIVATE mytimer_t timer_send_recv_sreqs
#define PROFILE_DECL_IDLE              PRIVATE mytimer_t timer_idle

#define PROFILE_INIT_RUN_TASK()        timer_new(&timer_run_tasks, CPUFREQ)
#define PROFILE_INIT_ENQ_DEQ_TASK()    timer_new(&timer_enq_deq_tasks, CPUFREQ)
#define PROFILE_INIT_SEND_RECV_TASK()  timer_new(&timer_send_recv_tasks, CPUFREQ)
#define PROFILE_INIT_SEND_RECV_REQ()   timer_new(&timer_send_recv_sreqs, CPUFREQ)
#define PROFILE_INIT_IDLE()            timer_new(&timer_idle, CPUFREQ)

#define PROFILE_START_RUN_TASK()       timer_start(&timer_run_tasks)
#define PROFILE_START_ENQ_DEQ_TASK()   timer_start(&timer_enq_deq_tasks)
#define PROFILE_START_SEND_RECV_TASK() timer_start(&timer_send_recv_tasks)
#define PROFILE_START_SEND_RECV_REQ()  timer_start(&timer_send_recv_sreqs)
#define PROFILE_START_IDLE()           timer_start(&timer_idle)

#define PROFILE_STOP_RUN_TASK()        timer_end(&timer_run_tasks)
#define PROFILE_STOP_ENQ_DEQ_TASK()    timer_end(&timer_enq_deq_tasks)
#define PROFILE_STOP_SEND_RECV_TASK()  timer_end(&timer_send_recv_tasks)
#define PROFILE_STOP_SEND_RECV_REQ()   timer_end(&timer_send_recv_sreqs)
#define PROFILE_STOP_IDLE()            timer_end(&timer_idle)

#define PROFILE_INIT(x)                PROFILE_INIT_##x()
#define PROFILE_START(x)               PROFILE_START_##x()
#define PROFILE_STOP(x)                PROFILE_STOP_##x()

#ifndef NTIME
  #define PROFILE(x)                   BLOCK(PROFILE_START(x), PROFILE_STOP(x))
  #define PROFILE_EXTERN_DECL(x)       PROFILE_EXTERN_DECL_##x
  #define PROFILE_DECL(x)              PROFILE_DECL_##x
  #define PROFILE_RESULTS() \
	/* Parsable format */ \
	/* The first value should make it easy to grep for these lines, e.g. with */ \
	/* ./a.out | grep Timer | cut -d, -f2- */ \
	/* Worker ID, Task, Send/Recv Req, Send/Recv Task, Enq/Deq Task, Idle, Total */ \
	printf("Timer,%d,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf\n", ID, \
		   timer_elapsed  (&timer_run_tasks,       timer_us), \
		   timer_elapsed  (&timer_send_recv_sreqs, timer_us), \
		   timer_elapsed  (&timer_send_recv_tasks, timer_us), \
		   timer_elapsed  (&timer_enq_deq_tasks,   timer_us), \
		   timer_elapsed  (&timer_idle,            timer_us), \
		   timers_elapsed (&timer_run_tasks,       timer_us, \
			               &timer_send_recv_sreqs, \
						   &timer_send_recv_tasks, \
						   &timer_enq_deq_tasks, \
						   &timer_idle, \
						   NULL))
#else
  #define PROFILE(x)                   ((void)0); // removes the loop
  #define PROFILE_EXTERN_DECL(x)
  #define PROFILE_DECL(x)
  #define PROFILE_RESULTS()
#endif

#endif // PROFILE_H
