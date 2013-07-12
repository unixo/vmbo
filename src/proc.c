/*! \file proc.c
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */


#include "proc.h"
#include "mmu.h"
#include "io_device.h"
#include <string.h>
#include <math.h>


/*! \def LOOP_ITERATIONS
 *  \brief Numero di cicli
 *  \details Numero di elementi del vettore che simulate_loop deve scorrere
 *  quando viene simulata la localita' spaziale.
 *  \sa SIZE_OF_ITEM
 */
#define LOOP_ITERATIONS            8

/*! \def SIZE_OF_ITEM
 *  \brief Dimensione dell'elemento del vettore
 *  \sa LOOP_ITERATIONS
 */
#define SIZE_OF_ITEM               10

/*! \var uint16_t *reference_string
 *  \brief Reference string 
 *  \details Vettore di pagine alle quali il processo dovra' accedere per
 *  misurare il numero di page fault generati.
 */
uint16_t *reference_string;

/*! \var int reference_count
 *  \brief Numero di elementi nella reference string
 *  \sa reference_string
 */
int reference_count;


/*! \def DSS(proc)
 *  \brief Dimensione spazio indirizzamento virtuale
 *  \details Restituisce la dimensione dello spazio d'indirizzamento virtuale del
 *  processo, dato dal prodotto del numero di pagine allocate per la dimensione
 *  della singola.
 */
#define DSS(p)                     (((proc_table[p]))->page_count*mmu.page_size)

/*! \def MEM_ACCESS_PROBABILITY(n)
 *  \brief Probabilita di effettuare un accesso in memoria
 *  \details Valore numerico espresso, in percentuale, che determina la
 *  probabilita per un processo di effettuare una lettura in memoria piuttosto
 *  che dal dispositivo di I/O.
 */
#define MEM_ACCESS_PROBABILITY(n)  (proc_table[n]->percentile)

/*! \def WAIT_FOR_IO_TO_COMPLETE(n)
 *  \brief Attesa per il completamente I/O
 *  \details Il processo deve restare in attesa che il dispositivo di I/O
 *  espleti la richiesta effettuata.
 */
#define WAIT_FOR_IO_TO_COMPLETE(n) do { \
    pthread_mutex_lock(&proc_table[n]->io_lock);\
    pthread_cond_wait(&proc_table[n]->io_cond, &proc_table[n]->io_lock);\
    pthread_mutex_unlock(&proc_table[n]->io_lock); \
    } while (0)

/*! \var int temporal_locality
 *  \brief Localita temporale
 *  \details Percentuale di probabilita temporale di accedere allo stesso dato
 */
int temporal_locality = 30;

/*! \var static int only_read_allowed
 *  \brief Tipo di accesso alla memoria (R o RW)
 *  \details Determina se i processi possono effettuare accessi alla memoria
 *  in lettura o anche in scrittura: se vale uno (1), un processo puo soltanto
 *  leggere la memoria.
 */
static int only_read_allowed;

/*! \var proc_t **proc_table
 *  \brief Vettore dei processi attivi
 *  \details Viene allocato dalla funzione proc_init e deallocato nel main al 
 *  termine dell'esecuzione; il numero massimo di elementi e dato dal valore di
 *  max_proc.
 */
proc_t **proc_table;

/*! \var int max_proc
 *  \brief Numero di processi concorrenti
 *  \details La variabile \c max_proc, per default, viene inizializzato al 
 *  valore 5, ma e tuttavia modificabile usando il paramentro \c -p.
 */
int max_proc;


/*! \addtogroup PROC
 * @{
 *  \fn int simulate_loop(int procnum)
 *  \brief Simulazione d'accesso ad un vettore di elementi
 *  \details La funzione e' responsabile della simulazione di un'iterazione su
 *  un vettore di LOOP_ITERATIONS elementi, ognuno dei quali e' pari a 
 *  SIZE_OF_ITEM byte.
 */
static int
simulate_loop(int procnum)
{
    int rw, i;
    uint32_t addr;
    
    addr = bounded_rand(0, DSS(procnum)-LOOP_ITERATIONS*SIZE_OF_ITEM);
    for (i=0; i<LOOP_ITERATIONS; i++) {
        rw = only_read_allowed ? 0 : (bounded_rand(0, 100) > 50 ? 1 : 0);
        if (memory_access(procnum, addr + (i*SIZE_OF_ITEM), rw) == (uint32_t) - 1)
            return 0;
    }
    return 1;
}


/*! \fn int random_access(int procnum)
 *  \brief Effettua un accesso alla memoria con indirizzo casuale
 *  \details La funzione si occupa di generare un indirizzo di memoria con un
 *  approccio pseudo casuale; in osservanza della localita temporale
 *  sussite la probabilta che una richiesta possa essere vicina alla 
 *  precedente, con un intervallo pari a 1024.
 *  \param procnum Identificativo del processo all'interno della proc_table
 *  \return Se vale zero (0) indica al processo di terminare 
 */
static int
random_access(int procnum)
{
    uint32_t addr;
    int rw; 
    
    if (proc_table[procnum]->last_address == (uint32_t) -1)
        /*
         *  E' la prima volta che il processo accede alla memoria: genero un
         *  indirizzo compresso tra 0 ed il massimo spazio d'indirizzamento del
         *  processo.
         */
        addr = bounded_rand(0, DSS(procnum));
    else {
        if (bounded_rand(0, 100) <= temporal_locality) {
            addr = proc_table[procnum]->last_address+1024;
            if (addr > DSS(procnum))
                addr = proc_table[procnum]->last_address;
        } else
            addr = bounded_rand(0, DSS(procnum));   
    }
    proc_table[procnum]->last_address = addr;
    
    rw = only_read_allowed ? 0 : (bounded_rand(0, 100) > 50 ? 1 : 0);
    return (memory_access(procnum, addr, rw) != (uint32_t) - 1);
}


/*! \fn void *thread_proc(int procnum)
 *  \brief Thread per la simulazione di un processo
 *  \details Il thread, istanziato dalla funzione proc_init, si occupa di
 *  simulare un processo utente: la sua esecuzione e legata a quella della MMU,
 *  che rimane attiva fintanto che non viene raggiunto il numero massimo di
 *  accessi alla memoria.\n Al thread sono concesse solo due operazioni:
 *  \li Accesso alla memoria, generando un indirizzo virtuale casuale, sempre
 *  compreso nel proprio spazio d'indirizzamento
 *  \li Accesso al dispositivo di I/O.\n
 *  \param procnum        Identificativo del processo all'interno di proc_table
 */
static void *
thread_proc(int procnum)
{
    int condition = 1, reference_item = 0;
    
    fprintf(LOG_FILE(procnum),  "INIZIO PROCESSO\n======================\n"
            "PID             = %d\nPAGINE VIRTUALI = %d\nPROBABILITA'    = %.0f%%\n"
            "======================\n", procnum, proc_table[procnum]->page_count,
            proc_table[procnum]->percentile);
    
    while (condition) {
        /*
         *  Verifico se sono nella modalta' in cui viene misurato il numero di
         *  page fault usando la reference string: in questo caso il processo
         *  effettua gli accessi richiesti, viceversa effettua gli accessi come
         *  da specifica (I/O e memoria).
         */
        if (reference_string) {
            uint32_t addr;
            
            if (reference_item >= reference_count)
                reference_item = 0;
            addr = reference_string[reference_item++];
            addr *= mmu.page_size;
            if (memory_access(procnum, addr, 0) == (uint32_t) -1)
                condition = 0;
        } else {        
            if (bounded_rand(0, 100) <= MEM_ACCESS_PROBABILITY(procnum)) {
                /*  Nel 20% dei casi, il processo effettua un loop (while/for), 
                 *  effettuando acessi ad indirizzi di memoria contigui. 
                 *  Nel restante 80% effettua un accesso casuale al proprio spazio 
                 *  d'indirizzamento, con la possibilita di localita temporale, 
                 *  ovvero di accedere ad un indirizzo usato di recente.
                 */
                if (bounded_rand(0, 100) <= 30)
                    condition = simulate_loop(procnum);
                else
                    condition = random_access(procnum);
            } else {
                /*
                 *  Inserisco una richiesta di accesso al dispositivo di I/O e
                 *  resto in attesa che il dispositivo di I/O mi risvegli.
                 */
                if (io_device_read(procnum))
                    WAIT_FOR_IO_TO_COMPLETE(procnum);
                else
                    condition = 0;
            }
        }
    }
    
    pthread_exit(NULL);
}


/*! \fn void proc_init(int num, int percentile, int only_read, int max_memory, char *pl, int lp)
 *  \brief Funzione per la creazione dei thread di tipo "PROCESSO"
 *  \details Ad ogni thread viene associato un PID che lo identifica 
 *  univocamente, nonche una page table: tale tabella, di default, viene 
 *  inizializzata con un numero casuale di pagine, compreso tra 1 e 16; il
 *  parametro \c -M, tuttavia, impone ad ogni processo di allocare il massimo
 *  delle pagine disponibili (16 per processo).\n
 *  Viene inoltre assegnata una probabilita di effettuare richieste di accesso 
 *  alla memoria piuttosto che al dispositivo di I/O; di base tale valore e
 *  pari ad 80% ma e tuttavia possibile modificare questo valore con il 
 *  parametro \c -P, nonche inizalizzare ogni singolo processo con un valore
 *  differente (usando \c -l).
 *  \param num          Numero massimo di thread da eseguire
 *  \param percentile   Probabilita' di eseguire una lettura in memoria
 *  \param only_read    Specifica se i processi possono effettuare accessi
 *                      esclusivamente in lettura o anche in scrittura
 *  \param max_memory   Se vale uno, il processo alloca il massimo della memoria
 *  \param pl           Stringa con le probabilita per l'inizializzazione dei
 *                      processi
 *  \param lp           Localita' temporale
 */
void 
proc_init(int num, int percentile, int only_read, int max_memory, char *pl, int lp)
{
    char proc_filename[FILENAME_MAX];
    int *probs = NULL;
    int i, j;
    
    temporal_locality = lp;
    only_read_allowed = only_read;
    max_proc = num;
    
    /*
     *  Analizzo, se presente, la lista delle probabilita' con cui inizializzare
     *  i singoli processi; ogni valore viene separato da due punti dal valore
     *  successivo.
     */
    if (pl) {
        char *ap, *ppl;
        
        probs = XMALLOC(int, max_proc);
        for (i=0; i<max_proc; i++)
            probs[i] = percentile;
        for (i=0, ppl = pl; (ap = strsep(&ppl, ":")) != NULL; ) {
            if (*ap != '\0') {
                probs[i++] = atof(ap)*100;
            }
        }
    }   
    
    /*
     *  Creo dapprima la tabella dei processi: per ognuno di questi, alloco la
     *  memoria necessaria ed inizializzo la struttura dati; il thread associato
     *  viene eseguito solo quando l'intera proc_table e' stata creata, per 
     *  evitare la MMU possa accedere alla page table di processi la cui 
     *  inizializzazione non e' terminata.
     */
    proc_table = XMALLOC(proc_t *, max_proc);
    for (i = 0; i < max_proc; i++) {
        snprintf(proc_filename, FILENAME_MAX, "PROC_%02d.log", i);
        proc_table[i] = XMALLOC(proc_t, 1);
        proc_table[i]->pid = i;
        if (reference_string)
            proc_table[i]->page_count = reference_count;
        else
            proc_table[i]->page_count = max_memory?exp2(mmu.page_bits):bounded_rand(1, exp2(mmu.page_bits));
        proc_table[i]->percentile = probs?((i<max_proc)?probs[i]:percentile):percentile;
        proc_table[i]->log_file = fopen(proc_filename, "w");
        proc_table[i]->stats.mem_accesses = proc_table[i]->stats.page_faults = 0;
        proc_table[i]->stats.io_requests = proc_table[i]->stats.time_elapsed = 0;
        proc_table[i]->last_address = (uint32_t) -1;
        pthread_cond_init(&proc_table[i]->io_cond, NULL);
        pthread_mutex_init(&proc_table[i]->io_lock, NULL);
        
        /*
         *  Alloco ed inizializzo la page table del processo: il numero di 
         *  pagine e' tracciato dalla variabile "page_count", generato
         *  casualmente in un intervallo [1,16], in modo tale che ogni 
         *  processo abbia almeno una pagina.
         */
        proc_table[i]->page_table = XMALLOC(page_t, proc_table[i]->page_count);
        for (j = 0; j < proc_table[i]->page_count; j++) {
            proc_table[i]->page_table[j].id = j;
            proc_table[i]->page_table[j].frame_id = (uint16_t) -1;
            proc_table[i]->page_table[j].present = 0;
            proc_table[i]->page_table[j].reference = 0;
            proc_table[i]->page_table[j].dirty = 0;         
        }
    }
    /*
     *  Eseguo "max_proc" thread di tipo processo utente.
     */
    for (i = 0; i < max_proc; i++)
        pthread_create(&proc_table[i]->tid, NULL, (thread_fn_t) & thread_proc, (void *) i);
    printf("--> Thread PROC avviati [NUM=%d, PROB=%d%%, OPER=%s, LOCALITY=%d%%]\n",
           max_proc, pl?0:percentile, only_read_allowed?"R":"RW", temporal_locality);
}


/*! \fn void process_info(int procnum)
 *  \brief Stampa lo stato delle pagine di un processo
 *  \details La funzione scrive, nel file di log del processo, lo stato delle 
 *  pagine: per ognuna di esse viene indicato se e' presente o meno in memoria, 
 *  se e' referenziata o se "sporca".
 *  \param procnum       Identificativo processo nella page table
 */
void process_info(int procnum)
{
    proc_t *proc = proc_table[procnum];
    int i;
    
    for (i = 0; i < proc->page_count; i++) {
        fprintf(proc->log_file, "         PAGE %2d : ", i);
        if (IS_PAGE_PRESENT(proc->page_table[i])) {
            unsigned int frame_id = FRAME_ID(proc->page_table[i]);
            fprintf(proc->log_file, "FRAME %2d ", frame_id);
            if (IS_PAGE_REFERENCED(proc->page_table[i]))
                fprintf(proc->log_file, "[REF");
            else
                fprintf(proc->log_file, "[NOT REF");
            if (IS_PAGE_DIRTY(proc->page_table[i]))
                fprintf(proc->log_file, ", DIRTY]\n");
            else
                fprintf(proc->log_file, "]\n");
        } else
            fprintf(proc->log_file, "\n");
    }
    fprintf(proc->log_file, "============================================\n");
}

/*! @} */

