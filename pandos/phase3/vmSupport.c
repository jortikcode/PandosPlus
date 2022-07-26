#include "../h/vmSupport.h"

/* Swap pool mutex */
int swap_pool_semaphore = 1; 
/* Swap pool */
swap_t swap_pool[POOLSIZE]; 

extern pcb_PTR current_p; 

void pager(){
	// Recupero della struttura di supporto del processo corrente
	support_t *curr_support = (support_t *) SYSCALL(GETSUPPORTPTR, 0, 0, 0); 
	// Estrazione del Cause.ExcCode
	int cause = curr_support->sup_exceptState[0].cause & GETEXECCODE; 
	cause >>= 2; 

	if (cause == 1){
		// TLB-Modification exception, si gestisce come una program trap
	}
	
	// Acquisizione della mutua esclusione sulla swap pool table
	SYSCALL(PASSEREN, &swap_pool_semaphore, 0, 0); 
	
	// Acquisizione del numero della pagina da caricare in memoria
	int page_missing = (curr_support->sup_exceptState[0].entry_hi - KUSEG) >> VPNSHIFT; 

	int victim_frame = -1; 
	// Ciclo per trovare un frame libero nella swap_pool
	while(++victim_frame < POOLSIZE)
		if (swap_pool[victim_frame].sw_asid == NOPROC)
			break; 
	
	// Non è stato trovato un frame libero, si deve chiamare l'algoritmo di rimpiazzamento
	if (victim_frame == POOLSIZE)
		victim_frame = replacement_algorithm(); 
	
	int frame_asid = swap_pool[victim_frame].sw_asid; 
	// Il frame "vittima" è occupato dalla pagina di un processo
	if (frame_asid != NOPROC){
		// Disabilitazione degli interrupt
		setSTATUS(getSTATUS() & DISABLEINTS); 

		// Marcatura della page table entry come non valida
		swap_pool[frame_asid].sw_pte->pte_entryLO &= (~VALIDON); 
		// Aggiornamento del TLB, per garantire la coerenza dei dati 
		// TODO: aggiornare il TLB riscrivendo la entry usando TLBP e TLBWI
		TLBCLR(); 

		// Riabilitazione degli interrupt
		setSTATUS(getSTATUS() & IECON); 
		
		// Aggiornamento della memoria "secondaria" i.e. flash device associato al processo
		flash_device_operation(victim_frame,FLASHWRITE, curr_support); 	
	}

	// Rilascio della mutua esclusione sulla swap pool table
	SYSCALL(VERHOGEN, &swap_pool_semaphore, 0, 0); 

}

// Algoritmo di rimpiazzamento FIFO
int replacement_algorithm(){
	// Variabile che contiene l'indice della prossima pagina vittima
	static int next_frame = 0; 
	int victim_frame = next_frame; 
	next_frame = (next_frame + 1) % POOLSIZE; 
	return victim_frame; 
}

void flash_device_operation(int frame, int operation, support_t *curr_support){
	// Ottenimento del frame asid coinvolto nell'operazione dal/sul flash device
	int asid = operation == FLASHWRITE ? swap_pool[frame].sw_asid : curr_support->sup_asid;

	// Ricavo l'indirizzo del device register associato al flash device dell'asid passato come parametro
	memaddr dev_reg_addr = (memaddr) (DEVREGSTRT_ADDR + ((FLASHINT - 3) * 0x80) + (asid * 0x10));    /* Inidirizzo del device register del device che ha provocato l'eccezione */
    devreg_t *dev_reg = (devreg_t *) dev_reg_addr;                                                              /* Device register del device che ha generato l'interrupt */
	
	// Operazione di scrittura sul / lettura dal flash device, seguendo il formato descritto in 5.4 pops
	dev_reg->dtp.command = (swap_pool[frame].sw_pageNo - KUSEG) >> VPNSHIFT | operation; 

	// Scrittura sul / lettura dal flash device asid-esimo
	int flash_status = SYSCALL(DOIO, &(dev_reg->dtp.command), operation, 0); 
	
	// Se si è verificato un errore, scatta una trap
	if (flash_status != READY){
		// TODO: program trap handling
	}
}