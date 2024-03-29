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

void interrupt_handler(state_t* exception_state) {
    // Estrazione del campo IP dal registro CAUSE
    int ip = exception_state->cause & IMON;                 
    
    // La priorità delle chiamate è implementata in base all'ordine di attivazione dei seguenti if
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


void plt_handler(state_t *exception_state) {
    /* 
     * Acknowledge dell'interrupt PLT: si scrive un nuovo valore nel registro Timer della CP0. 
     * Grande abbastanza per permettere allo scheduler di scegliere un altro processo da eseguire e settare il PLT a TIMESLICE del nuovo processo da eseguire.
    */
    setTIMER(10000000);

    // Salvataggio dello stato di esecuzione del processo al momento dell'interrupt
    copy_state(&(current_p->p_s), exception_state);                     
    // Aggiornamento del tempo del processo
    current_p->p_time += exception_time - start_usage_cpu;              
    STCK(start_usage_cpu);
    ready_by_priority(current_p); 
    current_p = NULL; 
    scheduler(); 
}

void interval_handler(state_t *exception_state) {
    // Acknowledge dell'interrupt dell'interval timer caricando un nuovo valore: 100ms
    LDIT(100000);                                                       
    int block_flag = 0;  
    // Sblocco di tutti i pcb bloccati sul semaforo dell'interval timer
    while(headBlocked(&(sem[INTERVAL_INDEX])) != NULL) {                
        sem_operation(&(sem[INTERVAL_INDEX]),&block_flag,0); 
    } 
    
    // Reset del semaforo a 0 cosìcche le successive wait_clock() blocchino i processi
    sem[INTERVAL_INDEX] = 0;                                            
    if (current_p == NULL) scheduler(); 
    else {
        // Salvataggio dello stato di esecuzione del processo al momento dell'interrupt
        copy_state(&(current_p->p_s), exception_state);                 
        // Aggiornamento del tempo del processo
        current_p->p_time += exception_time - start_usage_cpu;          
        STCK(start_usage_cpu);
        // Prosegue l'esecuzione del processo corrente
        LDST(exception_state);                                          
    }
}

void non_timer_interrupt(int line) {
    /* 
    Per le linee di interrupt da 3 a 7 è necessario identificare il device sulla linea che ha provocato l'interrupt.
	Questo si fa attraverso la interrupting device bitmap, un'area di memoria di 4 word che inizia all'indirizzo 0x10000040. 
    Come funziona la interrupting device bitmap? 
	Quando il bit i della word j è posto a 1 allora il device i della linea j+3 ha un interrupt in attesa su tale linea. 
    La bitmap è gestita automaticamente dall'hardware => noi non ci dobbiamo preoccupare solo di farne l'acknowledgement.
    */

    memaddr *bitmap_word_addr = (memaddr *) ((BITMAPSTRT_ADDR) + (line - 3) * 0x04); 
    // Numero del device che ha provocato l'eccezione
    int device_interrupting = get_dev_interrupting(bitmap_word_addr);                                           
    // Inidirizzo del device register del device che ha provocato l'eccezione
    memaddr dev_reg_addr = (memaddr) (DEVREGSTRT_ADDR + ((line - 3) * 0x80) + (device_interrupting * 0x10));    
    // Device register del device che ha generato l'interrupt
    devreg_t *dev_reg = (devreg_t *) dev_reg_addr;                                                              

    // Gli interrupt dei terminali vanno distinti dagli interrupt degli altri device
    if (line != TERMINT)
        acknowledge(device_interrupting, line, (devreg_t *) dev_reg_addr, GENERAL_INT);
    else
        if ((dev_reg->term.transm_status & TERMSTATMASK) != READY && (dev_reg->term.transm_status & TERMSTATMASK) != BUSY)
            acknowledge(device_interrupting, line, (devreg_t *) dev_reg_addr, TERMTRSM_INT);
        else
            acknowledge(device_interrupting, line, (devreg_t *) dev_reg_addr, TERMRECV_INT);
}


void acknowledge(int device_interrupting, int line, devreg_t *dev_register, int type) {
    // Indice del semaforo su cui fare l'operazione di verhogen
    int device_index = (line - 3) * 8 + device_interrupting + 1;                                
    if (line == TERMINT && type == TERMRECV_INT){
        device_index += DEVPERINT; 
    }
    // Processo da sbloccare, che è in stato di wait
    pcb_PTR to_unblock_proc = headBlocked(&(sem[device_index]));                                
    if (to_unblock_proc != NULL) {                                                              
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

int get_dev_interrupting(memaddr *bitmap_word_addr) {
    int device_interrupting = 0; 
    while(device_interrupting < DEVPERINT) {
        if ((*bitmap_word_addr) & (1 << device_interrupting))
            return device_interrupting; 
        device_interrupting++; 
    }
    return -1;                          
}