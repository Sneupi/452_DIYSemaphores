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

    int e;
    int w;
    int queue_e[QUEUE_SIZE];
    int queue_w[QUEUE_SIZE];

    struct csc452_sem full_e;
    struct csc452_sem empty_e;
    struct csc452_sem full_w;
    struct csc452_sem empty_w;

    struct csc452_sem mutex;
    struct csc452_sem honk;
};

void seminit(struct csc452_sem *sem, int value) { syscall(451, sem, value); }
void down(struct csc452_sem *sem) { syscall(452, sem); }
void up(struct csc452_sem *sem) { syscall(453, sem); }

void producer(struct sim_data *s, int isWest) {
    
    int *queue = (isWest) ? s->queue_w : s->queue_e;
    int *idx = (isWest) ? &(s->w) : &(s->e);
    int *idx_opp = (isWest) ? &(s->e) : &(s->w);
    const char dir = (isWest) ? 'W' : 'E';
    struct csc452_sem *empty = (isWest) ? &(s->empty_w) : &(s->empty_e);
    struct csc452_sem *full = (isWest) ? &(s->full_w) : &(s->full_e);

    // produce new items
    while (1) {

        down(empty);
        down(&(s->mutex));
        
        // enqueue
        int item = s->counter;
        queue[*idx] = item;
        (*idx)++;
        s->counter++;
        printf("Car %d coming from the %c direction arrived in the queue at time %d.\n", item, dir, -1);
        
        // if first arrival, honk
        if ((*idx)-1 == 0 && *idx_opp == 0) {
            printf("Car %d coming from the %c direction, blew their horn at time %d.\n", item, dir, -1);
            up(&(s->honk));
        }

        int maxed = (*idx == QUEUE_SIZE);

        up(&(s->mutex));
        up(full);

        // 25% chance traffic pauses for 8s
        if (maxed || rand() % 100 < 25) sleep(8);
    }
}

void consumer(struct sim_data *s) {

    // process each item
    while (1) {

        // sleep until woken
        down(&(s->mutex));
        if (s->e == 0 && s->w == 0) {
            printf("The flagperson is now asleep.\n");
            up(&(s->mutex));
            down(&(s->honk));
            printf("The flagperson is now awake.\n");
        }
        else {
            up(&(s->mutex));
        }

        down(&(s->full_e));
        down(&(s->mutex));
        
        // dequeue
        int item = s->queue_e[0]; 
        for (int i = 0; i < s->e - 1; i++) {
            s->queue_e[i] = s->queue_e[i + 1];
        }
        s->e--;
        up(&(s->mutex));
        up(&(s->empty_e));
        
        // process the item
        sleep(1);
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

    seminit(&(data->full_e), 0);
    seminit(&(data->empty_e), QUEUE_SIZE);
    seminit(&(data->full_w), 0);
    seminit(&(data->empty_w), QUEUE_SIZE);
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
        producer(data, 0);
        wait(NULL); // Keep parent alive
    }

    return 0;
}