/*! \file mmu.h
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */


#ifndef _MMU_H_
#define _MMU_H_

#include <pthread.h>
#include "vm_types.h"
#include "queue.h"
#include "proc.h"
#include "io_device.h"

/*! \defgroup MMU Memory Management Unit
 *  \def IS_PAGE_PRESENT(p)
 *  \brief Restituisce 1 se la pagina e' presente in memoria.
 *  \def IS_PAGE_REFERENCED(p)
 *  \brief Restituisce 1 se la pagina e' stata referenziata.
 *  \def IS_PAGE_DIRTY(p)
 *  \brief Restituisce 1 se la pagina e' "sporca".
 *  \def PAGE_CLEAR_DIRTY(p)
 *  \brief Imposta a zero il bit dirty della pagina.
 *  \def PAGE_CLEAR_REFERENCED(p)
 *  \brief Imposta a zero il bit reference della pagina.
 *  \def PAGE_CLEAR_PRESENT(p)
 *  \brief Imposta a zero il bit present della pagina.
 *  \def PAGE_CLEAR_FRAMEID(p)
 *  \brief Imposta a zero il frame-id della pagina.
 *  \def PAGE_SET_DIRTY(p)
 *  \brief Imposta a uno il bit dirty della pagina.
 *  \def PAGE_SET_REFERENCED(p)
 *  \brief Imposta ad uno il bit reference della pagina.
 *  \def PAGE_SET_PRESENT(p)
 *  \brief Imposta ad uno il bit present della pagina.
 *  \def PAGE_SET_NUM(p, num)
 *  \brief Imposta l'identificatore della pagina.
 *  \def PAGE_SET_FRAMEID(p, fid)
 *  \brief Imposta il frame-id per la pagina "p".
 *  \def PAGE_NUM(p)
 *  \brief Restituisce l'identificatore della pagina.
 *  \def FRAME_ID(p)
 *  \brief Restituisce il frame-id della pgina.
 *  \def ASSIGN_FRAME_TO_PROC(f,p,n)
 *  \brief Assegna il frame "f" alla pagina "n" del processo "p".
 *  \def NUM_OF_REQUESTS()
 *  \brief Restituisce il numero di richieste effettuate all'MMU.
 */
#define IS_PAGE_PRESENT(p)              ((p).present)
#define IS_PAGE_REFERENCED(p)           ((p).reference)
#define IS_PAGE_DIRTY(p)                ((p).dirty)
#define PAGE_CLEAR_DIRTY(p)             ((p).dirty = 0)
#define PAGE_CLEAR_REFERENCED(p)        ((p).reference = 0)
#define PAGE_CLEAR_PRESENT(p)           ((p).present = 0)
#define PAGE_CLEAR_FRAMEID(p)           ((p).frame_id = (uint16_t) -1)
#define PAGE_SET_DIRTY(p)               ((p).dirty = 1)
#define PAGE_SET_REFERENCED(p)          ((p).reference = 1)
#define PAGE_SET_PRESENT(p)             ((p).present = 1)
#define PAGE_SET_NUM(p, num)            ((p).id = num)
#define PAGE_SET_FRAMEID(p, fid)        ((p).frame_id = fid)
#define PAGE_NUM(p)                     ((p).id)
#define FRAME_ID(p)                     ((p).frame_id)
#define ASSIGN_FRAME_TO_PROC(f,p,n)     do {\
f->debug_info.pid = p->pid; \
f->debug_info.page_id = n; \
} while (0)
#define NUM_OF_REQUESTS()               (mmu.page_hits+mmu.page_faults)


/*! \struct mmu_data
 *  \brief Parametri di configurazione del modulo MMU
 *  \details Questa struttura viene inizializzata dalla funzione mmu_init; e
 *  possibile modificare alcuni valori con gli opportuni parametri specificati 
 *  su riga di comando.
 */
struct mmu_data {
    /*! Numero massimo di pagine per processo */
    uint16_t page_bits;
    /*! Numero massimo di bit per l'offset di un indirizzo virtuale */
    uint16_t offset_bits;
    /*! Numero intero per estrarre l'offset da un indirizzo virtuale (AND) */
    uint32_t offset_mask;   
    /*! Numero totale di accessi alla memoria prima che il programma termini */
    uint32_t total_access;
    /*! Numero totale di page fault avvenuti */
    uint32_t page_faults;
    /*! Numero totale di page hit avvenuti */
    uint32_t page_hits;
    /*! Dimensione della singola pagina/frame */
    uint32_t page_size;
    /*! Quantita' complessiva di memoria principale */
    uint32_t ram_size;
    /*! Numero massimo di pagine disponibili */
    uint16_t max_page_count;
} mmu;


/*! \struct frame
 *  \brief Struttura per la rappresentazione di un frame di memoria
 *  \details Struttura per la rappresentazione in memoria di un frame. La
 *  funzione mmu_init e' incaricata della suddivisione della memoria in frame
 *  tutti della medesima dimensione; ogni frame sara' identificato da un 
 *  valore univoco all'interno della lista dei frame ed un indirizzo di memoria
 *  fisico di partenza, cui andra' sommato l'offset estratto dall'indirizzo
 *  virtuale generato dal processo.
 */
struct frame {
    /*! Identificativo univovo del frame */
    uint16_t id;
    /*! Indirizzo fisico di memoria di partenza del frame, cui sommare l'offset */
    uint32_t physical_addr;
    /*! Bit di stato: vale uno (1) se il frame e' utilizzato */
    unsigned int valid:1;
    /*! Informazioni di debug aggiuntive, non necessarie al funzionamento */
    struct {
        /*! PID del processo che "possiede" il frame */
        uint16_t pid;
        /*! Identificativo della pagina associata al frame */
        uint16_t page_id;
    } debug_info;
    /*! Puntatore al successivo elemenento della lista */
    STAILQ_ENTRY(frame) entries;
};


/*! \struct active_page
 *  \brief Struttura per rappresentare una pagina associata ad un frame.
 */
struct active_page {
    /*! Identificativo del processo all'interno della proc table */
    uint16_t procnum;
    /*! Identificativo della pagina associata ad un frame */
    uint16_t page_id;
    /*! Puntatore al successivo elemento della lista */
    TAILQ_ENTRY(active_page) entries;
};

/*! \typedef struct active_page active_page_t
 *  \brief Definizione del tipo di dato active_page_t da struct active_page
 */
typedef struct active_page active_page_t;

/*! \typedef struct frame frame_t
 *  \brief Definizione del tipo di dato frame_t da struct frame
 */
typedef struct frame frame_t;

/*
 *  Prototipi di funzioni pubbliche
 */
pthread_t *mmu_init(int, int, int);
uint32_t memory_access(int, uint32_t, int);

#endif              /* _MMU_H_ */
