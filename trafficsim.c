/**
 * File: trafficsim.c
 * Author: Gabe Venegas
 * Date: 4/6/2026
 *
 * Traffic simulation making use of multi-processing 
 * and lane queues (shared/sync via "DIY" semaphores),
 * to simulate a "two-lane, single-wide road" problem, 
 * handled by a flag person to conduct traffic through.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/syscall.h>

// Using void* to avoid 
// duplication clutter
struct csc452_sem {
    int value;
    void *lock; 
    void *queue;
};

#define QUEUE_SIZE 100

struct sim_data {
    int counter;
    int idx;
    int queue[QUEUE_SIZE];
    struct csc452_sem full;
    struct csc452_sem mutex;
    struct csc452_sem empty;
    struct csc452_sem honk;
};

void seminit(struct csc452_sem *sem, int value) { syscall(451, sem, value); }
void down(struct csc452_sem *sem) { syscall(452, sem); }
void up(struct csc452_sem *sem) { syscall(453, sem); }

void producer(struct sim_data *s) {
    
    // produce new items
    while (1) {

        // enqueue
        down(&(s->empty));
        down(&(s->mutex));

        int item = s->counter;
        s->queue[s->idx] = item;
        s->idx++;
        s->counter++;

        // printf("++ %3d | waiting=%d\n", item, s->idx);
        printf("Car %d coming from the %c direction arrived in the queue at time %d.\n", item, -1, -1);

        // if first arrival, honk
        if (!(s->idx - 1)) {
            up(&(s->honk));
            printf("Car %d coming from the %c direction, blew their horn at time %d.\n", item, -1, -1);
        }
        
        up(&(s->mutex));
        up(&(s->full)); 
        
        // roll for end of batch
        if (rand() % 100 > 75) sleep(8); // 25% chance of 8s sleep
    }
}

void consumer(struct sim_data *s) {

    // process each item
    while (1) {

        // sleep until woken
        if (!s->idx) {
            printf("The flagperson is now asleep.\n");
            down(&(s->honk));
            printf("The flagperson is now awake.\n");
        }

        // dequeue
        down(&(s->full)); 
        down(&(s->mutex));
        int item = s->queue[0]; 
        for (int i = 0; i < s->idx - 1; i++) {
            s->queue[i] = s->queue[i + 1];
        }
        s->idx--;
        up(&(s->mutex));
        up(&(s->empty));
        
        // process the item
        sleep(1);
        // printf("-- %3d | waiting=%d\n", item, s->idx);
        printf("Car %d coming from the %c direction left the construction zone at time %d.\n", item, -1, -1);
    }
}

void handle_sigint(int sig) {
    printf("\nExiting...\n");
    exit(0);
}

int main() {

    // Setup interrupts for program termination (Ctrl+C)

    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    // Allocate shared memory for the entire data struct

    struct sim_data *data = mmap(NULL, sizeof(struct sim_data), 
                                 PROT_READ | PROT_WRITE, 
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    if (data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    memset(data, 0, sizeof(struct sim_data));

    seminit(&(data->full), 0);
    seminit(&(data->empty), QUEUE_SIZE);
    seminit(&(data->mutex), 1);
    seminit(&(data->honk), 0);

    // Begin main program simulation
    
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return 1;
    }

    if (pid == 0) {
        consumer(data);
    } else {
        producer(data);
        wait(NULL); // Keep parent alive
    }

    return 0;
}