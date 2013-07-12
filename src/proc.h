/*! \file proc.h
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 *  \defgroup PROC Processo utente
 */

#ifndef __PROC_H__
#define __PROC_H__

#include <pthread.h>
#include "vm_types.h"

/*! \def LOG_FILE(n)
 *  \brief File di log del processo
 *  \details Restituisce il puntatore a FILE, equivalente al file di log delle
 *  attivita del processo stesso; il file viene aperto dalla funzione proc_init
 *  e chiuso dal processo stesso durante la chiusura.
 */
#define LOG_FILE(n)                (proc_table[n]->log_file)

/*! \struct proc
 *  \brief Struttura per la rappresentazione in memoria di un processo
 *  \details E' la funzione proc_init ad avere il compito di creare i thread che 
 *  simuleranno un processo utente: per ognuno di questi verra creata 
 *  un'equivalente voce nella tabella dei processi.
 */
struct proc {
    /*! Process ID: identificativo del processo all'interno della proc table */
    uint16_t pid;
    /*! Thread ID associato al processo */
    pthread_t tid;
    /*! Numero di pagine allocate dal processo */
    unsigned page_count;
    /*! Tabella delle pagine del processo */
    page_t *page_table;
    /*! Percentuale di effettuare un accesso alla memoria piuttosto che una
     lettura dal dispositivo di I/O */
    float percentile;
    /*! Condizione d'attesa a seguito della richiesta di I/O */
    pthread_cond_t io_cond;
    /*! Mutex per la condizione d'attesa */
    pthread_mutex_t io_lock;
    /*! File di log associato al processo */
    FILE *log_file;
    /*! Statistiche delle operazioni effettuate dal processo */
    struct  proc_stats {
        /*! Numero di accessi alla memoria */
        uint16_t mem_accesses;
        /*! Numero di page fault generati a seguito di un accesso */
        uint16_t page_faults;
        /*! Numero di richieste al dispositivo di I/O */
        uint16_t io_requests;
        /*! Totale dei tempi d'attesa per espletare le richieste di I/O */
        uint16_t time_elapsed;
    } stats;
    /*! Ultimo indirizzo di memoria generato (localita) */
    uint32_t last_address;
};

/*! \typedef struct proc proc_t
 *  \brief Definizione del tipo di dato proc_t da struct proc
 */
typedef struct proc proc_t;

/*
 *  Prototipi di funzioni pubbliche
 */
void proc_init(int, int, int, int, char *, int);
void process_info(int);

#endif              /* __PROC_H__ */
