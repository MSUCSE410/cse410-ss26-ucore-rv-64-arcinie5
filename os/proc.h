#ifndef PROC_H
#define PROC_H

#include "types.h"

#define NPROC (16)
/* Maximum number of syscall IDs we track; higher than 410 (SYS_task_info = 410)*/
#define MAX_SYSCALL_NUM (500)

// Saved registers for kernel context switches.
struct context {
	uint64 ra;
	uint64 sp;

	// callee-saved
	uint64 s0;
	uint64 s1;
	uint64 s2;
	uint64 s3;
	uint64 s4;
	uint64 s5;
	uint64 s6;
	uint64 s7;
	uint64 s8;
	uint64 s9;
	uint64 s10;
	uint64 s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
	enum procstate state; // Process state
	int pid; // Process ID
	uint64 ustack; // Virtual address of user stack
	uint64 kstack; // Virtual address of kernel stack
	struct trapframe *trapframe; // data page for trampoline.S
	struct context context; // swtch() here to run process
	/*
	* LAB1: you may need to add some new fields here
	*/
	unsigned int syscall_times[MAX_SYSCALL_NUM]; // syscall counter; index i = number of times syscall ID i invoked 
	uint64 start_time; 						 // CPU cycle count first time process scheduled; computes total running time
};

/*
* LAB1: you may need to define struct for TaskInfo here
*/

/* Container filled by sys_task_info & returned to user.
 * Mirrors user-side TaskInfo struct in user/stddef.h */
struct TaskInfo {
	int status; 								 // Running status (hardcoded to 2 matches user-side TaskStatus enum)
	unsigned int syscall_times[MAX_SYSCALL_NUM]; // Copy of per-process syscall counters at query time 
	int time; 									 // Total process running time, ms
};

struct proc *curr_proc();
void exit(int);
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void yield();
struct proc *allocproc();
// swtch.S
void swtch(struct context *, struct context *);

#endif // PROC_H