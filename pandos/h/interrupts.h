#ifndef INTERRUPTS
#define INTERRUPTS

#include "./types.h"
#include "./pandos_const.h"
#include "./pandos_types.h"
#include "./exceptions.h"
#include "./asl.h"

/* UTILITY CONSTANTS */
#define BITMAPSTRT_ADDR 0x10000040
#define DEVREGSTRT_ADDR 0x10000054
#define NETINTERRUPT 0x00002000
#define GENERAL_INT 0
#define TERMTRSM_INT 1
#define TERMRECV_INT 2


void interrupt_handler(state_t* exception_state); 

/* Interrupt Handlers */

/* Handler degli interrupt non di tipo timer */
void non_timer_interrupt(int line); 

/* 
    Funzione di Acknowledge per I/O interrupts. 
    Questa funzione si occupa di fare ACK sul device register e di fare una v sul semaforo del dispositivo appropriato. 
*/
void acknowledge(int device_interrupting, int line, devreg_t *dev_register, int type); 

/* Utility functions */

/* Questa funzione ritorna un intero che rappresenta il numero del device che ha generato l'interrupt. Ritorna -1 se non trova nessun device valido.*/
int get_dev_interrupting(memaddr bitmap_word_addr); 

#endif