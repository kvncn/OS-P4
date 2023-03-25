/**
 * AUTHORS:    Kevin Nisterenko & Rey Sanayei
 * COURSE:     CSC 452, Spring 2023
 * INSTRUCTOR: Russell Lewis
 * ASSIGNMENT: Phase4
 * DUE_DATE:   04/13/2023
 * 
 * 
 */

// ----- Constants 
#define FREE 0
#define IN_USE 1

// ----- Includes
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <usloss.h>
#include <usyscall.h>
#include <string.h>
#include <stdlib.h>

// ----- typedefs

// ----- Structs


// ----- Function Prototypes

// Phase 3 Bootload
void phase4_init(void);
void phase4_start_service_processes(void);

// Helpers

// ----- Global data structures/vars

// ----- Phase 4 Bootload

/**
 * called by the testcase during bootstrap, before any process runs.
 * this function is responsible for initializing our data structures.
 */
void phase4_init(void) {

}

/**
 * Since we do not use any service processes, this function is blank. 
 */
void phase4_start_service_processes(void) {}

// ----- Syscall Handlers


// ----- Helper Functions
