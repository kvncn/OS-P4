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
#include <stdio.h>

// ----- typedefs
typedef USLOSS_Sysargs sysArgs;
typedef struct sleepRequest sleepRequest; 
typedef struct diskRequest diskRequest; 

// ----- Structs

struct sleepRequest {
    int status;         // if the proc is awake or asleep
    long wakeUpTime;    // time to check if should wake up
    sleepRequest* next; // next proc to sleep/wake up
    int mutex;          // lock for the request
};

struct diskRequest {
    int pid;
    int track;
    int first;
    int sector; 
    void* buffer; 
    int op;
    int mboxID; 
    diskRequest* next; 
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
void cleanDiskEntry(int);
int sleepHelperMain(char*);
void cleanSleepEntry(int);
int getNextSleeper();
int termHelperMain(char*);
int diskHelperMain(char*);
void diskSeek(int, int);
int diskReader(int, int, int, int, void*);
void diskQueueHelper(int, int, int);

// ----- Global data structures/vars

// sleep
sleepRequest sleepRequestsTable[MAXPROC];
sleepRequest* sleepRequests;
int curSleeperIdx;      

// terminal
char termLines[USLOSS_TERM_UNITS][MAXLINE]; 
int termLineIdx[USLOSS_TERM_UNITS];        
int termRead[USLOSS_TERM_UNITS];           
int termReadyWrite[USLOSS_TERM_UNITS];            
int termWriteMutex[USLOSS_TERM_UNITS];

// disk
diskRequest diskRequestsTable[MAXPROC];
int disk0;
int disk0Q;
int disk1;
int disk1Q;
int disk0Mutex;
int disk1Mutex;
int disk0MutexTrack;
int disk1MutexTrack;
int globaldisk;
int disk0NumTracks;
int disk1NumTracks;
diskRequest* disk0Req;
diskRequest* disk1Req;

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

    // terminal initialization
    memset(termLines, '\0', sizeof(termLines));
    memset(termLineIdx, 0, sizeof(termLineIdx));
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void*)(long)USLOSS_TERM_CTRL_RECV_INT(1));
        termRead[i] = MboxCreate(10, MAXLINE);
        termReadyWrite[i] = MboxCreate(1, 0);
        termWriteMutex[i] = MboxCreate(1, 0);
    }

    // diskRequest setup
    for (int i = 0; i < MAXPROC; i++) {
        cleanDiskEntry(i);
    }

    // setup all mutexes
    disk0 = MboxCreate(1, 0);
    disk0Q = MboxCreate(1, 0);
    disk1 = MboxCreate(1, 0);
    disk1Q = MboxCreate(1, 0);
    disk0Mutex = MboxCreate(1, 0);
    disk1Mutex = MboxCreate(1, 0);
    disk0MutexTrack = MboxCreate(1, 0);
    disk1MutexTrack = MboxCreate(1, 0);

    // setup linked lists
    disk0Req = NULL;
    disk1Req = NULL;
}

/**
 * Since we do not use any service processes, this function is blank. 
 */
void phase4_start_service_processes(void) {
    // initialize clock/sleep daemon
    int sleepHelper = fork1("SleepHelper", sleepHelperMain, NULL, USLOSS_MIN_STACK, 2);
    
    
    // initialize terminal daemons
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        char termName[128];
        sprintf(termName, "Terminal Driver %d", i);
        char termUnit[100];
        sprintf(termUnit, "%d", i);

        int termPID = fork1(termName, termHelperMain, termUnit, USLOSS_MIN_STACK, 2);
    }

    // initialize disk daemons
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        // buffer for the driver to know which unit to use
        char buffer[10];
        sprintf(buffer, "%d", i);

        char process[20];
        sprintf(process, "Disk Driver %d", i);

        int diskPID = fork1(process, diskHelperMain, buffer, USLOSS_MIN_STACK, 2);
    }


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
    kernelCheck("sleepHandler");

    long msecs = (long) args->arg1; 

    // invalid param
    if (msecs < 0) {
        args->arg4 = (void *)(long)-1;
        return;
    }

    int sleepIdx = getNextSleeper();

    // allocate the sleep request
    sleepRequest* toSleep = &sleepRequestsTable[sleepIdx];
    toSleep->wakeUpTime = currentTime() + msecs * 1000000;
    toSleep->mutex = MboxCreate(1, 0);
    toSleep->status = ASLEEP;

    // add to sleep requests queue
    if (sleepRequests == NULL) {
        sleepRequests = toSleep;
    } else {
        sleepRequest* rest = sleepRequests;
        toSleep->next = rest; 
        sleepRequests = toSleep; 
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
    kernelCheck("termReadHandler");

    char* location = (char*) args ->arg1;
    int locationLen = (int)(long) args ->arg2;
    int termUnit = (int)(long) args ->arg3;

    if (location == NULL || locationLen <= 0 || termUnit < 0 || termUnit >= USLOSS_TERM_UNITS) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    // buffer to store line read
    char line[MAXLINE];

    // receive line from mailbox
    int lineLen = MboxRecv(termRead[termUnit], &line, MAXLINE);

    // if the location is less than the lineLen, we need to bound lineLen
    if (locationLen < lineLen) {
        lineLen = locationLen;
    }
    memcpy(location, line, lineLen);

    // set return values
    args->arg2 = (void*)(long)lineLen;
    args->arg4 = (void*)(long)0;

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
    kernelCheck("termWriteHandler");

    char* location = (char*) args ->arg1;
    int locationLen = (int)(long) args ->arg2;
    int termUnit = (int)(long) args ->arg3;

    if (location == NULL || locationLen <= 0 || termUnit < 0 || termUnit >= USLOSS_TERM_UNITS) {
        args->arg4  = (void*)(long)-1;
        return;
    }

    // acquire lock to work on terminal
    MboxSend(termWriteMutex[termUnit], NULL, 0);

    for (int i = 0; i < locationLen; i++) {
        // if we are not ready, block
        MboxRecv(termReadyWrite[termUnit], NULL, 0);

        // get the control value
        int ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(0);
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        ctrl = USLOSS_TERM_CTRL_CHAR(ctrl, location[i]);

        // update the control while writing to character
        int devOut = USLOSS_DeviceOutput(USLOSS_TERM_DEV, termUnit, (void*)(long)ctrl);
    }

    // set return values
    args->arg2 = (void*)(long)locationLen;
    args->arg4 = (void*)(long)0;

    // release lock as we stopped work on the terminal
    MboxRecv(termWriteMutex[termUnit], NULL, 0);
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
    kernelCheck("diskSizeHandler");

    int unit = (long)args->arg1;
    int daemonMutexTrack = -1;

    // setup daemon mutex
    if (unit == 0) {
        daemonMutexTrack = disk0MutexTrack;
    } else {
        daemonMutexTrack = disk1MutexTrack;
    }

    MboxRecv(daemonMutexTrack, NULL, 0);

    // set the appropriate syscall output values
    args->arg1 = (void *)USLOSS_DISK_SECTOR_SIZE;
    args->arg2 = (void *)USLOSS_DISK_TRACK_SIZE;
    args->arg3 = (void *)(long) globaldisk;
    args->arg4 = (void *)0;

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
    kernelCheck("diskReadHandler");

    void *buffer = args->arg1;
    int sectors = (int)(long)args->arg2;
    int track = (int)(long)args->arg3;
    int first = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;

    args->arg1 = (void *)(long)diskReader(unit, track, first, sectors, buffer);
    args->arg4 = (void *)(long)0;

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
    kernelCheck("diskWriteHandler");

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

/**
 * Main function for the daemon process responsible for checking
 * for terminal interrupts, if its ready to read or write, it
 * performs the operation accordingly. 
 * 
 * @param args, char pointer for the main function arguments
 * 
 * @return int representing if the exit status was normal
 */
int termHelperMain(char* args) {
    int status; 

    // get the terminal unit
    int termUnit = atoi(args);
    
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, termUnit, (void *)(long)USLOSS_TERM_CTRL_RECV_INT(0));
    
    while (1) {
        waitDevice(USLOSS_TERM_DEV, termUnit, &status);

        // read the receive field of the device
        int recv = USLOSS_TERM_STAT_RECV(status);

        // received input
        if (recv == USLOSS_DEV_BUSY) {
            char character = USLOSS_TERM_STAT_CHAR(status);
            // find end of input
            if (character == '\n' || termLineIdx[termUnit] == MAXLINE) {
                // if we arent at the limit
                if (termLineIdx[termUnit] != MAXLINE) {
                    // just add to current line
                    termLines[termUnit][termLineIdx[termUnit]] = character;
                    termLineIdx[termUnit]++;
                }
                // send line to mailbox so we can access it 
                MboxCondSend(termRead[termUnit], termLines[termUnit], termLineIdx[termUnit]);
                // reset pointer of line
                memset(termLines[termUnit], '\0', sizeof(termLines[termUnit]));
                termLineIdx[termUnit] = 0;
            } else {
                // just add to current line
                termLines[termUnit][termLineIdx[termUnit]] = character;
                termLineIdx[termUnit]++;
            }
        // if we can't receive
        } else if (recv == USLOSS_DEV_ERROR) {
            USLOSS_Console("USLOSS_DEV_ERROR. Terminating simulation.\n");
            USLOSS_Halt(1);
        }

        // now this means we can check for writes
        // read the Xmit field from the status register
        int xmit = USLOSS_TERM_STAT_XMIT(status);
        // if terminal is ready to write new character
        if (xmit == USLOSS_DEV_READY) {
            // mark terminal as ready to write
            MboxCondSend(termReadyWrite[termUnit], NULL, 0);
        // if we can't write yet
        } else if (xmit == USLOSS_DEV_ERROR) {
            USLOSS_Console("USLOSS_DEV_ERROR. Terminating simulation.\n");
            USLOSS_Halt(1);
        }
    }
    return 0; 
}

/**
 * Helper for cleaning/initializing a disk entry to the default/zero
 * values. 
 * 
 * @param slot, int representing index into the diskRequestTable
 */
void cleanDiskEntry(int slot) {
    diskRequestsTable[slot].pid = -1;
    diskRequestsTable[slot].next = NULL;
    diskRequestsTable[slot].mboxID = MboxCreate(1, 0);
}

/**
 * Main function for the daemon process responsible for checking
 * disk requests. 
 * 
 * @param args, char pointer for the main function arguments
 * 
 * @return int representing if the exit status was normal
 */
int diskHelperMain(char* args) {
    int daemonMbox;
    int daemonQMbox;
    int daemonMutex;
    int daemonMutexTrack;
    int diskUnit;


    int res;
    int status;

    diskRequest** diskQPtr = NULL;
    diskRequest* diskQ = NULL;
    USLOSS_DeviceRequest request; 

    // select appropriate disk
    if (*args == '0') {
        diskUnit = 0;
        daemonMbox = disk0;
        daemonQMbox = disk0Q;
        daemonMutex = disk0Mutex;
        daemonMutexTrack = disk0MutexTrack;
        diskQPtr = &disk0Req;
    } else {
        diskUnit = 1;
        daemonMbox = disk1;
        daemonQMbox = disk1Q;
        daemonMutex = disk1Mutex;
        daemonMutexTrack = disk1MutexTrack;
        diskQPtr = &disk1Req;
    }

    // get number of tracks
    request.opr = USLOSS_DISK_TRACKS;
    request.reg1 = (void*)(long)&globaldisk;

    // enable and receive the request
    int retval = USLOSS_DeviceOutput(USLOSS_DISK_DEV, diskUnit, &request);
    waitDevice(USLOSS_DISK_DEV, diskUnit, &status);

    // keep the number of tracks
    if (diskUnit == 0) disk0NumTracks = globaldisk;
    else disk1NumTracks = globaldisk;

    // now we can acquire the lock to work on this disk
    MboxSend(daemonMutexTrack, NULL, 0);

    // daemon work
    while (1) {
        // wait for syscall
        MboxRecv(daemonMbox, NULL, 0);

        while (*diskQPtr != NULL) {
            diskQ = *diskQPtr;

            int mboxID = diskQ->mboxID;
            int track = diskQ->track;
            int first = diskQ->first;
            int sector = diskQ->sector;
            void *buffer = diskQ->buffer;
            int op = diskQ->op;

            diskSeek(diskUnit, track);

            // setup the USLOSS struct
            request.opr = op;
            request.reg1 = (void*)(long)first;
            request.reg2 = buffer;

            for (int i = 0; i < sector; i++) {
                // if we were to go over, tart from beginning
                if ((int)(long)request.reg1 == USLOSS_DISK_TRACK_SIZE) {
                    request.reg1 = 0;
                    track++;
                    diskSeek(diskUnit, track);
                }

                // acquire lock since we will send a USLOSS request
                MboxSend(daemonMutex, NULL, 0);

                // send the request
                res = USLOSS_DeviceOutput(USLOSS_DISK_DEV, diskUnit, &request);
                waitDevice(USLOSS_DISK_DEV, diskUnit, &status);

                // since we finish our work (send request), release lock
                MboxRecv(daemonMutex, NULL, 0);

                // increment request pointer to move sectors
                request.reg1++;
                request.reg2 += USLOSS_DISK_SECTOR_SIZE;
            }

            // acquire the lock on the queue since we will change it
            MboxSend(daemonQMbox, NULL, 0);
            
            *diskQPtr = (*diskQPtr)->next;
            diskQ->next = NULL;

            // release the lock on the queue
            MboxRecv(daemonQMbox, NULL, 0);

            MboxSend(mboxID, NULL, 0);
        }
    }
    return 0;
}

/**
 * Seeks the given disk for the appropriate task. 
 * 
 * @param unit, int representing the disk unit
 * @param track, int representing the track to
 * search for
 */
void diskSeek(int unit, int track) {
    int result;
    int status;

    int daemonMutex = 1;

    // check for the unit
    if (unit == 0) {
        daemonMutex = disk0Mutex;
    } else {
        daemonMutex = disk1Mutex;
    }

    // set up the request struct
    USLOSS_DeviceRequest request; 
    request.opr = USLOSS_DISK_SEEK;
    request.reg1 = (void*)(long)track;

    // acquire lock to work on it
    MboxSend(daemonMutex, NULL, 0);

    result = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
    waitDevice(USLOSS_DISK_DEV, unit, &status);

    // release lock
    MboxRecv(daemonMutex, NULL, 0);
}

/**
 * Helper function for the read syscall. 
 * 
 * @param unit, int representing the disk unit
 * @param track, int representing the track to search for
 * @param first, int representing the first track on the disk
 * @param sectors, int representing the sectors on the disk
 * @param buffer, void* representing the buffer of the disk 
 * 
 * @return int 0 if the opertaion was sucessful
 */
int diskReader(int unit, int track, int first, int sectors, void* buffer) {
    int pid = getpid();

    int daemonQMbox = -1;
    int daemonMbox = -1;

    if (unit == 0) {
        daemonQMbox = disk0Q;
        daemonMbox = disk0;
    } else {
        daemonQMbox = disk1Q;
        daemonMbox = disk1;
    }

    // Set up the values in the shadow proc table so that the daemon can use them when
    // it removes us from the queue
    diskRequestsTable[pid % MAXPROC].pid = pid;
    diskRequestsTable[pid % MAXPROC].track = track;
    diskRequestsTable[pid % MAXPROC].first = first;
    diskRequestsTable[pid % MAXPROC].sector = sectors;
    diskRequestsTable[pid % MAXPROC].buffer = buffer;
    diskRequestsTable[pid % MAXPROC].op = USLOSS_DISK_READ;

    // acquire the lock since we want to add ourselves to the queue
    MboxSend(daemonQMbox, NULL, 0);

    // call the queue helper
    diskQueueHelper(unit, pid, daemonQMbox);

    // release the lock 
    MboxRecv(daemonQMbox, NULL, 0);

    // wake up the disk daemon
    MboxCondSend(daemonMbox, NULL, 0);
    MboxRecv(diskRequestsTable[pid % MAXPROC].mboxID, NULL, 0);

    return 0;
}

/**
 * Adds a request to the disk request queue.
 * 
 * @param unit, int representing the disk unit
 * 
 */
void diskQueueHelper(int unit, int pid, int mboxToSend) {

}