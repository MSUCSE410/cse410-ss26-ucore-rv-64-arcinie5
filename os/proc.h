#ifndef PROC_H
#define PROC_H

#include "riscv.h"
#include "types.h"

#define NPROC (16)

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
	pagetable_t pagetable; // User page table
	uint64 ustack;
	uint64 kstack; // Virtual address of kernel stack
	struct trapframe *trapframe; // data page for trampoline.S
	struct context context; // swtch() here to run process
	uint64 max_page;
	/*
	* LAB1: you may need to add some new fields here
	*/
	/* Per-process syscall counter; index i holds the number of
	 * times syscall ID i has been invoked by this process */
	unsigned int syscall_times[MAX_SYSCALL_NUM];
	/* CPU cycle count recorded the first time this process is
	 * scheduled; used to compute total running time */
	uint64 start_time;
};

/*
* LAB1: you may need to define struct for TaskInfo here
*/
/* Container filled by sys_task_info and returned to the user.
 * Mirrors the user-side TaskInfo struct in user/stddef.h */
struct TaskInfo {
	/* Running status; hardcoded to 2 to match the user-side
	 * TaskStatus enum where Running = 2 */
	int status;
	/* Copy of the per-process syscall counters at query time */
	unsigned int syscall_times[MAX_SYSCALL_NUM];
	/* Total time the process has been running, in milliseconds */
	int time;
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