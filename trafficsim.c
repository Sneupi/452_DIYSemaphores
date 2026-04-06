#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>

struct proc_q
{
	struct task_struct *task;
	struct proc_q *next;
};

struct csc452_sem
{
	int value;
	struct mutex *lock;
	struct proc_q *queue;
};

void seminit(struct csc452_sem *sem, int value) { syscall(451, sem, value); }
void down(struct csc452_sem *sem)               { syscall(452, sem); }
void up(struct csc452_sem *sem)                 { syscall(453, sem); }

/**
 * For the "two-way traffic, one-way road problem" presented, we may extend 
 * the idea of the single source producer-consumer problem, with modifications 
 * for consuming from 2+ sources.
 *
 * Consider each lane/queue (A, B, etc) getting its own flag person (consumer) 
 * avocating for it. Just like single-source producer-consumer, this flag 
 * person will consume cars as fast as possible, given access to the queue.
 * Now, consider adding the modification that in order to pass it's cars, the
 * flag person also requires access to the road lock. This means, for any 
 * arbitrary number of lanes, only one will be accessing the road at a time,
 * as no more than 1 flag person can attain this lock at a time.
 *  
 * Futhermore, by using flag people who are generous (process few cars then 
 * unlocks) we may add other elements (the traffic controller), which can 
 * monitor waiting lanes with greater discernment than first-come-first-serve, 
 * and intercept the lock to give control to any specific lane in need.
 */

void traffic_controller(
	int *queue_a,
	int *queue_b,
	int *queue_len_a,
	int *queue_len_b,
	struct csc452_sem *sem_queue_a,
	struct csc452_sem *sem_queue_b,
	struct csc452_sem *sem_road_a,
	struct csc452_sem *sem_road_b) {

	// init control to A
	int state = 0;
	down(sem_road_b);

	do {
		// get snapshot of the system
		down(sem_queue_a);
		down(sem_queue_b);
		int len_a = *queue_len_a;
		int len_b = *queue_len_b;
		up(sem_queue_b);
		up(sem_queue_a);
		
		// perform controlled inversion of road access
		if ((state==0 && !len_a && len_b) ||
			(state==1 && !len_b && len_a) ||
			(state==0 && len_a < 8 && len_b >= 8) ||
			(state==1 && len_b < 8 && len_a >= 8)) {

			state = !state;

			// give control to A=0, hold B down, bring A up
			if (!state) {
				down(sem_road_b);
				up(sem_road_a);
			}
			// give control to B=1, hold A down, bring B up
			else {
				down(sem_road_a);
				up(sem_road_b);
			}
		}
	} while (1);

	// clean up in state A=0, need to release B
	if (!state) { down(sem_road_a); up(sem_road_b); up(sem_road_a); } 
	// clean up in state B=1, need to release A
	else        { down(sem_road_b); up(sem_road_a); up(sem_road_b); } 
}

/**
 * Consumer of cars / producer of available slots 
 * for a given lane/queue. Awoken by new traffic in lane.
 */
void flag_person(
	const char direction,                 // this lanes label
	int *queue,                           // this lanes queue
	int *queue_len,                       // to index queue end
	struct csc452_sem *sem_queue,         // data lock
	struct csc452_sem *sem_queue_slots,   // task will consume slots
	struct csc452_sem *sem_honk,          // task will produce honks
	struct csc452_sem *sem_road) {        // road lock (on this lane)
	
	do {
		// sleep until honk
		printf("The flagperson is now asleep.\n");
		down(sem_honk);
		printf("The flagperson is now awake.\n");

		// consume all the cars I see
		int cars_left;
		do {
			// dequeue a car
			down(sem_queue); // competes w it's lane for excl. mutation
			(*queue_len)--;
			int car_id = queue[(*queue_len)];
			cars_left = (*queue_len);
			up(sem_queue);

			// produce a slot
			up(sem_queue_slots);

			// await road availability
			down(sem_road);
			sleep(1);
			printf("Car %d coming from the %c direction left the construction zone at time %d.\n", car_id, direction, -1);
			up(sem_road);

		} while (cars_left);

		// queue is now empty, therefore i sleep
	} while (1);
}

/**
 * Produces traffic to provided queue (integer array), generating 
 * unique car_ids from next available in car_counter, notifying a
 * flag person (consumer) to process the queue.
 */
void produce_traffic(
	const char direction,                 // this lanes label
	int *queue,                           // this lanes queue
	int *queue_len,                       // to index queue end
	struct csc452_sem *sem_queue,         // data lock
	struct csc452_sem *sem_queue_slots,   // task will consume slots
	struct csc452_sem *sem_honk,          // task will produce honks
	int *car_counter,                     // global counter / id gen
	struct csc452_sem *sem_car_counter) { // data lock

	do {

		// gain locks for queue, and id gen
		down(sem_queue);        // competes w its flag for excl. mutation
		down(sem_car_counter);  // competes w other lanes for excl. mutation

		// produce burst of cars (traffic)
		do {
			// consume a slot
			down(sem_queue_slots);

			// enqueue a car
			(*car_counter)++;
			int car_id = (*car_counter);
			queue[(*queue_len)] = car_id;
			(*queue_len)++;
			printf("Car %d coming from the %c direction arrived in the queue at time %d.\n", car_id, direction, -1);
			
			// if first car, honk
			if (*queue_len == 1) {
				up(sem_honk);
				printf("Car %d coming from the %c direction, blew their horn at time %d.\n", car_id, direction, -1);
			}

			// roll for cheance of a subsequent enqueue
		} while (rand() % 100 < 75);

		// burst done, release exclusive locks
		up(sem_car_counter);
		up(sem_queue);
		
		// sleep between bursts
		sleep(8);

	} while (1); 
}

int main() {

	if (signal(SIGINT, exit) == SIG_ERR) {
        perror("signal(SIGINT)");
        exit(EXIT_FAILURE);
    }

    pid_t pid_tp_a, // Traffic Producer A 
		pid_tp_b,   // Traffic Producer A
		pid_fp_a,   // Flag Person A
		pid_fp_b,   // Flag Person B
		pid_tc;     // Traffic Controller

	struct traffic_sim {
		int queue_a[50];
		int queue_b[50];
		int queue_len_a;
		int queue_len_b;
		struct csc452_sem *sem_queue_a;
		struct csc452_sem *sem_queue_b;
		struct csc452_sem *sem_road_a;
		struct csc452_sem *sem_road_b;
		struct csc452_sem *sem_queue_slots_a;
		struct csc452_sem *sem_queue_slots_b;
		struct csc452_sem *sem_honk_a;
		struct csc452_sem *sem_honk_b;
		struct csc452_sem *sem_car_counter;
		int car_counter;
	};

	// allocate all simulation memory in shared structure
	struct traffic_sim *sim = mmap(
		NULL, 
		sizeof(struct traffic_sim), 
		PROT_READ|PROT_WRITE, 
		MAP_SHARED|MAP_ANONYMOUS, 
		-1, 
		0
	);
	memset(sim, 0, sizeof(struct traffic_sim));

	seminit(sim->sem_queue_a, 1);
	seminit(sim->sem_queue_b, 1);
	seminit(sim->sem_road_a, 1);
	seminit(sim->sem_road_b, 1);
	seminit(sim->sem_queue_slots_a, 50);
	seminit(sim->sem_queue_slots_b, 50);
	seminit(sim->sem_honk_a, 1);
	seminit(sim->sem_honk_b, 1);
	seminit(sim->sem_car_counter, 1);
	
    pid_tp_a = fork();
    if (pid_tp_a < 0) { 
		perror("Fork failed: pid_tp_a"); 
		exit(1);
	} else if (pid_tp_a == 0) {
        printf("pid_tp_a (%d) is running.\n", getpid());
        produce_traffic(
			'A',
			sim->queue_a,
			&sim->queue_len_a,
			sim->sem_queue_a,
			sim->sem_queue_slots_a,
			sim->sem_honk_a,
			&sim->car_counter,
			sim->sem_car_counter
		);
        printf("pid_tp_a (%d) is exiting.\n", getpid());
        exit(0);
    }

	pid_tp_b = fork();
    if (pid_tp_b < 0) { 
		perror("Fork failed: pid_tp_b"); 
		exit(1);
	} else if (pid_tp_b == 0) {
        printf("pid_tp_b (%d) is running.\n", getpid());
        produce_traffic(
			'B',
			sim->queue_b,
			&sim->queue_len_b,
			sim->sem_queue_b,
			sim->sem_queue_slots_b,
			sim->sem_honk_b,
			&sim->car_counter,
			sim->sem_car_counter
		);
        printf("pid_tp_b (%d) is exiting.\n", getpid());
        exit(0);
    }

	pid_fp_a = fork();
    if (pid_fp_a < 0) { 
		perror("Fork failed: pid_fp_a"); 
		exit(1);
	} else if (pid_fp_a == 0) {
        printf("pid_fp_a (%d) is running.\n", getpid());
        flag_person(
			'A',
			sim->queue_a,
			&sim->queue_len_a,
			sim->sem_queue_a,
			sim->sem_queue_slots_a,
			sim->sem_honk_a,
			sim->sem_road_a
		);
        printf("pid_fp_a (%d) is exiting.\n", getpid());
        exit(0);
    }

	pid_fp_b = fork();
    if (pid_fp_b < 0) { 
		perror("Fork failed: pid_fp_b"); 
		exit(1);
	} else if (pid_fp_b == 0) {
        printf("pid_fp_b (%d) is running.\n", getpid());
        flag_person(
			'B',
			sim->queue_b,
			&sim->queue_len_b,
			sim->sem_queue_b,
			sim->sem_queue_slots_b,
			sim->sem_honk_b,
			sim->sem_road_b 
		);
        printf("pid_fp_b (%d) is exiting.\n", getpid());
        exit(0);
    }

	pid_tc = fork();
    if (pid_tc < 0) { 
		perror("Fork failed: pid_tc"); 
		exit(1);
	} else if (pid_tc == 0) {
        printf("pid_tc (%d) is running.\n", getpid());
        traffic_controller(
			sim->queue_a,
			sim->queue_b,
			&sim->queue_len_a,
			&sim->queue_len_b,
			sim->sem_queue_a,
			sim->sem_queue_b,
			sim->sem_road_b,
			sim->sem_road_a
		);
        printf("pid_tc (%d) is exiting.\n", getpid());
        exit(0);
    }

    waitpid(pid_tp_a, NULL, 0);
	waitpid(pid_tp_b, NULL, 0);
	waitpid(pid_fp_a, NULL, 0);
	waitpid(pid_fp_b, NULL, 0);
	waitpid(pid_tc, NULL, 0);

    printf("Successful program exit.\n");
    return 0;
}