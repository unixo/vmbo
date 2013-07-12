/*! file vmbo.h
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */

#ifndef __VMBO_H__
#define __VMBO_H__

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <getopt.h>
#include <string.h>
#include <sys/errno.h>
#include "random.h"
#include "vm_types.h"

#define VER_MAJOR       1
#define VER_MINOR       2
#define VER_REVISION    0

/*! \def ADDRESS_LENGTH
 *  \brief Lunghezza in bit di un indirizzo
 *  \details Determina la lunghezza massima di un indirizzo fisico e virtuale,
 *  nonche lo spazio d'indirizzamento del sistema ospite.
 */
#define ADDRESS_LENGTH  20  

#endif   /* __VMBO_H__ */
