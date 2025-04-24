#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t * q) {
        if (q == NULL) return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t * q, struct pcb_t * proc) {
        /* TODO: put a new process to queue [q] */
        if (q->size < MAX_QUEUE_SIZE) {
                q->proc[q->size++] = proc;
        } else {
                fprintf(stderr, "Queue is full!\n");
        }
}

struct pcb_t * dequeue(struct queue_t * q) {
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (empty(q)) return NULL;

        struct pcb_t *highest_prio_proc = q->proc[0];
        int highest_prio_index = 0;

        // Tìm tiến trình có ưu tiên cao nhất
        for (int i = 1; i < q->size; i++) {
            if (q->proc[i]->prio > highest_prio_proc->prio) {
                highest_prio_proc = q->proc[i];
                highest_prio_index = i;
            }
        }

        // Xóa phần tử ở vị trí `highest_prio_index`
        for (int i = highest_prio_index; i < q->size - 1; i++) {
            q->proc[i] = q->proc[i + 1];
        }

        q->size--;
        return highest_prio_proc;
}

