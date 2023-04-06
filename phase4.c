/**
 * AUTHORS:    Kevin Nisterenko & Rey Sanayei
 * COURSE:     CSC 452, Spring 2023
 * INSTRUCTOR: Russell Lewis
 * ASSIGNMENT: Phase4
 * DUE_DATE:   04/13/2023
 * 
 * This phase is responsible for device drivers,
 * particularly the clock device, the terminal and
 * also disk devices and their system calls. 
 */

// ----- Constants 
#define FREE 0
#define IN_USE 1
#define AWAKE 2
#define ASLEEP 3

// ----- Includes
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase4_usermode.h>
#include <usloss.h>
#include <usyscall.h>
#include <string.h>
#include <stdlib.h>

// ----- typedefs
typedef USLOSS_Sysargs sysArgs;
typedef struct sleepRequest sleepRequest; 


// ----- Structs

struct sleepRequest {
    int status;         // if the proc is awake or asleep
    long wakeUpTime;    // time to check if should wake up
    sleepRequest *next; // next proc to sleep/wake up
    int mutex;          // lock for the request
};


// ----- Function Prototypes

// Phase 4 Bootload
void phase4_init(void);
void phase4_start_service_processes(void);

// Syscall handlers
void sleepHandler(sysArgs*);
void termReadHandler(sysArgs*);
void termWriteHandler(sysArgs*);
void diskSizeHandler(sysArgs*);
void diskReadHandler(sysArgs*);
void diskWriteHandler(sysArgs*);

// Helpers
void kernelCheck(char*);
void cleanShadowEntry(int);
int sleepHelperMain(char*);
void cleanSleepEntry(int);
int getNextSleeper();

// ----- Global data structures/vars
sleepRequest sleepRequestsTable[MAXPROC];
sleepRequest* sleepRequests;
int curSleeperIdx;                        // for sleepRequest allocation

// ----- Phase 4 Bootload

/**
 * called by the testcase during bootstrap, before any process runs.
 * this function is responsible for initializing our data structures.
 */
void phase4_init(void) {
    // set the syscall handlers 
    systemCallVec[SYS_SLEEP]     = sleepHandler;
    systemCallVec[SYS_TERMREAD]  = termReadHandler;
    systemCallVec[SYS_TERMWRITE] = termWriteHandler;
    systemCallVec[SYS_DISKSIZE]  = diskSizeHandler;
    systemCallVec[SYS_DISKREAD]  = diskReadHandler;
    systemCallVec[SYS_DISKWRITE] = diskWriteHandler;

    // sleepRequest setup
    for (int i = 0; i < MAXPROC; i++) {
        cleanSleepEntry(i);
    }
    sleepRequests = NULL;
    curSleeperIdx = 0;
}

/**
 * Since we do not use any service processes, this function is blank. 
 */
void phase4_start_service_processes(void) {
    // initialize clock/sleep daemon
    int sleepHelper = fork1("SleepHelper", sleepHelperMain, NULL, USLOSS_MIN_STACK, 2);
}

// ----- Syscall Handlers

/**
 * Pauses the current process for the specified number of seconds. (The
 * delay is approximate.)
 * 
 * @param *args, USLOSS System args to receive and return 
 * params
 * 
 * @return void
*/
void sleepHandler(sysArgs* args) {
    kernelCheck("Sleep Handler");

    long msecs = (long) args->arg1; 

    // invalid param
    if (msecs < 0) {
        args->arg4 = (void *)(long)-1;
        return;
    }

    int sleepIdx = getNextSleeper();

    sleepRequest* toSleep = &sleepRequestsTable[sleepIdx];
    toSleep->wakeUpTime = currentTime() + msecs * 1000000;
    toSleep->mutex = MboxCreate(1, 0);

    // add to sleep requests queue
    if (sleepRequests == NULL) {
        sleepRequests = toSleep;
    } else {
        // iterate over queue and add toSleep to it
        sleepRequest* curr = sleepRequests;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = toSleep;
    }

    // block/sleep proc until we can wake it up
    MboxRecv(toSleep->mutex, NULL, 0);

    // return 0 as operation was successful
    args->arg4 = (void *) (long) 0;
}

/**
 * Performs a read of one of the terminals; an entire line will be read. This line will
 * either end with a newline, or be exactly MAXLINE characters long. If the syscall asks for
 * a shorter line than is ready in the buffer, only part of the buffer will be copied, 
 * and the rest will be discarded.
 * 
 * @param *args, USLOSS System args to receive and return 
 * params
 * 
 * @return void
*/
void termReadHandler(sysArgs* args) {

}

/**
 * Writes characters from a buffer to a terminal. All of the characters of the buffer
 * will be written atomically; no other process can write to the terminal until they
 * have flushed.
 * 
 * @param *args, USLOSS System args to receive and return 
 * params
 * 
 * @return void
*/
void termWriteHandler(sysArgs* args) {

}

/**
 * Queries the size of a given disk. It returns three values, all as out-parameters:
 * the number of bytes in a block: the number of blocks in a track; and the number
 * of tracks in a disk.
 * 
 * @param *args, USLOSS System args to receive and return 
 * params
 * 
 * @return void
*/
void diskSizeHandler(sysArgs* args) {

}

/**
 * Reads a certain number of blocks from disk, sequentially. Once begun, the entire
 * read is atomic; no other syscalls will be able to access the disk.
 * 
 * @param *args, USLOSS System args to receive and return 
 * params
 * 
 * @return void
*/
void diskReadHandler(sysArgs* args) {

}

/**
 * Writes s a certain number of blocks from disk, sequentially. Once begun, the entire
 * write is atomic; no other syscalls will be able to access the disk.
 * 
 * @param *args, USLOSS System args to receive and return 
 * params
 * 
 * @return void
*/
void diskWriteHandler(sysArgs* args) {

}

// ----- Helper Functions

/**
 * Checks whether or not the simulation is running in kernel mode and halts the
 * entire simulation if so. 
 * 
 * @param func, char pointer representing the functions' name so we can output 
 * a good error message as to what process tried running in user mode
 */
void kernelCheck(char* func) {
    // means we are running in user mode, so we halt simulation
    if (!(USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE)) {
		USLOSS_Console("ERROR: Someone attempted to call %s while in user mode!\n", func);
		USLOSS_Halt(1);
	}
}

/**
 * Helper to change from kernel to user mode. 
*/
void changeMode() {
    kernelCheck("changeMode");
    int chng = USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

/**
 * Main function for the daemon process responsible for checking
 * wake up times for the sleep device, if a process wake up time
 * has arrived it will wake it up and keep track of the next 
 * request. 
 * 
 * @param args, char pointer for the main function arguments
 * 
 * @return int representing if the exit status was normal
 */
int sleepHelperMain(char* args) {
    int status; 
    
    // increment counter each time interrupt is received
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);

        sleepRequest *proc = sleepRequests;
        
        // if we still have processes to wake up, do 
        // so 
        while (proc != NULL) {
            // if we can wake up one process, do so, otherwise check along
            // the queue
            if (proc->status == ASLEEP && proc->wakeUpTime < currentTime()) {
                proc->status = AWAKE;
                MboxSend(proc->mutex, NULL, 0);
                break;
            }
            proc = proc->next;
        }
    }
    return 0; 
}

/**
 * Helper for cleaning/initializing a sleeper entry to the default/zero
 * values. 
 * 
 * @param slot, int representing index into the sleepRequestTable
 */
void cleanSleepEntry(int slot) {
    sleepRequestsTable[slot].mutex = 0;
    sleepRequestsTable[slot].next = NULL;
    sleepRequestsTable[slot].status = FREE;
    sleepRequestsTable[slot].wakeUpTime = 0;
}

/**
 * Tries to find the next free sleeper slot in the global array and
 * returns the index so we can access that sleep request. 
 * 
 * @return int representing the index into the array
 */
int getNextSleeper() {
    int count = 0;
    // in a circular fashion, try to find the next free process
    // in the sleeper table
	while (sleepRequestsTable[curSleeperIdx % MAXPROC].status != FREE) {
		if (count < MAXPROC) {
            count++;
		    curSleeperIdx++;
        } else {
            return -1;
        }
	}
    
	return curSleeperIdx % MAXPROC;

}