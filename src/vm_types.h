/*! \file vm_types.h
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *  \version 1.1
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */

#ifndef __VM_TYPES_H__
#define __VM_TYPES_H__

#include <stdio.h>

/*! \def XMALLOC(type, num)
 *  \brief Macro per migliorare la leggibilita' dell'operazione di allocazione 
 *  di blocchi di memoria: e' necessario specificare il tipo di dato ed il
 *  numero di elementi da allocare (il prototipo e' simile alla calloc)
 */
#define XMALLOC(type, num)      ((type *) xmalloc ((num) * sizeof(type)))

/*! \def XFREE(stale)
 *  \brief Deallocazione sicura di un blocco di memoria. Viene dapprima
 *  effettuato un controllo sul puntatore passato come parametro, poi 
 *  deallocato ed infine azzerato (garbage collector).
 */
#define XFREE(stale)            do { \
if (stale) { free ((void *) stale); \
stale = 0; } \
} while (0)

/*! \typedef void *(*thread_fn_t)(void *)
 *  \brief Definizione del tipo thread_fn_t come prototipo di una funzione
 *  eseguita come thread.
 */
typedef void *(*thread_fn_t) (void *);

/*! \typedef unsigned short int uint16_t
 *  \brief Definizione del tipo numero intero non segnato a 16 bit */
typedef unsigned short int uint16_t;

/*! \typedef unsigned short int uint32_t
 *  \brief Definizione del tipo numero intero non segnato a 32 bit */
typedef unsigned int uint32_t;

/*! \struct page
 *  \brief Struttura per la rappresentazione di una pagina virtuale
 *  \details E' la funzione proc_init ad avere il compito di creare la 
 *  tabella delle pagine per ogni processo; ogni voce in tabella sara' del 
 *  tipo "struct page".
 */
struct page {
    /*! Identificativo della pagina */
    uint16_t id;
    /*! Se vale 1, la pagina e' presente in memoria */
    int present:1;
    /*! Se vale 1, la pagina e' stata referenziata di recente */
    int reference:1;
    /*! Se vale 1, la pagina e' stata modificata di recente */
    int dirty:1;
    /*! Se la pagina e' presente in memoria, questo e' l'ID del frame associato */
    uint16_t frame_id;
};

typedef struct page page_t;

void *xmalloc(size_t);

#endif    /* __VM_TYPES_H__ */
