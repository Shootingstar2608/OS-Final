#include "common.h"
#include "syscall.h"
#include "stdio.h"
#include "libmem.h"
#include "queue.h"

int __sys_killall(struct pcb_t *caller, struct sc_regs* regs)
{
    char proc_name[100];
    uint32_t data;
 
    uint32_t memrg = regs->a1; // ID of mem region that stores proc name to kill
     
    /* Get name of the target proc */
    int i = 0;
    data = 0;
    while (data != (uint32_t)-1) {
        if (libread(caller, memrg, i, &data) != 0) {
            printf("Error reading from memory region %d at offset %d\n", memrg, i);
            return -1;
        }
        proc_name[i] = (char)(data & 0xFF); // Chỉ lấy 1 byte thấp nhất
        if (data == (uint32_t)-1) {
            proc_name[i] = '\0';
            break;
        }
        i++;
        if (i >= 100) {
            proc_name[99] = '\0';
            break;
        }
    }
    
    printf("The procname retrieved from memregionid %d is \"%s\"\n", memrg, proc_name);

    /* Traverse proclist to terminate the proc */
    struct pcb_t *kill_process[MAX_PRIO];
    int index = 0;
 
    struct queue_t *run_list = caller->running_list;
    struct queue_t *mlq = caller->mlq_ready_queue;
    printf("run_list size: %d\n", run_list->size);
     
    for (int i = 0; i < run_list->size; i++) {
        char *proc_get_name = strrchr(run_list->proc[i]->path, '/');
        if (proc_get_name && strcmp(proc_get_name + 1, proc_name) == 0) {
            kill_process[index++] = run_list->proc[i];
            printf("Found process name %s to kill in run list\n", proc_get_name + 1);
            run_list->proc[i]->pc = run_list->proc[i]->code->size;
            for (int j = i; j < run_list->size - 1; j++) {
                run_list->proc[j] = run_list->proc[j + 1];
            }
            run_list->size--;
            i--;
        }
    }
 
    for (int i = 0; i < MAX_PRIO; i++) {
        for (int j = 0; j < mlq[i].size; j++) {
            char *mlq_proc_get_name = strrchr(mlq[i].proc[j]->path, '/');
            if (mlq_proc_get_name && strcmp(mlq_proc_get_name + 1, proc_name) == 0) {
                printf("mlq[%d] size = %d\n", i, mlq[i].size);
                kill_process[index++] = mlq[i].proc[j];
                printf("Found process name %s to kill in mlq\n", mlq_proc_get_name + 1);
                mlq[i].proc[j]->pc = mlq[i].proc[j]->code->size;
                for (int k = 0; k < mlq[i].size - 1; k++) {
                    mlq[i].proc[k] = mlq[i].proc[k + 1];
                } 
                mlq[i].size--;
                j--;
            }
        }
    }

    if (index > 0) {
        printf("Remove %d process from mlq and run queue, ready to free\n", index);
        for (int i = 0; i < index; i++) {
            printf("Free allocated region for process %s with ID=%d in kill list\n", proc_name, i);
            for (int j = 0; j < kill_process[i]->code->size; j++) {
                if (kill_process[i]->code->text[j].opcode == ALLOC) {
                    __free(kill_process[i], kill_process[i]->mm->mmap->vm_id, kill_process[i]->code->text[j].arg_0);
                } 
            }
        }
    } else {
        printf("Process with name %s does not exist\n", proc_name);
    }

    return 0; 
}