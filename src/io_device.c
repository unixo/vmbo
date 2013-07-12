/*! \file io_device.c
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */


#include "io_device.h"

/*! \struct io_requests
 *  \brief Lista FIFO di richieste di I/O
 *  \details I thread di tipo processo possono
 *  accodare la propria richiesta usando la funzione io_device_read()
 *  \var struct io_requests io_request_head
 *  \brief Puntatore al primo elemento della FIFO.
 */
static STAILQ_HEAD(io_requests, io_entry) io_request_head;
/*! \var uint16_t ioreq_count
 *  \brief Numero di richieste di I/O in coda.
 */
static uint16_t ioreq_count;
/*! \var pthread_mutex_t fifo_lock
 *  \brief Mutex posto a protezione della FIFO
 */
static pthread_mutex_t fifo_lock = PTHREAD_MUTEX_INITIALIZER;
/*! \var pthread_cond_t wait_cond
 *  \brief Condizione d'attesa nel quale il thread thread_io_device si blocca
 *  in attesa che la FIFO contenga un elemento.
 */
static pthread_cond_t wait_cond = PTHREAD_COND_INITIALIZER;
/*! \var pthread_mutex_t wait_lock
 *  \brief Mutex posto a protezione della condizione wait_cond
 */
static pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
/*! \var pthread_mutex_t request_lock
 *  \brief Mutex posto a protezione della FIFO: per inserire/rimuovere elementi
 *  dalla coda si deve prima effettuare il lock di questo mutex.
 */
static pthread_mutex_t request_lock = PTHREAD_MUTEX_INITIALIZER;
/*! \var int io_device_should_exit
 *  \brief Durata del thread I/O
 *  \details Questa variabile determina, quando posto ad uno, l'uscita del
 *  thread che emula il dispositivo di I/O; tale condizione viene impostata
 *  dalla funzione memory_access quando viene raggiunto il limite massimo
 *  d'accessi alla memoria.
 */
static int io_device_should_exit;

extern proc_t **proc_table;
extern int max_proc;


/*! \fn void *thread_io_device(void *parg)
 *  \brief Thread dispositivo I/O
 *  \details La funzione costituisce il corpo del thread che rappresenta il 
 *  dispositivo di I/O. Il suddetto thread sospende la propria esecuzione 
 *  fintanto che non siano presenti delle richieste d'accesso al dispositivo.
 *  Il thread termina la propria esecuzione quando sono stati compiuti il
 *  numero massimo d'accessi alla memoria (ad opera dell'MMU).
 *  \param parg         inutilizzato
 *  \return             inutilizzato
 */
static void *
thread_io_device(void *parg)
{
    io_entry_t *req;
    struct timespec timeout;
    int num;
    
    printf("--> Thread DEVICE I/O avviato [Tmin=%d, Tmax=%d]\n",
           io_dev.Tmin, io_dev.Tmax);
    
    while (!io_device_should_exit) {
        pthread_mutex_lock(&wait_lock);
        while ((ioreq_count == 0) && !io_device_should_exit) {
            pthread_cond_wait(&wait_cond, &wait_lock);
        }
        
        if (io_device_should_exit && (ioreq_count == 0)) {
            pthread_mutex_unlock(&wait_lock);
            break;
        }
        
        /*
         *  Estraggo la prima richiesta e la rimuovo dalla coda, aggiornando
         *  la variabile "ioreq_count" che tiene traccia di quante richieste
         *  sono ancora in attesa.
         */
        req = STAILQ_FIRST(&io_request_head);
        assert(req);
        
        pthread_mutex_lock(&fifo_lock);
        STAILQ_REMOVE(&io_request_head, req, io_entry, entries);
        ioreq_count--;
        pthread_mutex_unlock(&fifo_lock);
        
        /*
         *  Genero un numero casuale compreso nell'intervallo chiuso 
         *  [Tmin,Tmax] utile a simulare il reperimento dell'informazione dal
         *  dispositivo.
         */
        num = bounded_rand(io_dev.Tmin, io_dev.Tmax);
        timeout.tv_sec = 0;
        timeout.tv_nsec = num * 1000000;
        nanosleep(&timeout, NULL);
        fprintf(LOG_FILE(req->procnum), 
                "Richiesta d'accesso servita in %d ms\n", num);
        
        /*
         *  Aggiorno le statistiche e "risveglio" il processo che ha fatto la
         *  richiesta.
         */
        io_dev.req_count++;
        proc_table[req->procnum]->stats.io_requests++;
        proc_table[req->procnum]->stats.time_elapsed += num;
        pthread_mutex_unlock(&wait_lock);
        pthread_cond_signal(&proc_table[req->procnum]->io_cond);
        XFREE(req);
    }
    printf("<-- Thread DEVICE I/O terminato\n");
    pthread_exit(NULL);
}


/*! \addtogroup IO
 * @{
 *  \fn pthread_t *io_device_init(int min, int max)
 *  \brief Inizializzazione I/O
 *  \details La funzione inizializza la struttura dati utile a rappresentare il
 *  dispositivo di I/O, nonche la lista delle richieste.\n
 *  In ultimo, si occupera' di creare il thread.
 *  \param min          Tempo minimo d'attesa
 *  \param max          Tempo massimo d'attesa
 *  \return             Restituisce il thread_id appena creato
 *  \sa thread_io_device
 */
pthread_t *io_device_init(int min, int max)
{
    pthread_t *tid = XMALLOC(pthread_t, 1);
    int ret;
    
    if (io_dev.Tmax > io_dev.Tmin)
        io_dev.Tmax = io_dev.Tmin;
    else {
        io_dev.Tmin = min;
        io_dev.Tmax = max;
    }
    io_device_should_exit = 0;
    ioreq_count = 0;
    io_dev.req_count = 0;
    STAILQ_INIT(&io_request_head);
    ret = pthread_create(tid, NULL, &thread_io_device, NULL);
    
    return (ret == 0) ? tid : NULL;
}


/*! \fn int io_device_read(uint16_t procnum)
 *  \brief Richiede un accesso al dispositivo di I/O
 *  \details La funzione si occupa di accodare la richiesta d'accesso al 
 *  dispositivo di I/O da parte di uno dei processi. Qualora l'MMU abbia 
 *  gia terminato la propria esecuzione, la funzione non accetta 
 *  ulteriori richieste.
 *  \param procnum      ID del processo
 *  \return             Restituisce l'esito dell'operazione:
 *                      1  la richiesta e' stata accodata
 *                      0  quando non sono piu' ammesse richieste
 */
int io_device_read(uint16_t procnum)
{
    io_entry_t *req;
    int ret;
    
    pthread_mutex_lock(&request_lock);
    
    if (!io_device_should_exit) {
        pthread_mutex_lock(&wait_lock);
        req = XMALLOC(io_entry_t, 1);
        req->pid = proc_table[procnum]->pid;
        req->procnum = procnum;
        pthread_mutex_lock(&fifo_lock);
        if (STAILQ_EMPTY(&io_request_head))
            STAILQ_INSERT_HEAD(&io_request_head, req, entries);
        else
            STAILQ_INSERT_TAIL(&io_request_head, req, entries);
        ioreq_count++;
        pthread_mutex_unlock(&fifo_lock);
        
        fprintf(LOG_FILE(procnum), 
                "\nRichiesta d'accesso a dispositivo I/O accodata\n");
        
        pthread_mutex_unlock(&wait_lock);
        pthread_cond_signal(&wait_cond);
        ret = 1;
    } else
        ret = 0;
    
    pthread_mutex_unlock(&request_lock);
    
    return ret;
}


/*! \fn void tell_io_device_to_exit()
 *  \brief Determina l'uscita del thread di I/O
 *  \details La funzione comunica al thread la richiesta di terminare: viene
 *  richiamata da memory_access quando viene raggiunto il numero massimo
 *  di accessi alla memoria da parte dei processi.
 */
void tell_io_device_to_exit()
{
    pthread_mutex_lock(&request_lock);
    io_device_should_exit = 1;
    pthread_mutex_unlock(&request_lock);
    pthread_cond_signal(&wait_cond);
}

/*! @} */
