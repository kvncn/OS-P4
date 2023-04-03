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

// ----- Structs


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
int sleepHelperMain(char*);

// ----- Global data structures/vars

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

}

/**
 * Since we do not use any service processes, this function is blank. 
 */
void phase4_start_service_processes(void) {
    fork1("SleepHelper", sleepHelperMain, NULL, USLOSS_MIN_STACK, 6);
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
    int seconds = (int) args->arg1;

    // illegal input value (negative time)
    if (seconds < 0) {
        args->arg4 = (void*) -1;
        return;
    }

    int msec = seconds * 1000;
    int status;

    // wait for the clock device to become available
    int unit = 0; // assuming clock is device unit 0
    
     // set a time for the clock interrupt
    int time = USLOSS_DeviceInput(unit, 0, &status) + msec;

    waitDevice(USLOSS_CLOCK_DEV, unit, &status);

    USLOSS_DeviceOutput(unit, unit, (void *) time);

    // wait for a clock interrupt to occur
    waitDevice(USLOSS_CLOCK_DEV, unit, &status);

    args->arg4 = (void*) 0;
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

int sleepHelperMain(char* args) {
    int intCount = 0;
    int lastTime = 0;
    // increment counter each time interrupt is received
    while (1) {
        int status; 
        // check time and if interrupt occured
        int time = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &status);
        if (time != lastTime) {
            intCount++;
            lastTime = time; 
        }

        // check if there any processes to wake up now, and
        // wake them up!, maybe it should be lastTime here idk
        if (intCount == -1) {}
    }
}