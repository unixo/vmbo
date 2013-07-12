/*! \file io_device.h
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 *  \defgroup IO Dispositivo I/O
 */


#ifndef __IO_DEVICE_H__
#define __IO_DEVICE_H__

#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include "vm_types.h"
#include "queue.h"
#include "proc.h"
#include "random.h"


/*! \struct io_entry
 *  \brief Struttura per la rappresentazione di una richiesta d'I/O
 */
struct io_entry {
    /*! il PID del processo che ha effettuato la richiesta */
    uint16_t pid;
    /*! identificativo del processo all'interno della "proc table" */
    uint16_t procnum;
    /*! puntatore al successivo elemento della FIFO */
	STAILQ_ENTRY(io_entry) entries;
};

/*! \var typedef struct io_entry io_entry_t
 *  \brief Definizione del tipo di dato io_entry_t
 */
typedef struct io_entry io_entry_t;


/*! \struct io_dev_data
 *  \brief Struttura per la configurazione del dispositivo di I/O.
 *  Il campo "req_count" viene incrementato quando una nuova richiesta
 *  di I/O viene inserita nella coda. 
 *  Il tempo entro il quale viene servita la richiesta e' limitato 
 *  inferiormente da Tmin e superiormente da Tmax.
 */
struct io_dev_data {
    /*! Tempo minimo di attesa per espletare una richiesta di I/O */
    uint16_t Tmin;
    /*! Tempo massimo di attesa per espletare una richiesta di I/O */
    uint16_t Tmax;
    /*! Numero di richieste in coda */
    uint16_t req_count;
} io_dev;

/*
 *  Prototipi di funzione
 */
pthread_t *io_device_init(int, int);
int io_device_read(uint16_t);
void tell_io_device_to_exit();

#endif				/* __IO_DEVICE_H__ */
