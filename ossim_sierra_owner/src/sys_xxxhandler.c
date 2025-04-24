// From src/sys_xxxhandler.c
#include "common.h"
#include "syscall.h"
#include "stdio.h"

int __sys_xxxhandler(struct pcb_t * caller, struct sc_regs* regs)
{
    /* TODO: implement syscall job */
    
    printf("The first system call parameter %d\n", regs->a1);
    return 0;
}