/*! \file vmbo.c
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 *
 *  \mainpage Simulatore Memoria Virtuale
 *
 *  \section intro_sec Introduzione
 *  \b VMBO e' un simulatore elementare di memoria virtuale interamente scritto 
 *  in C.\n\n Il programma simula un sistema con indirizzamento a 20 bit, con
 *  memoria paginata, l'algoritmo "enhanced second chance" come politica di 
 *  rimpiazzo ed il "pure demand paging" come politica di allocazione.\n
 *  Attraverso queste pagine sara' possibile comprendere in fondo la struttura
 *  del simulatore, i moduli che lo compongono e l'interazione tra essi.\n
 *  Attraverso il link "Moduli", sara' possibile leggere la documentazione 
 *  delle principali funzioni dei singoli moduli.\n
 *
 *  \section requirements Requisiti d'installazione
 *  Per utilizzare \c vmbo e' necessario disporre:
 *  \li sistema operativo *nix
 *  \li compilatore C (per esempio "gcc")
 *  \li utility "make"
 *
 *  All'interno della directory contenente i sorgenti, eseguire il comando 
 *  \b make: al termine della compilazione, sara' disponibile il binario 
 *  \c vmbo.\n
 *
 *  \section info Informazioni didattiche
 *  Materia: <b>Sistemi Operativi</b>\n
 *  Prof.re: <b>Emanuele Lattanzi</b>\n
 *  Studente: Ferruccio Vitale <unixo@devzero.it>\n
 *  Anno accademico: 2008-2009 - Sessione estiva\n\n
 */

#include "vmbo.h"
#include "mmu.h"
#include "proc.h"
#include "io_device.h"

extern proc_t **proc_table;
extern int max_proc;
extern int anticipatory_paging;
extern int reference_count;
extern uint16_t *reference_string;

/*! \var int debug
 *  \brief Livello di debug
 *  \details Determina il livello di dettaglio dei messaggi d'errore e
 *  informativi prodotti dal simulatore. Il parametro "-d" aumenta tale livello
 *  e puo essere specificato piu volte.
 */
int debug;

/*! \struct option longopts
 *  \brief Parametri da riga di comando (versione lunga)
 *  \details Elenco dei possibili parametri da riga di comando
 */
static struct option longopts[] = {
    { "anticipatory", no_argument, NULL, 'a' },
    { "debug", no_argument, NULL, 'd' },
    { "help", no_argument, NULL, 'h' },
    { "locality",  required_argument, NULL, 'L' },
    { "probabilities", required_argument, NULL, 'l' },
    { "all-memory", no_argument, NULL, 'M' },
    { "memory-read", required_argument, NULL, 'm' },
    { "processes", required_argument, NULL, 'p' },
    { "probability", required_argument, NULL, 'P' },
    { "reference", required_argument, NULL, 'r' },
    { "ram-size", required_argument, NULL, 'R' },
    { "frame-size", required_argument, NULL, 's' },
    { "Tmin", required_argument, NULL, 't' },
    { "Tmax", required_argument, NULL, 'T' },
    { "write-enabled", no_argument, NULL, 'w' },
    { "version", no_argument, NULL, 'v' },
    { NULL, 0, NULL, 0 }
};  

/*! \fn void usage()
 *  \brief Stampa la sinossi del programma
 *  \details La funzione usage() viene invocata quando e stato specificato il
 *  parametro "-h" su riga di comando o qualora i parametri contengano errori
 *  di sintassi.\n
 *  L'output viene inviato su \c stderr.
 */
static void usage()
{
    fprintf(stderr, "Utilizzo: vmbo [OPZIONI]...\n\n"
            "Opzioni generali:\n"
            "  -h, --help                Stampa questo help\n"
            "  -v, --version             Stampa la versione del programma ed esce\n"
            "  -d, --debug               Attiva il debug\n\n"
            "Opzioni MMU:\n"
            "  -a, --anticipatory        Disabilita l'anticipatory paging\n"
            "  -m, --memory-read=NUM     Numero massimo di accessi alla memoria\n"
            "  -R, --ram-size=NUM        Quantita di RAM disponibile\n"
            "  -s, --frame-size=NUM      Dimensione della pagina/frame\n"
            "  -w, --write-enabled       Abilita gli accessi in scrittura alla memoria\n\n"
            "Opzioni PROCESSO:\n"
            "  -M, --all-memory          Forza i processi ad allocare il massimo della memoria\n"
            "  -p, --processes=NUM       Numero di processi contemporanei\n"
            "  -P, --probability=NUM     Probabilita di accessi alla memoria\n"
            "  -l, --probabilities=LIST  Specifica la probabilta per ogni processo\n"
            "  -L, --locality=NUM        Specifica la percentuale di localita temporale\n"
            "  -r, --reference=LIST      Specifica la reference string da usare\n\n"
            "Opzioni DISPOSITIVO I/O:\n"
            "  -t, --Tmin=NUM            Tempo minimo d'attesa del dispositivo I/O\n"
            "  -t, --Tmax=NUM            Tempo massimo d'attesa del dispositivo I/O\n\n");
    fprintf(stderr, "Esempi d'utilizzo:\n"
            " - Esegue 7 processi contemporanei ed effettua 10 letture\n"
            "   vmbo --max-read=10 --max-processes=7\n"
            " - Esegue 3 processi, specificando il ritardo per il dispositivo I/O\n"
            "   vmbo --max-processes=3 --Tmin=2 --Tmax=30\n"
            " - Specifica la probabilita' di effettuare un accesso alla memoria\n" 
            "   vmbo --probability=30\n"
            " - Specifica una probabilita' diversa per ogni processo\n"
            "   vmbo --probabilities=30:20:78:93:80\n"
            " - Specifica una reference string per determinare gli accessi\n"
            "   vmbo --reference=1:2:3:4:1:2:5:1:2:3:4:5\n\n");
}


int 
main(int argc, char **argv)
{
    pthread_t *tid_mmu, *tid_iodev;
    int i, time_seed, ch, error, _Tmin, _Tmax, _max_memory, _locality_prob,
    _prob, _max_read, _frame_size, _only_read, _ram_size, io_time_elapsed,
    option_index, allocated_pages, total_faults;
    char *prob_list, *_reference_string;
    
    /*
     *  Analisi dei parametri specificati da riga di comando: definisco prima
     *  i valori di default utili all'inizializzazione dei singoli thread e
     *  procedo all'analisi di "argv". Qualora siano stati specificati
     *  parametri inesistenti o siano presenti errori, la variabile "error"
     *  verra' impostata ad uno (1) ed il programma terminera' la propria 
     *  esecuzione.
     */
    option_index = 0;
    error = 0;
    _max_read = 50;
    _prob = 80;
    max_proc = 5;
    _frame_size = 4096;
    _ram_size = 1048576;
    _only_read = 1;
    _Tmin = 1, _Tmax = 100;
    _max_memory = 0;
    _locality_prob = 30;
    _reference_string = prob_list = NULL;
    anticipatory_paging = 1;
    mmu.offset_bits = log2(_frame_size);
    mmu.page_bits = ADDRESS_LENGTH-mmu.offset_bits;
    
    while ((ch = getopt_long(argc, argv, "hadl:L:Mm:r:R:s:t:T:p:P:wv", 
           longopts, &option_index)) != -1) {       
        switch (ch) {
            case 'a':
                anticipatory_paging = 0;
                break;
            case 'd':
                debug++;
                break;
            case 'L':
                if (optarg) {
                    _locality_prob = atoi(optarg);
                    if ((_locality_prob < 0) || (_locality_prob >100)) {
                        fprintf(stderr, "La percentuale di localita' deve "
                                "essere compresa tra 0 e 100.\n");
                        error = 2;
                    }
                } else
                    error = 1;
                break;
            case 'l':
                if (optarg)
                    prob_list = optarg;
                else
                    error = 1;
                break;              
            case 'M':
                _max_memory = 1;
                break;
            case 'm':
                if (optarg) {
                    _max_read = atoi(optarg);
                    if (_max_read <= 0) {
                        fprintf(stderr, "Il numero di accessi alla memoria "
                                "deve essere positivo.\n");
                        error = 2;
                    }
                } else
                    error = 1;
                break;
            case 'p':
                if (optarg) {
                    max_proc = atoi(optarg);
                    if (max_proc == 0) {
                        fprintf(stderr, "Il numero di processi deve essere "
                                "positivo.\n");
                        error = 2;
                    } 
                } else
                    error = 1;
                break;
            case 'P':
                if (optarg) {
                    _prob = atof(optarg)*100;
                    if (_prob <= 0 || _prob > 100) {
                        fprintf(stderr, "La probabilita' deve essere compresa"
                                " tra 0.1 ed 1.\n");
                        error = 2;
                    }
                } else
                    error = 1;
                break;
            case 'r':
                if (optarg)
                    _reference_string = optarg;
                else
                    error = 1;
                break;
            case 'R':
                if (optarg) {
                    _ram_size = atoi(optarg);
                    if (_ram_size > exp2(ADDRESS_LENGTH)) {
                        fprintf(stderr, "La dimensione della RAM non puo' "
                                " essere superiore a %d byte.\n", 
                                (int) exp2(ADDRESS_LENGTH));
                        error = 2;
                    }
                } else
                    error = 1;
                break;
            case 's':
                if (optarg) {
                    _frame_size = atoi(optarg);
                } else
                    error = 1;
                break;
            case 't':
                if (optarg)
                    _Tmin = atoi(optarg);
                else
                    error = 1;
                break;
            case 'T':
                if (optarg) 
                    _Tmax = atoi(optarg);
                else
                    error = 1;
                break;
            case 'w':
                _only_read = 0;
                break;
            case 'v':
                fprintf(stderr, "vmbo versione %d.%d.%d\n",
                        VER_MAJOR, VER_MINOR, VER_REVISION);
                error = 2;
                break;
            case 'h':
                error = 1;
                break;
            case 0:
                break;
        }
    }
    
    if (error) {
        if (error == 1)
            usage();
        return EXIT_FAILURE;
    } 
    
    /* 
     *  Inizializzazione dimensione indirizzo di memoria e maschera per 
     *  ottenere l'offset da un indirizzo virtuale generaato da un processo.
     */
    mmu.offset_bits = log2(_frame_size);
    mmu.page_bits = ADDRESS_LENGTH-mmu.offset_bits;
    for (mmu.offset_mask=0, i=0; i<mmu.offset_bits; i++)
        mmu.offset_mask += (uint32_t) exp2(i);
    fprintf(stdout, "--> Simulatore inizializzato con indirizzi a %d bit\n", 
            mmu.offset_bits+mmu.page_bits);
    
    /*
     *  Inizializzazione generatore numeri pseudo-casuali.
     */
    time_seed = (int) time(0);
    srandom(time_seed);
    
    /*
     *  Inizializzazione strutture dati e lancio dei thread: dapprima verra' 
     *  instanziato il thread che emula la MMU, successivamente il thread per
     *  simulare il dispositivo di I/O ed in ultimo i thread "processo.
     */
    tid_mmu = mmu_init(_max_read, _ram_size, _frame_size);
    
    /* 
     *  Gestione della reference string: se viene specificata da riga di
     *  comando ne effettuo il parse ed imposto dei nuovi valori di default.
     */
    reference_count = (uint16_t) -1;
    if (_reference_string) {
        char *ap, *prs;
        int j;
        
        for (reference_count=j=0; j<strlen(_reference_string); j++)
            if (_reference_string[j] == ':')
                reference_count++;
        mmu.total_access = ++reference_count;
        anticipatory_paging = 0;   /* Disabilita la paginazione anticipata */
        _prob = 100;               /* Consenti solo accessi alla memoria   */
        max_proc = 1;              /* Crea un solo processo                */
        _only_read = 1;            /* Consenti solo accessi in lettura     */
        _max_memory = 1;           /* Alloca il massimo della memoria      */
        reference_string = XMALLOC(uint16_t, reference_count);
        for (i=0, prs = _reference_string; (ap = strsep(&prs, ":")) != NULL; ) {
            if (*ap != '\0')
                reference_string[i++] = atoi(ap);
        }
    }

    tid_iodev = io_device_init(_Tmin, _Tmax);
    proc_init(max_proc, _prob, _only_read, _max_memory, prob_list, _locality_prob);
    
    /*
     *  Attesa che tutti i thread abbiano terminato la propria esecuzione e
     *  successiva deallocazione della memoria utilizzata: nell'ordine attendo
     *  l'MMU, il dispositivo di I/O e tutti i processi.
     */
    pthread_join(*tid_mmu, NULL);
    tell_io_device_to_exit();
    pthread_join(*tid_iodev, NULL);
    for (i = 0; i < max_proc; i++) {
        pthread_cond_signal(&proc_table[i]->io_cond);
        pthread_join(proc_table[i]->tid, NULL);
        fclose(LOG_FILE(i));
    }
    
    /*
     *  Stampa delle statistiche.
     */
    fprintf(stdout, "\n+==================================================================+\n"
           "|                       S T A T I S T I C H E                      |\n"
           "+==================================================================+\n"
           "| PID | NUM  | PROB | ACCESSI |  PAGE   | FAULT | ACCESSI |  TEMPO |\n"
           "|     | PAG  |      | MEMORIA |  FAULT  |  (%%)  |   I/O   |  MEDIO |\n"
           "+-----+------+------+---------+---------+-------+---------+--------+\n");
    for (total_faults = allocated_pages = io_time_elapsed = i = 0; i < max_proc; i++) {
        fprintf(stdout, "|% 4d |% 5d |% 4.0f%% | % 7d | % 7d | % 4.0f%% | % 7d | % 6.0f |\n",
                proc_table[i]->pid, proc_table[i]->page_count,
                proc_table[i]->percentile,
                proc_table[i]->stats.mem_accesses,
                proc_table[i]->stats.page_faults,
                proc_table[i]->stats.page_faults?
                      ((float)proc_table[i]->stats.page_faults/
                       (float)proc_table[i]->stats.mem_accesses)*100:0,
                proc_table[i]->stats.io_requests,
                proc_table[i]->stats.io_requests?
                ((float)proc_table[i]->stats.time_elapsed/
                 (float)proc_table[i]->stats.io_requests):0);
                io_time_elapsed += proc_table[i]->stats.time_elapsed;
        allocated_pages += proc_table[i]->page_count;
        total_faults += proc_table[i]->stats.page_faults;
    }
    fprintf(stdout, "+-----+------+------+---------+---------"
           "+-------+---------+--------+\n"
           "                    | % 7d | % 7d | % 4.0f%% | % 7d | % 6.0f |\n"
           "                    +---------+---------+-------"
           "+---------+--------+\n\n", mmu.total_access, total_faults, 
           ((float)mmu.page_faults/(float)mmu.total_access)*100,
           io_dev.req_count, io_dev.req_count?
            ((float)io_time_elapsed/io_dev.req_count):0);
    
    fprintf(stdout, "Pagine virtuali allocate  = % 12d\n"
            "Memoria virtuale allocata = %12lu (~ %.1f Mb)\n\n",
            allocated_pages, (unsigned long) allocated_pages*mmu.page_size,
            (float) allocated_pages*mmu.page_size/1048576);
    
    /*
     *  Dealloco la struttura dati che rappresenta la proc table ed i relativi
     *  thread ID.
     */
    for (i = 0; i < max_proc; i++) {
        XFREE(proc_table[i]->page_table);
        XFREE(proc_table[i]);
    }
    XFREE(proc_table);
    XFREE(tid_iodev);
    XFREE(tid_mmu);
    XFREE(reference_string);
        
    return EXIT_SUCCESS;
}


/*! \fn void *xmalloc(size_t num)
 *  \brief Wrapper della funzione "malloc"
 *  \details La funzione effettua un controllo di validita del puntatore 
 *  restituito: in caso di mancata allocazione, il programma terminera la 
 *  propria esecuzione.
 *  \param num          Dimensione del blocco di memoria da allocare
 *  \return             Puntatore al blocco di memoria allocato
 */
void *
xmalloc(size_t num)
{
    void *p = (void *) malloc(num);
    if (!p) {
        printf("Memory exhausted");
        exit(EXIT_FAILURE);
    }
    return p;
}
