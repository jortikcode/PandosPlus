#include "../h/exceptions.h"


void exception_handler(){
    /* Recupero dello stato al momento dell'eccezione del processore */
    exception_state = BIOSDATAPAGE; 

    /* And bitwise per estrarre il Cause.ExcCode */
    int cause = exception_state->cause & GETEXECCODE; 

    /* Shift per migliore manipolazione */
    cause = cause >> 2; 

    /* Switch per la gestione dell'eccezione */
    switch(cause){
        case IOINTERRUPTS:                                  /* Si è verificato un interrupt */
            interrupt_handler(); 
            break; 
        case 1:                                             /* Si è verificata una eccezione TLB*/ 
        case TLBINVLDL:
        case TLBINVLDS:
            tlb_handler(); 
            break; 
        case SYSEXCEPTION:                                  /* E' stata chiamata una system call */
            if (exception_state->status & USERPON)          /* La chiamata è avvenuta in kernel mode */
                syscall_handler(); 
            else                                            /* La chiamata non è avvenuta in kernel mode */
                trap_handler();
            break;
        default:                                            /* trap_hendler starts when a {SYSC in user-mode | non-existent nucleus service} request arrives */
            trap_handler(); 
            break; 
    }

}

void syscall_handler(){
    /* Intero che rappresenta il tipo di system call */
    int syscode = exception_state->reg_a0;

    //TODO: this shit must be retuned -> (current_p->p_s).reg_v0 = returnValue;
    unsigned int returnValue = exception_state->reg_v0;                     /* Valore di ritorno da syscall_handler() */
    
    /* Switch per la gestione della syscall */
    switch(syscode){
        case CREATEPROCESS:
            state_t *a1_state = (state_t *) exception_state->reg_a1; 
            int a2_p_prio = (int) exception_state->reg_a2; 
            support_t *a3_p_support_struct = (support_t *) exception_state->reg_a3; 

            create_process(a1_state, a2_p_prio, a3_p_support_struct); 
            break; 
        case TERMPROCESS:
            /* PID del processo chiamante */
            int a2_pid = exception_state->reg_a2; 

            terminate_process(a2_pid); 
            break; 
        case PASSEREN:
            int *a1_semaddr = exception_state->reg_a1;
            passeren(a1_semaddr);
            break; 
        case VERHOGEN:
            int *a1_semaddr = exception_state->reg_a1;
            verhogen(a1_semaddr);
            break; 
        case DOIO:
            int *a1_cmdAddr = exception_state->reg_a1;
            int a2_cmdValue = exception_state->reg_a2;
            do_io(a1_semaddr, a2_cmdValue);
            break; 
        case GETTIME:
            get_cpu_time();
            break; 
        case CLOCKWAIT:
            wait_for_clock();
            break; 
        case GETSUPPORTPTR:
            returnValue = (unsigned int) get_support_data();
            break; 
        case GETPROCESSID:
            int a1_parent = exception_state->reg_a1;

            //TODO: understand what to do with the returned value
            // ? = get_processor_id(a1_parent);
            break; 
        case YIELD:
            yield();
            break; 
    }

}

void create_process(state_t *a1_state, int a2_p_prio, support_t *a3_p_support_struct){
    /* Tentativo di allocazione di un nuovo processo */
    pcb_PTR new_proc = allocPcb(); 

    if (new_proc != NULL){                           /* Allocazione avvenuta con successo */
        /* Il nuovo processo è figlio del processo chiamante */
        insertChild(current_p,new_proc); 

        /* Inizializzazione campi del nuovo processo a partire da a1,a2,a3 */
        new_proc->p_s = *a1_state; 
        new_proc->p_prio = a2_p_prio; 
        new_proc->p_supportStruct = a3_p_support_struct; 

        /* PID è implementato come l'indirizzo del pcb_t */
        new_proc->p_pid = new_proc; 


        /* Il nuovo processo è pronto per essere messo nella ready_q */
        switch(new_proc->p_prio){
            case 0:                                  /* E' un processo a bassa priorità */
                insertProcQ(&(ready_lq->p_list),new_proc); 
                break;
            default:                                 /* E' un processo ad alta priorità */
                insertProcQ(&(ready_hq->p_list),new_proc); 
                break; 
        }

        /* Operazione completata, ritorno con successo */
        current_p->p_s.reg_v0 = new_proc->p_pid; 
    }else                                           /* Allocazione fallita, risorse non disponibili */
        current_p->p_s.reg_v0 = NOPROC; 

    //TODO: Leggere e implementare sezione 3.5.12 manuale pandosplus per il "return from a syscall"
}

void terminate_process(int a2_pid){
    pcb_PTR old_proc; 
    if (a2_pid == 0){                                      
        old_proc = current_p; 
        /* Terminazione del processo corrente */
        current_p = NULL; 
    }else{
        /* PID è implementato come indirizzo del PCB */
        old_proc = a2_pid; 
    }
    /* Rimozione di old_proc dalla lista dei figli del suo padre */
    outChild(old_proc); 
    /* Terminazione di tutta la discendenza di old_proc */
    if (old_proc != NULL) terminate_all(old_proc); 

    /* Scheduling di un nuovo processo: il processo corrente e' stato terminato */
    if (current_p == NULL) scheduler(); 
}

void terminate_all(pcb_PTR old_proc){
    if (old_proc != NULL){
        pcb_PTR child; 
        while(child = removeChild(old_proc))
            terminate_all(child); 

        /* 
            Task di terminazione di un processo (sezione 3.9, manuale pandosplus) 
        */


        /* Aggiornamento semafori / variabile di conteggio dei bloccati su I/O */
        if (old_proc->p_semAdd != NULL)
            if ((old_proc->p_semAdd >= &(sem[0])) && (old_proc->p_semAdd <= &(sem[DEVICE_EXCEPTIONS])))
                soft_counter -= 1;
            else
                *(old_proc->p_semAdd) += 1; 
        else{
            /* Rimozione dalla coda dei processi ready */
            switch (old_proc->p_prio){
            case 0:
                outProcQ(&(ready_lq->p_list), old_proc);
                break;
            default:
                outProcQ(&(ready_hq->p_list), old_proc);
                break;
            }
        }
        p_count -= 1;
        /* Inserimento di old_proc nella lista dei pcb liberi, da allocare */
        freePcb(old_proc); 
    }
}

void passeren (int *a1_semaddr) {
    
}

void verhogen (int *a1_semaddr) {
    
}

int do_io(int *a1_cmdAddr, int a2_cmdValue) {

}

int get_cpu_time() {

}

int wait_for_clock() {

}

support_t* get_support_data() {
    return current_p->p_supportStruct;
}

int get_processor_id(int a1_parent) {
    if (a1_parent == 0)
        return current_p->p_pid;
    else
        return (current_p->p_parent)->p_pid;
}

int yield() {
    //TODO: Sospendere current_p (non so se basti rimuoverlo)
    removeProcQ(&(current_p->p_list));            // Ora come ora, la funzione removeProcQ rimuove l'elemento più vecchio della coda, non se servano modifiche alla semantica o meno.
    if (current_p->p_prio == 1)
        insertProcQ(ready_hq, current_p);
    else
        insertProcQ(ready_lq, current_p);
}