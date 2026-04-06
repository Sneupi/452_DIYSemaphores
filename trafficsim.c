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
#include <time.h>

// Using void* to avoid 
// duplication clutter
struct csc452_sem {
    int value;
    void *lock; 
    void *queue;
};

#define QUEUE_SIZE 20

struct sim_data {
    int counter;
    int start_time;

    int s;
    int n;
    int queue_s[QUEUE_SIZE];
    int queue_n[QUEUE_SIZE];

    struct csc452_sem full_s;
    struct csc452_sem empty_s;
    struct csc452_sem full_n;
    struct csc452_sem empty_n;

    struct csc452_sem mutex;
    struct csc452_sem honk;
};

void seminit(struct csc452_sem *sem, int value) { syscall(451, sem, value); }
void down(struct csc452_sem *sem) { syscall(452, sem); }
void up(struct csc452_sem *sem) { syscall(453, sem); }

void producer(struct sim_data *v, int isNorth) {
    
    int *queue = (isNorth) ? v->queue_n : v->queue_s;
    int *idx = (isNorth) ? &(v->n) : &(v->s);
    int *idx_opp = (isNorth) ? &(v->s) : &(v->n);
    const char dir = (isNorth) ? 'N' : 'S';
    struct csc452_sem *empty = (isNorth) ? &(v->empty_n) : &(v->empty_s);
    struct csc452_sem *full = (isNorth) ? &(v->full_n) : &(v->full_s);

    // produce new items
    while (1) {

        down(empty);
        down(&(v->mutex));
        
        // enqueue
        int item = v->counter;
        queue[*idx] = item;
        (*idx)++;
        v->counter++;
        printf("Car %d coming from the %c direction arrived in the queue at time %d.\n", item, dir, (int)time(NULL)-(v->start_time));
        
        // if first arrival, honk
        if ((*idx)-1 == 0 && *idx_opp == 0) {
            printf("Car %d coming from the %c direction, blew their horn at time %d.\n", item, dir, (int)time(NULL)-(v->start_time));
            up(&(v->honk));
        }

        int maxed = (*idx == QUEUE_SIZE);

        up(&(v->mutex));
        up(full);

        // 25% chance traffic pauses for 8s
        if (maxed || rand() % 100 < 25) sleep(8);
    }
}

void consumer(struct sim_data *v) {

    // init sleeping, await first honk
    int isNorth = 1;
    down(&(v->honk));

    // process each item
    while (1) {

        // if no activity, sleep
        down(&(v->mutex));
        if (v->s == 0 && v->n == 0) {
            printf("The flagperson is now asleep.\n");
            up(&(v->mutex));
            down(&(v->honk));
            printf("The flagperson is now awake.\n");
        } 
        // if opposite exceeds 8 pileup, flip
        else if (
            (isNorth && !v->n && v->s) ||
            (!isNorth && !v->s && v->n) ||
            (isNorth && v->n < 8 && v->s >= 8) ||
            (!isNorth && v->s < 8 && v->n >= 8)
        ) {
            isNorth = !isNorth;
            up(&(v->mutex));
        } 
        // (check normal exit)
        else {
            up(&(v->mutex));
        }

        // generalized vars
        int *queue = (isNorth) ? v->queue_n : v->queue_s;
        int *idx = (isNorth) ? &(v->n) : &(v->s);
        const char dir = (isNorth) ? 'N' : 'S';
        struct csc452_sem *empty = (isNorth) ? &(v->empty_n) : &(v->empty_s);
        struct csc452_sem *full = (isNorth) ? &(v->full_n) : &(v->full_s);

        // dequeue an item
        down(full);
        down(&(v->mutex));
        int item = queue[0]; 
        for (int i = 0; i < (*idx) - 1; i++) {
            queue[i] = queue[i + 1];
        }
        (*idx)--;
        up(&(v->mutex));
        up(empty);
        
        // process the item
        sleep(1);
        printf("Car %d coming from the %c direction left the construction zone at time %d.\n", item, dir, (int)time(NULL)-(v->start_time));
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

    seminit(&(data->full_s), 0);
    seminit(&(data->empty_s), QUEUE_SIZE);
    seminit(&(data->full_n), 0);
    seminit(&(data->empty_n), QUEUE_SIZE);
    seminit(&(data->mutex), 1);
    seminit(&(data->honk), 0);

    data->start_time = (int)time(NULL);

    // spawn traffic 1 (child)
    pid_t pid_e = fork();
    if (pid_e < 0) {
        perror("Fork 1 failed");
        return 1;
    } else if (pid_e == 0) {
        producer(data, 1);
        exit(0);
    }

    // spawn traffic 2 (child)
    pid_t pid_w = fork();
    if (pid_w < 0) {
        perror("Fork 2 failed");
        return 1;
    } else if (pid_w == 0) {
        producer(data, 0);
        exit(0);
    } 
    
    // run flag person (parent)
    consumer(data);
    wait(NULL);
    wait(NULL);

    return 0;
}