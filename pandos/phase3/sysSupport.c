#include "../h/sysSupport.h"

//Variabile che rappresenta lo stato al momento dell'eccezione del processo corrente
state_t *exception_state; 

void general_exception_handler() {
    support_t* curr_support = (support_t*) SYSCALL(GETSUPPORTPTR, 0, 0, 0);     /* SYS8 to get the supp struct's addrs of the curr proc */
    // Ricavo lo stato al momento dell'eccezione
    exception_state = &(curr_support->sup_exceptState[GENERALEXCEPT]); 

	// Estrazione del Cause.ExcCode
    int exe_code = exception_state->cause & GETEXECCODE;
    if (9 <= exe_code && exe_code >= 12)
        terminate();
    // Intero che rappresenta la syscall chiamata
    int syscode = exception_state->reg_a0;

    switch (syscode) {
        case GET_TOD: {
            get_tod();
        }
        case TERMINATE: {
            terminate();
        }
        case WRITEPRINTER: {
            write_to_printer();
        }
        case WRITETERMINAL: {
            write_to_terminal();
        }
        case READTERMINAL: {
            read_from_terminal();
        }
        default: {

        }
    }

}

/* NSYS11 */
void get_tod () {
    unsigned int tod; 
    STCK(tod); 
    exception_state->reg_v0 = tod; 
}

/* NSYS12 */
void terminate () {
   /**
    * If the process to be terminated is currently holding mutual exclusion on
    * a Support Level semaphore (e.g. Swap Pool semaphore), mutual exclusion must
    * first be released (NSYS4) before invoking the Nucleus terminate command (NSYS2).
    */
}

/* NSYS13 */
void write_to_printer () {
    
}

/* NSYS14 */
void write_to_terminal () {
    
}

/* NSYS15 */
void read_from_terminal () {
    
}
