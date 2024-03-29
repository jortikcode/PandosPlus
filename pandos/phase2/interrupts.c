#include "../h/interrupts.h"

extern cpu_t exception_time; 
extern int sem[DEVICE_INITIAL];  
extern cpu_t start_usage_cpu; 
extern void copy_state(state_t *a, state_t *b); 
extern void scheduler(); 
extern pcb_PTR current_p; 

extern struct list_head ready_hq; 
extern struct list_head ready_lq; 
extern state_t *exception_state; 

/**
 * It checks the cause register to see which interrupt is pending, and then calls the appropriate
 * handler
 * 
 * @param exception_state the state of the processor when the exception occurred.
 */
void interrupt_handler(state_t* exception_state) {
    int ip = exception_state->cause & IMON;                 /* Estrazione del campo IP dal registro CAUSE */

    /* La priorità delle chiamate è implementata in base all'ordine di attivazione dei seguenti if */
    if (ip & LOCALTIMERINT) {
        plt_handler(exception_state); 
    } else if (ip & TIMERINTERRUPT) {
        interval_handler(exception_state); 
    } else if (ip & DISKINTERRUPT) {
        non_timer_interrupt(DISKINT);     
    } else if (ip & FLASHINTERRUPT) { 
        non_timer_interrupt(FLASHINT);
    } else if (ip & NETINTERRUPT) {
        non_timer_interrupt(NETWINT); 
    } else if (ip & PRINTINTERRUPT) { 
        non_timer_interrupt(PRNTINT); 
    } else if (ip & TERMINTERRUPT) { 
        non_timer_interrupt(TERMINT); 
    }
}

/**
 * The PLT handler is called when the PLT timer expires. It saves the current process state, updates
 * the process time, and inserts the process in the ready queue. Then, it calls the scheduler. 
 * 
 * @param exception_state the state of the process when the exception occurred.
 */
void plt_handler(state_t *exception_state) {
    /* 
     * Acknowledge dell'interrupt PLT: si scrive un nuovo valore nel registro Timer della CP0. 
     * Grande abbastanza per permettere allo scheduler di scegliere un altro processo da eseguire e settare il PLT a TIMESLICE del nuovo processo da eseguire.
    */
    setTIMER(10000000);

    copy_state(&(current_p->p_s), exception_state);                     /* Salvataggio dello stato di esecuzione del processo al momento dell'interrupt */ 
    current_p->p_time += exception_time - start_usage_cpu;              /* Aggiornamento del tempo del processo */
    STCK(start_usage_cpu);
    ready_by_priority(current_p); 
    current_p = NULL; 
    scheduler(); 
}

/**
 * It handles the interval timer interrupt
 * 
 * @param exception_state the state of the process that was interrupted
 */
void interval_handler(state_t *exception_state) {
    LDIT(100000);                                                       /* Acknowledge dell'interrupt dell'interval timer caricando un nuovo valore: 100ms */
    int block_flag = 0;  
    while(headBlocked(&(sem[INTERVAL_INDEX])) != NULL) {                /* Sblocco di tutti i pcb bloccati sul semaforo dell'interval timer */
        sem_operation(&(sem[INTERVAL_INDEX]),&block_flag,0); 
    } 
    
    sem[INTERVAL_INDEX] = 0;                                            /* Reset del semaforo a 0 cosìcche le successive wait_clock() blocchino i processi */ 
    if (current_p == NULL) scheduler(); 
    else {
        copy_state(&(current_p->p_s), exception_state);                 /* Salvataggio dello stato di esecuzione del processo al momento dell'interrupt */
        current_p->p_time += exception_time - start_usage_cpu;          /* Aggiornamento del tempo del processo */
        STCK(start_usage_cpu);
        LDST(exception_state);                                          /* Prosegue l'esecuzione del processo corrente */
    }
}

/**
 * It checks which device has generated the interrupt, and then calls the `acknowledge` function to
 * handle the interrupt
 * 
 * @param line the line of the interrupt
 */
void non_timer_interrupt(int line) {
    memaddr *bitmap_word_addr = (memaddr *) ((BITMAPSTRT_ADDR) + (line - 3) * 0x04); 
    int device_interrupting = get_dev_interrupting(bitmap_word_addr);                                           /* Numero del device che ha provocato l'eccezione */
    memaddr dev_reg_addr = (memaddr) (DEVREGSTRT_ADDR + ((line - 3) * 0x80) + (device_interrupting * 0x10));    /* Inidirizzo del device register del device che ha provocato l'eccezione */
    devreg_t *dev_reg = (devreg_t *) dev_reg_addr;                                                              /* Device register del device che ha generato l'interrupt */

    /* Gli interrupt dei terminali vanno distinti dagli interrupt degli altri device */
    if (line != TERMINT)
        acknowledge(device_interrupting, line, (devreg_t *) dev_reg_addr, GENERAL_INT);
    else
        if (dev_reg->term.transm_status != READY && dev_reg->term.transm_status != BUSY)
            acknowledge(device_interrupting, line, (devreg_t *) dev_reg_addr, TERMTRSM_INT);
        else
            acknowledge(device_interrupting, line, (devreg_t *) dev_reg_addr, TERMRECV_INT);
}


/**
 * It finds the semaphore associated with the device that interrupted, and unblocks the process
 * that was waiting on that semaphore. It then sets the return value of the process to the
 * status of the device that interrupted. Finally, it acknowledges the interrupt
 * 
 * @param device_interrupting the device that interrupted
 * @param line the line of the interrupt
 * @param dev_register the device register of the device that caused the interrupt
 * @param type the type of interrupt, which can be either a general interrupt, a terminal transmit
 * interrupt or a terminal receive interrupt.
 */
void acknowledge(int device_interrupting, int line, devreg_t *dev_register, int type) {
    int device_index = (line - 3) * 8 + device_interrupting + 1;                                /* Indice del semaforo su cui fare l'operazione di verhogen */

    if (line == TERMINT && type == TERMRECV_INT)
        device_index += DEVPERINT; 

    pcb_PTR to_unblock_proc = headBlocked(&(sem[device_index]));                                /* Processo da sbloccare, che è in stato di wait */

    if (to_unblock_proc != NULL) {                                                              /* Se c'è effettivamente un processo da sbloccare... */
        switch (type) {
            case GENERAL_INT:
                to_unblock_proc->p_s.reg_v0 = dev_register->dtp.status;
                dev_register->dtp.command = ACK;
                break;
            case TERMTRSM_INT:
                to_unblock_proc->p_s.reg_v0 = dev_register->term.transm_status;
                dev_register->term.transm_command = ACK;
                break;
            case TERMRECV_INT:
                to_unblock_proc->p_s.reg_v0 = dev_register->term.recv_status;
                dev_register->term.recv_command = ACK;
                break;
        }
        int block_flag = 0; 
        sem_operation(&sem[device_index],&block_flag,0); 
    }
    if (current_p == NULL) scheduler(); 
    else {
        current_p->p_time += exception_time - start_usage_cpu;
        STCK(start_usage_cpu); 
        copy_state(&(current_p->p_s), exception_state); 
        LDST(&(current_p->p_s)); 
    }
}

/**
 * It returns the number of the device that is interrupting.
 * 
 * @param bitmap_word_addr the address of the word in the bitmap that contains the bit for the device
 * that is interrupting
 */
int get_dev_interrupting(memaddr *bitmap_word_addr) {
    int device_interrupting = 0; 
    while(device_interrupting < DEVPERINT) {
        if ((*bitmap_word_addr) & (1 << device_interrupting))
            return device_interrupting; 
        device_interrupting++; 
    }
    return -1;                          /* Teoricamente non dovrebbe mai arrivare qui */ 
}