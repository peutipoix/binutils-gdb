/* Native-dependent code for GNU/Linux x86-64.

   Copyright 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Jiri Smid, SuSE Labs.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "inferior.h"
#include "gdbcore.h"
#include "regcache.h"
#include "linux-nat.h"

#include "gdb_assert.h"
#include "gdb_string.h"
#include <sys/ptrace.h>
#include <sys/debugreg.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <asm/prctl.h>
/* FIXME ezannoni-2003-07-09: we need <sys/reg.h> to be included after
   <asm/ptrace.h> because the latter redefines FS and GS for no apparent
   reason, and those definitions don't match the ones that libpthread_db
   uses, which come from <sys/reg.h>.  */
/* ezannoni-2003-07-09: I think this is fixed. The extraneous defs have
   been removed from ptrace.h in the kernel.  However, better safe than
   sorry.  */
#include <asm/ptrace.h>
#include <sys/reg.h>
#include "gdb_proc_service.h"

/* Prototypes for supply_gregset etc.  */
#include "gregset.h"

#include "x86-64-tdep.h"
#include "x86-64-linux-tdep.h"
#include "i386-linux-tdep.h"
#include "amd64-nat.h"

/* Mapping between the general-purpose registers in GNU/Linux x86-64
   `struct user' format and GDB's register cache layout.  */

static int x86_64_linux_gregset64_reg_offset[] =
{
  RAX * 8, RBX * 8,		/* %rax, %rbx */
  RCX * 8, RDX * 8,		/* %rcx, %rdx */
  RSI * 8, RDI * 8,		/* %rsi, %rdi */
  RBP * 8, RSP * 8,		/* %rbp, %rsp */
  R8 * 8, R9 * 8,		/* %r8 ... */
  R10 * 8, R11 * 8,
  R12 * 8, R13 * 8,
  R14 * 8, R15 * 8,		/* ... %r15 */
  RIP * 8, EFLAGS * 8,		/* %rip, %eflags */
  CS * 8, SS * 8,		/* %cs, %ss */
  DS * 8, ES * 8,		/* %ds, %es */
  FS * 8, GS * 8		/* %fs, %gs */
};


/* Mapping between the general-purpose registers in GNU/Linux x86-64
   `struct user' format and GDB's register cache layout for GNU/Linux
   i386.

   Note that most GNU/Linux x86-64 registers are 64-bit, while the
   GNU/Linux i386 registers are all 32-bit, but since we're
   little-endian we get away with that.  */

/* From <sys/reg.h> on GNU/Linux i386.  */
static int x86_64_linux_gregset32_reg_offset[] =
{
  RAX * 8, RCX * 8,		/* %eax, %ecx */
  RDX * 8, RBX * 8,		/* %edx, %ebx */
  RSP * 8, RBP * 8,		/* %esp, %ebp */
  RSI * 8, RDI * 8,		/* %esi, %edi */
  RIP * 8, EFLAGS * 8,		/* %eip, %eflags */
  CS * 8, SS * 8,		/* %cs, %ss */
  DS * 8, ES * 8,		/* %ds, %es */
  FS * 8, GS * 8,		/* %fs, %gs */
  -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1,
  ORIG_RAX * 8			/* "orig_eax" */
};

/* Which ptrace request retrieves which registers?
   These apply to the corresponding SET requests as well.  */

#define GETFPREGS_SUPPLIES(regno) \
  (FP0_REGNUM <= (regno) && (regno) <= MXCSR_REGNUM)


/* Transfering the general-purpose registers between GDB, inferiors
   and core files.  */

/* Fill GDB's register cache with the general-purpose register values
   in *GREGSETP.  */

void
supply_gregset (elf_gregset_t *gregsetp)
{
  amd64_supply_native_gregset (current_regcache, gregsetp, -1);
}

/* Fill register REGNUM (if it is a general-purpose register) in
   *GREGSETP with the value in GDB's register cache.  If REGNUM is -1,
   do this for all registers.  */

void
fill_gregset (elf_gregset_t *gregsetp, int regnum)
{
  amd64_collect_native_gregset (current_regcache, gregsetp, regnum);
}

/* Fetch all general-purpose registers from process/thread TID and
   store their values in GDB's register cache.  */

static void
fetch_regs (int tid)
{
  elf_gregset_t regs;

  if (ptrace (PTRACE_GETREGS, tid, 0, (long) &regs) < 0)
    perror_with_name ("Couldn't get registers");

  supply_gregset (&regs);
}

/* Store all valid general-purpose registers in GDB's register cache
   into the process/thread specified by TID.  */

static void
store_regs (int tid, int regnum)
{
  elf_gregset_t regs;

  if (ptrace (PTRACE_GETREGS, tid, 0, (long) &regs) < 0)
    perror_with_name ("Couldn't get registers");

  fill_gregset (&regs, regnum);

  if (ptrace (PTRACE_SETREGS, tid, 0, (long) &regs) < 0)
    perror_with_name ("Couldn't write registers");
}


/* Transfering floating-point registers between GDB, inferiors and cores.  */

/* Fill GDB's register cache with the floating-point and SSE register
   values in *FPREGSETP.  */

void
supply_fpregset (elf_fpregset_t *fpregsetp)
{
  x86_64_supply_fxsave (current_regcache, -1, fpregsetp);
}

/* Fill register REGNUM (if it is a floating-point or SSE register) in
   *FPREGSETP with the value in GDB's register cache.  If REGNUM is
   -1, do this for all registers.  */

void
fill_fpregset (elf_fpregset_t *fpregsetp, int regnum)
{
  x86_64_fill_fxsave ((char *) fpregsetp, regnum);
}

/* Fetch all floating-point registers from process/thread TID and store
   thier values in GDB's register cache.  */

static void
fetch_fpregs (int tid)
{
  elf_fpregset_t fpregs;

  if (ptrace (PTRACE_GETFPREGS, tid, 0, (long) &fpregs) < 0)
    perror_with_name ("Couldn't get floating point status");

  supply_fpregset (&fpregs);
}

/* Store all valid floating-point registers in GDB's register cache
   into the process/thread specified by TID.  */

static void
store_fpregs (int tid, int regnum)
{
  elf_fpregset_t fpregs;

  if (ptrace (PTRACE_GETFPREGS, tid, 0, (long) &fpregs) < 0)
    perror_with_name ("Couldn't get floating point status");

  fill_fpregset (&fpregs, regnum);

  if (ptrace (PTRACE_SETFPREGS, tid, 0, (long) &fpregs) < 0)
    perror_with_name ("Couldn't write floating point status");
}


/* Transferring arbitrary registers between GDB and inferior.  */

/* Fetch register REGNUM from the child process.  If REGNUM is -1, do
   this for all registers (including the floating point and SSE
   registers).  */

void
fetch_inferior_registers (int regnum)
{
  int tid;

  /* GNU/Linux LWP ID's are process ID's.  */
  tid = TIDGET (inferior_ptid);
  if (tid == 0)
    tid = PIDGET (inferior_ptid); /* Not a threaded program.  */

  if (regnum == -1 || amd64_native_gregset_supplies_p (regnum))
    {
      fetch_regs (tid);
      if (regnum != -1)
	return;
    }

  if (regnum == -1 || GETFPREGS_SUPPLIES (regnum))
    {
      fetch_fpregs (tid);
      return;
    }

  internal_error (__FILE__, __LINE__,
		  "Got request for bad register number %d.", regnum);
}

/* Store register REGNUM back into the child process.  If REGNUM is
   -1, do this for all registers (including the floating-point and SSE
   registers).  */

void
store_inferior_registers (int regnum)
{
  int tid;

  /* GNU/Linux LWP ID's are process ID's.  */
  tid = TIDGET (inferior_ptid);
  if (tid == 0)
    tid = PIDGET (inferior_ptid); /* Not a threaded program.  */

  if (regnum == -1 || amd64_native_gregset_supplies_p (regnum))
    {
      store_regs (tid, regnum);
      if (regnum != -1)
	return;
    }

  if (regnum == -1 || GETFPREGS_SUPPLIES (regnum))
    {
      store_fpregs (tid, regnum);
      return;
    }

  internal_error (__FILE__, __LINE__,
		  "Got request to store bad register number %d.", regnum);
}


static unsigned long
x86_64_linux_dr_get (int regnum)
{
  int tid;
  unsigned long value;

  /* FIXME: kettenis/2001-01-29: It's not clear what we should do with
     multi-threaded processes here.  For now, pretend there is just
     one thread.  */
  tid = PIDGET (inferior_ptid);

  /* FIXME: kettenis/2001-03-27: Calling perror_with_name if the
     ptrace call fails breaks debugging remote targets.  The correct
     way to fix this is to add the hardware breakpoint and watchpoint
     stuff to the target vectore.  For now, just return zero if the
     ptrace call fails.  */
  errno = 0;
  value = ptrace (PT_READ_U, tid,
		  offsetof (struct user, u_debugreg[regnum]), 0);
  if (errno != 0)
#if 0
    perror_with_name ("Couldn't read debug register");
#else
    return 0;
#endif

  return value;
}

static void
x86_64_linux_dr_set (int regnum, unsigned long value)
{
  int tid;

  /* FIXME: kettenis/2001-01-29: It's not clear what we should do with
     multi-threaded processes here.  For now, pretend there is just
     one thread.  */
  tid = PIDGET (inferior_ptid);

  errno = 0;
  ptrace (PT_WRITE_U, tid, offsetof (struct user, u_debugreg[regnum]), value);
  if (errno != 0)
    perror_with_name ("Couldn't write debug register");
}

void
x86_64_linux_dr_set_control (unsigned long control)
{
  x86_64_linux_dr_set (DR_CONTROL, control);
}

void
x86_64_linux_dr_set_addr (int regnum, CORE_ADDR addr)
{
  gdb_assert (regnum >= 0 && regnum <= DR_LASTADDR - DR_FIRSTADDR);

  x86_64_linux_dr_set (DR_FIRSTADDR + regnum, addr);
}

void
x86_64_linux_dr_reset_addr (int regnum)
{
  gdb_assert (regnum >= 0 && regnum <= DR_LASTADDR - DR_FIRSTADDR);

  x86_64_linux_dr_set (DR_FIRSTADDR + regnum, 0L);
}

unsigned long
x86_64_linux_dr_get_status (void)
{
  return x86_64_linux_dr_get (DR_STATUS);
}


ps_err_e
ps_get_thread_area (const struct ps_prochandle *ph,
                    lwpid_t lwpid, int idx, void **base)
{
/* This definition comes from prctl.h, but some kernels may not have it.  */
#ifndef PTRACE_ARCH_PRCTL
#define PTRACE_ARCH_PRCTL      30
#endif

  /* FIXME: ezannoni-2003-07-09 see comment above about include file order.
     We could be getting bogus values for these two.  */
  gdb_assert (FS < ELF_NGREG);
  gdb_assert (GS < ELF_NGREG);
  switch (idx)
    {
    case FS:
      if (ptrace (PTRACE_ARCH_PRCTL, lwpid, base, ARCH_GET_FS) == 0)
	return PS_OK;
      break;
    case GS:
      if (ptrace (PTRACE_ARCH_PRCTL, lwpid, base, ARCH_GET_GS) == 0)
	return PS_OK;
      break;
    default:                   /* Should not happen.  */
      return PS_BADADDR;
    }
  return PS_ERR;               /* ptrace failed.  */
}


void
child_post_startup_inferior (ptid_t ptid)
{
  i386_cleanup_dregs ();
  linux_child_post_startup_inferior (ptid);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_x86_64_linux_nat (void);

void
_initialize_x86_64_linux_nat (void)
{
  amd64_native_gregset32_reg_offset = x86_64_linux_gregset32_reg_offset;
  amd64_native_gregset32_num_regs = I386_LINUX_NUM_REGS;
  amd64_native_gregset64_reg_offset = x86_64_linux_gregset64_reg_offset;

  gdb_assert (ARRAY_SIZE (x86_64_linux_gregset32_reg_offset)
	      == amd64_native_gregset32_num_regs);
  gdb_assert (ARRAY_SIZE (x86_64_linux_gregset64_reg_offset)
	      == amd64_native_gregset64_num_regs);
}
