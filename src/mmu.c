/*! \file mmu.c
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */


#include "mmu.h"

#define EMPTY               0
#define DATA_AVAILABLE      1
#define RESULT_AVAILABLE    2

/*! \var int anticipatory_paging
 *  \brief Indica se la paginazione anticipata risulta attiva.
 *  \details Qualora il numero di frame fisici disponibili siano superiori a
 *  128 viene abilitata la paginazione anticipata.
 */
int anticipatory_paging;

/*! \struct mmu_shared_data
 *  \brief Struttura per contenere la richiesta attuale alla MMU
 *  \details Struttura condivisa tra il thread MMU e la funzione memory_access:
 *  in questa struttura vengono inseriti i dati di una richiesta d'accesso
 *  da parte di un processo.
 *  \var struct mmu_shared_data current
 *  \brief Istanza della struttura mmu_shared_data
 */
struct mmu_shared_data {
    /*! identificativo del processo all'interno della proc table */
    int procnum;
    /*! indirizzo virtuale generato dal processo */
    uint32_t virtual_address;
    /*! indirizzo fisico tradotto dalla MMU */
    uint32_t translated_address;
    /*! tipo di operazione: vale zero se e' lettura, uno se scrittura */
    int rw;
    /*! variabile di stato per la sincronizzazione di MMU e memory_access */
    int status;
    /*! lock per modificare la struttura */
    pthread_mutex_t lock;
    /*! condizione d'attesa per la sincronizzazione di MMU e memory_access */
    pthread_cond_t condition;
} current = 
       { -1, 0, 0, 0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };


/*! \var int mmu_should_exit
 *  \brief Variabile di controllo per l'MMU
 *  \details Questa variabile determina la durata del thread MMU: fintanto che
 *  il valore e pari a zero, il thread continua la propria esecuzione, 
 *  viceversa termina. Questa variabile viene posta ad uno dalla funzione
 *  memory_access.
 */
static int mmu_should_exit;

/*! \var pthread_mutex_t mem_read_lock
 *  \brief Mutex per la mutua esclusione della chiamata a memory_access 
 */
static pthread_mutex_t mem_read_lock = PTHREAD_MUTEX_INITIALIZER;

/*! \var STAILQ_HEAD free_frames_head
 *  \brief Lista dei frame inutilizzati (puntatore al primo elemento).
 */
static STAILQ_HEAD(free_frames, frame) free_frames_head;

/*! \var STAILQ_HEAD used_frames_head
 *  \brief Lista dei frame utilizzati (puntatore al primo elemento).
 */
static STAILQ_HEAD(used_frames, frame) used_frames_head;

/*! \var STAILQ_HEAD used_frames_head
 *  \brief Lista delle pagine residenti in memoria (puntatore al primo 
 *  elemento).
 */
static TAILQ_HEAD(active_pages, active_page) active_page_head;

extern proc_t **proc_table;
extern int max_proc;
extern int debug;


/*! \addtogroup MMU
 * @{
 *  \fn int second_chance(int procnum, uint16_t page, int update_stats, frame_t **frame)
 *  \brief Algoritmo di rimpiazzo della pagina
 *  \details La funzione second_chance viene invocata direttamente dal thread
 *  "mmu": scopo di questa funzione e trovare una pagina da rimuovere dalla
 *  memoria ed associare il frame appena liberato con la pagina che ha generato
 *  il fault.
 *  \param procnum       Identificativo del processo chiamante
 *  \param page          Pagina virtuale che ha generato il fault
 *  \param update_stats  Se vale uno (1) vengono aggiornate le statistiche
 *  \param frame         Se diverso da NULL, viene registrato il frame identificato
 *  \return              restituisce 1 se e stato un page hit, 0 per un fault
 *  \sa thread_mmu
 */
static int
second_chance(int procnum, uint16_t page, int update_stats, frame_t **frame)
{
    active_page_t *ap;
    proc_t *current_proc;
    unsigned int frame_id;
    int result;
    frame_t *f;
    
    current_proc = proc_table[procnum];
    f = NULL;
    
    /* 
     *  Verifico se la pagina richiesta e' presente, ovvero se risulta gia'
     *  associata ad un frame fisico.
     */
    if (IS_PAGE_PRESENT(current_proc->page_table[page])) {
        /*
         *  La pagina e' presente: incremento la statistica degli HIT,
         *  ottengo l'ID del frame associato e lo cerco all'interno della
         *  lista dei frame utilizzati: una volta trovato, imposto ad uno
         *  il bit Reference.
         */
        frame_id = FRAME_ID(current_proc->page_table[page]);
        if (update_stats)
            mmu.page_hits++;
        result = 1;
        
        STAILQ_FOREACH(f, &used_frames_head, entries) {
            if (frame_id == f->id) {
                PAGE_SET_REFERENCED(current_proc->page_table[page]);
                break;
            }
        }
#ifdef VM_DEBUG
        assert(frame_id == f->id);
#endif /* VM_DEBUG */
    } else {
        /*
         *  La pagina richiesta non e' presente in memoria. Aggiorno il
         *  contatore dei FAULT e verifico se:
         *    1. sono disponibili dei frame non ancora utilizzati
         *    2. viceversa, devo individuare quale frame liberare.
         */
        result = 0;
        if (update_stats) {
            mmu.page_faults++;
            current_proc->stats.page_faults++;
        } 
        
        if (STAILQ_EMPTY(&free_frames_head)) {
            /*
             *  La lista dei frame liberi e' vuota: applico l'algoritmo
             *  "enhanced second chance"
             */
            int proc_found, page_found;
            
            page_found = proc_found = -1;
            
            /*
             *  Inizio a scorrere la lista delle pagine associate a frame,
             *  alla ricerca di una pagina con i bit R e D posti a zero; se
             *  trovo una pagina con il bit D uguale ad uno, ne effettuo
             *  una copia su disco (write back) e pongo il bit a zero; se
             *  trovo una pagina con il bit R uguale ad uno, lo pongo uguale
             *  a zero e continuo la ricerca.
             */
            while (page_found == -1) {
                TAILQ_FOREACH(ap, &active_page_head, entries) {
                    if (IS_PAGE_DIRTY(proc_table[ap->procnum]->page_table[ap->page_id])) {
                        fprintf(proc_table[ap->procnum]->log_file,
                                "Write-back della pagina %d\n", ap->page_id);
                        PAGE_CLEAR_DIRTY(proc_table[ap->procnum]->page_table[ap->page_id]);
                        PAGE_CLEAR_REFERENCED(proc_table[ap->procnum]->page_table[ap->page_id]);
                        continue;
                    }
                    if (!IS_PAGE_REFERENCED(proc_table[ap->procnum]->page_table[ap->page_id])
                        && !IS_PAGE_DIRTY(proc_table[ap->procnum]->page_table[ap->page_id])) {
                        proc_found = ap->procnum;
                        page_found = ap->page_id;
                        break;
                    } else {
                        if (IS_PAGE_REFERENCED
                            (proc_table[ap->procnum]->page_table
                             [ap->page_id])) {
                            PAGE_CLEAR_REFERENCED(proc_table[ap->procnum]->page_table[ap->page_id]);
                            continue;
                        }
                    }
                }
            }
            
#ifdef VM_DEBUG
            assert(IS_PAGE_REFERENCED(proc_table[proc_found]->page_table[page_found]) == 0);
            assert(IS_PAGE_DIRTY(proc_table[proc_found]->page_table[page_found]) == 0);
#endif /* VM_DEBUG */
            
            /*
             *  E' stata identificato il processo che possiede la pagina da
             *  rimuovere dalla memoria: ne estraggo il frame ID.
             */
            frame_id = FRAME_ID(proc_table[proc_found]->page_table[page_found]);
            
            /*
             *  Elimino l'associazione tra la pagina identificata ed il 
             *  frame associato: questo implica porre uguale a zero anche i
             *  bit R e D, nonche' rimuoverlo dalla lista active_pages.
             */
            fprintf(current_proc->log_file,
                    "<-- La pagina %d del processo %d e stata rimossa "
                    "dalla memoria %s(frame %d)\n", page_found, proc_found,
                    (IS_PAGE_DIRTY(proc_table[proc_found]->page_table[page_found]) ?
                     "e paginata su disco " : ""),
                    FRAME_ID(proc_table[proc_found]->page_table[page_found]));
            
            PAGE_CLEAR_PRESENT(proc_table[proc_found]->page_table[page_found]);
            PAGE_CLEAR_REFERENCED(proc_table[proc_found]->page_table[page_found]);
            PAGE_CLEAR_DIRTY(proc_table[proc_found]->page_table[page_found]);
            PAGE_CLEAR_FRAMEID(proc_table[proc_found]->page_table[page_found]);
            
            TAILQ_REMOVE(&active_page_head, ap, entries);
            XFREE(ap);
            
            /* 
             *  Tramite il frame ID ottenuto in precedenza, cerco il frame 
             *  nella lista used_frames per associarvi la nuova pagina;
             *  analogamente inserisco una nuova voce nella lista 
             *  active_pages.
             */
            STAILQ_FOREACH(f, &used_frames_head, entries) {
                if (frame_id == f->id) {
                    PAGE_SET_REFERENCED(current_proc->page_table[page]);
                    PAGE_SET_PRESENT(current_proc->page_table[page]);
                    PAGE_SET_FRAMEID(current_proc->page_table[page], frame_id);
                    ASSIGN_FRAME_TO_PROC(f, current_proc, page);
                    STAILQ_REMOVE(&used_frames_head, f, frame, entries);
                    STAILQ_INSERT_TAIL(&used_frames_head, f, entries);
                    
                    ap = XMALLOC(active_page_t, 1);
                    ap->procnum = current.procnum;
                    ap->page_id = page;
                    TAILQ_INSERT_TAIL(&active_page_head, ap, entries);
                    
                    fprintf(current_proc->log_file,
                            "--> La pagina virtuale %d e' stata "
                            "associata al frame %u\n", page, f->id);
                    break;
                }
            }
        } else {
            /*
             *  La pagina richiesta non e' presente (page fault) ma la lista
             *  dei frame liberi non e' vuota: prendo il primo frame
             *  disponibile e lo associo alla pagina.
             */
            f = STAILQ_FIRST(&free_frames_head);
            STAILQ_REMOVE_HEAD(&free_frames_head, entries);
            assert(f->valid == 0);
            f->valid = 1;
            ASSIGN_FRAME_TO_PROC(f, current_proc, page);
            STAILQ_INSERT_TAIL(&used_frames_head, f, entries);
            
            fprintf(current_proc->log_file,
                    "--> La pagina virtuale %d e' stata associata al frame %u\n", 
                    page, f->id);
            
            PAGE_SET_PRESENT(current_proc->page_table[page]);
            PAGE_SET_REFERENCED(current_proc->page_table[page]);
            PAGE_SET_FRAMEID(current_proc->page_table[page], f->id);
            
            ap = XMALLOC(active_page_t, 1);
            ap->procnum = current.procnum;
            ap->page_id = page;
            TAILQ_INSERT_TAIL(&active_page_head, ap, entries);
        }
    }
    
    if (frame)
        *frame = f;
    return result;
}


/*! \fn void *thread_mmu(void *pArg)
 *  \brief Thread MMU
 *  \details La funzione, eseguita come thread, emula il modulo MMU per la
 *  traduzione degli indirizzi virtuali in fisici e l'implementazione
 *  dell'algoritmo enhanced sencond chance
 *  \param pArg        inutilizzato
 *  \return            valore di uscita del thread (inutilizzato)
 */
static void *
thread_mmu(void *pArg)
{
    active_page_t *ap, *ap_temp;
    proc_t *current_proc;
    frame_t *f;
    uint16_t page;
    uint16_t offset;
    uint16_t ws[3];
    int result;
    
    printf("--> Thread MMU avviato\n    [RAM=%d, PAGESIZE=%d, "
           "PHYS-FRAMES=%d, TOTAL_READ=%d, PROC=%d]\n",
           mmu.ram_size, mmu.page_size, mmu.max_page_count,
           mmu.total_access, max_proc);
    
    /*
     *  Fintanto che non venga raggiunto il numero totale di accessi, il
     *  thread resta in attesa di processere nuove richieste.
     */
    while (!mmu_should_exit) {
        pthread_mutex_lock(&current.lock);
        while (current.status != DATA_AVAILABLE)
            pthread_cond_wait(&current.condition, &current.lock);
        
        if (mmu_should_exit) {
            pthread_mutex_unlock(&current.lock);
            break;
        }
        
        /*
         *  La variabile "current" contiene i dati della richiesta da esaminare,
         *  tra cui il PID del processo che ha effettuato la richiesta (utile
         *  per accedere alla sua tabella delle pagine) e l'indirizzo virtuale
         *  dal quale estraggo l'identificativo della pagina virtuale e l'offset.
         */
        current_proc = proc_table[current.procnum];
        ws[0] = page = current.virtual_address >> mmu.offset_bits;
#ifdef VM_DEBUG
        if (page > current_proc->page_count) {
            fprintf(stderr, "PROC = %d, PAGE_COUNT = %d, PAGE = %d, "
                    "ADDRESS = %d\n", current.procnum, 
                    current_proc->page_count, page, current.virtual_address);
            assert(0);
        }
#endif /* VM_DEBUG */
        offset = current.virtual_address & mmu.offset_mask;
    
        fprintf(current_proc->log_file,
                "\n%s indirizzo virtuale %u [pagina %d - offset %d]\n",
                current.rw ? "Scrittura" : "Lettura",
                current.virtual_address, page, offset);
        
        
        result = second_chance(current.procnum, page, 1, &f);
#ifdef VM_DEBUG
        assert(f);
#endif /* VM_DEBUG */
        
        if (anticipatory_paging) {
            ws[1] = (page > 0)?page-1:(uint16_t)-1;
            ws[2] = (page < (current_proc->page_count-1))?page+1:(uint16_t)-1;
            if (ws[1] != (uint16_t) -1)
                second_chance(current.procnum, ws[1], 0, NULL);
            if (ws[2] != (uint16_t) -1)
                second_chance(current.procnum, ws[2], 0, NULL); 
        }
        
        current_proc->stats.mem_accesses++;
        current.translated_address = f->physical_addr + offset;
        current.status = RESULT_AVAILABLE;
        fprintf(current_proc->log_file,
                "[PAGE %s] L'indirizzo virtuale %u corrisponde al fisico %u\n",
                result?"HIT":"FAULT", current.virtual_address,
                current.translated_address);
        if (current.rw)
            PAGE_SET_DIRTY(current_proc->page_table[page]);
        
        pthread_mutex_unlock(&current.lock);
        pthread_cond_signal(&current.condition);
    }
    /*
     *  Dealloco la lista delle pagine utilizzate.
     */
    TAILQ_FOREACH_SAFE(ap, &active_page_head, entries, ap_temp) {
        TAILQ_REMOVE(&active_page_head, ap, entries);
        XFREE(ap);
    }
    printf("<-- Thread MMU terminato\n");
    pthread_exit(NULL);
}


/*! \fn pthread_t *mmu_init(int max_read, int ram_size, int page_size)
 *  \brief Inizializzazione MMU
 *  \details La funzione inizializza il modulo MMU, valorizzando la variabile
 *  "mmu" con i parametri che descrivono l'ambiente. I valori total_access, 
 *  page_size e ram_size sono modificabili mediante l'uso degli opportuni
 *  parametri da riga di comando. Una volta suddivisa la memoria in frame,
 *  verra' creato il thread MMU.
 *  \param max_read      Numero massimo di accessi alla memoria
 *  \param ram_size      Dimensione complessiva della memoria principale
 *  \param page_size     Dimensione della singola pagina/frame
 *  \return              Puntatore al thread ID della MMU
 *  \sa thread_mmu
 */
pthread_t *mmu_init(int max_read, int ram_size, int page_size)
{
    pthread_t *tid = XMALLOC(pthread_t, 1);
    int i, ret;
    
    mmu_should_exit = 0;
    mmu.total_access = max_read;
    mmu.page_hits = mmu.page_faults = 0;
    mmu.page_size = page_size;
    mmu.ram_size = ram_size;
    mmu.max_page_count = (mmu.ram_size / mmu.page_size);
    
    /*
     *  Se il rapporto frame/processi risulta troppo basso, non e conveniente
     *  utilizzare la paginazione anticipata.
     */
    if (((float)mmu.max_page_count/max_proc) < 3)
        anticipatory_paging = 0;
    
    /*
     *  Inizializza la lista dei frame usati e liberi e delle pagine residenti
     *  in memoria (ovvero associate ad un frame.
     */
    STAILQ_INIT(&free_frames_head);
    STAILQ_INIT(&used_frames_head);
    TAILQ_INIT(&active_page_head);
    
    /*
     *  Suddivide la memoria in frame e li inserisce nella lista dei
     *  frame liberi. Crea infine il thread che emula l'MMU.
     */
    for (i = 0; i < mmu.max_page_count; i++) {
        frame_t *f = XMALLOC(frame_t, 1);
        f->id = i;
        f->physical_addr = i * mmu.page_size;
        f->valid = 0;
        if (STAILQ_EMPTY(&free_frames_head))
            STAILQ_INSERT_HEAD(&free_frames_head, f, entries);
        else
            STAILQ_INSERT_TAIL(&free_frames_head, f, entries);
    }
    ret = pthread_create(tid, NULL, &thread_mmu, NULL);
    
    return (ret == 0) ? tid : NULL;
}


/*! \fn uint32_t memory_access(int procnum, uint32_t address, int rw)
 *  \brief Funzione per la lettura/scrittura di una zona di memoria. 
 *  \details La funzione puo' essere invocata solo da un processo per volta: 
 *  questa inserisce i dati della richiesta in una struttura temporanea 
 *  condivisa con l'MMU, risveglia l'MMU perche' processi la richiesta 
 *  e resta in attesa del risultato.
 *  \param procnum       Identificativo processo nella page table
 *  \param address       Indirizzo virtuale
 *  \param rw            Se vale '0' effettua una lettura, '1' scrittura
 *  \return              Risultato dell'operazione. Restituisce -1 quando MMU
 *                       ha raggiunto il numero massimo di operazioni ed il
 *                       processo deve terminare la propria esecuzione.
 */
uint32_t memory_access(int procnum, uint32_t address, int rw)
{
    static int signaled = 0;
    uint32_t result = (uint32_t) -1;
    
    /*
     *  Blocco il mutex "mem_read_lock" per garantire la mutua esclusione
     *  tra due processi concorrenti che tentano l'accesso alla MMU.
     */
    pthread_mutex_lock(&mem_read_lock);

    if (NUM_OF_REQUESTS() < mmu.total_access) {     
        /*
         *  Inserisco i dettagli della richiesta in una struttura condivisa
         *  tra questa funzione ed il thread, protetta dal mutex current.lock.
         */
        pthread_mutex_lock(&current.lock);
        current.procnum = procnum;
        current.virtual_address = address;
        current.translated_address = 0;
        current.rw = rw;
        current.status = DATA_AVAILABLE;
        pthread_mutex_unlock(&current.lock);
        
        /*
         * Comunico al thread MMU che e' disponibile una richiesta
         */
        pthread_cond_signal(&current.condition);

        /*
         *  Attendo che la richiesta venga espletata dalla MMU: al termine
         *  il risultato verra' restituito al processo chiamante, il mutex
         *  rilasciato e l'MMU sara' pronta per una successiva richiesta.
         */
        pthread_mutex_lock(&current.lock);
        while (current.status != RESULT_AVAILABLE)
            pthread_cond_wait(&current.condition, &current.lock);
        result = current.translated_address;
        current.status = EMPTY;
        pthread_mutex_unlock(&current.lock);
        
        if (debug)
            process_info(procnum);
    } else {
        /*
         *  E' stato raggiunto il numero massimo di accessi alla memoria:
         *  comunico al thread MMU e del dispositivo I/O di uscire; 
         *  restituisco un codice d'errore (-1) al processo chiamante,
         *  perche' questo termini la propria esecuzione.
         */
        if (!signaled) {
            tell_io_device_to_exit();
            mmu_should_exit = 1;
            current.status = DATA_AVAILABLE;
            pthread_cond_signal(&current.condition);
            signaled = 1;
        }
    }
    
    pthread_mutex_unlock(&mem_read_lock);
    
    return result;
}

/*! @} */
