#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h" // needed for struct TaskInfo, struct proc, curr_proc(), MAX_SYSCALL_NUM

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	/* val is a user virtual address; we cannot write to it directly.
	 * Build the result in kernel space first, then copy it out */
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal tv;
	tv.sec  = cycle / CPU_FREQ;
	tv.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	/* copyout translates the user virtual address and writes the data */
	if (copyout(p->pagetable, (uint64)val, (char *)&tv, sizeof(tv)) < 0)
		return -1;
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
	/* len = 0 is a no-op per the spec */
	if (len == 0)
		return 0;

	/* start must be page-aligned */
	if (!PGALIGNED(start))
		return -1;

	/* len must not exceed 1 GiB */
	if (len > (1u << 30))
		return -1;

	/* Bits above the lower 3 of port must all be zero */
	if (port & ~0x7)
		return -1;

	/* At least one permission bit must be set; all-zero is meaningless */
	if ((port & 0x7) == 0)
		return -1;

	struct proc *p = curr_proc();

	/* Round the end address up to the next page boundary */
	uint64 va0  = start;
	uint64 vaend = PGROUNDUP(start + len);

	/* Verify no page in [va0, vaend) is already mapped */
	for (uint64 va = va0; va < vaend; va += PGSIZE) {
		if (walkaddr(p->pagetable, va) != 0)
			return -1; // a page is already mapped in this range
	}

	/* Translate port bits into RISC-V PTE permission flags.
	 * PTE_U must always be set so user mode can access the pages.
	 * port bit 0 -> PTE_R (readable)
	 * port bit 1 -> PTE_W (writable)
	 * port bit 2 -> PTE_X (executable) */
	int perm = PTE_U;
	if (port & 0x1) perm |= PTE_R;
	if (port & 0x2) perm |= PTE_W;
	if (port & 0x4) perm |= PTE_X;

	/* Allocate and map one physical page per virtual page */
	for (uint64 va = va0; va < vaend; va += PGSIZE) {
		/* Request a fresh physical page from the allocator */
		void *pa = kalloc();
		if (pa == 0) {
			/* Out of physical memory; clean up already-mapped pages */
			uvmunmap(p->pagetable, va0, (va - va0) / PGSIZE, 1);
			return -1;
		}
		/* Zero the page to avoid leaking stale kernel data to user */
		memset(pa, 0, PGSIZE);
		/* Install the virtual->physical mapping in the page table */
		if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) != 0) {
			/* mappages failed; free this page and unmap previous ones */
			kfree(pa);
			uvmunmap(p->pagetable, va0, (va - va0) / PGSIZE, 1);
			return -1;
		}
	}
	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
	/* start must be page-aligned */
	if (!PGALIGNED(start))
		return -1;

	/* len = 0 is a no-op */
	if (len == 0)
		return 0;

	struct proc *p = curr_proc();

	/* Compute the page-aligned end of the region to unmap */
	uint64 va0  = start;
	uint64 vaend = PGROUNDUP(start + len);

	/* Verify every page in [va0, vaend) is currently mapped;
	 * the spec requires an error if any page is unmapped */
	for (uint64 va = va0; va < vaend; va += PGSIZE) {
		if (walkaddr(p->pagetable, va) == 0)
			return -1; // unmapped page found in range
	}

	/* Calculate total number of pages to unmap */
	uint64 npages = (vaend - va0) / PGSIZE;

	/* Unmap the pages and free the underlying physical memory */
	uvmunmap(p->pagetable, va0, npages, 1);

	return 0;
}

/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(struct TaskInfo *ti)
{
	struct proc *p = curr_proc();
	/* Reject a null pointer to avoid a kernel crash */
	if (ti == 0)
		return -1;

	/* Build the TaskInfo in kernel space first */
	struct TaskInfo kernel_ti;

	kernel_ti.status = 2;
	/* Copy this process's syscall counts into the user struct */
	for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
		kernel_ti.syscall_times[i] = p->syscall_times[i];
	}
	/* Convert elapsed CPU cycles to milliseconds:
	 * divide by (cycles per second / 1000) = cycles per millisecond */
	kernel_ti.time = (int)((get_cycle() - p->start_time) / (CPU_FREQ / 1000));

	/* Copy the kernel struct out to the user virtual address */
	if (copyout(p->pagetable, (uint64)ti, (char *)&kernel_ti, sizeof(kernel_ti)) < 0)
		return -1;

	return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	if (id >= 0 && id < MAX_SYSCALL_NUM) {
		curr_proc()->syscall_times[id]++;
	}

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info((struct TaskInfo *)args[0]);
		break;
	/* mmap and munmap: syscall IDs 222 and 215 are already
	 * defined in syscall_ids.h so no changes needed there */
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
