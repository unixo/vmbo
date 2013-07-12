/*! \file random.c
 *  \author Ferruccio Vitale (unixo@devzero.it)
 *  \date 21/04/2009
 *  \version 1.0
 *
 *  \note
 *  Universita' degli studi di Urbino "Carlo Bo"\n
 *  Sistemi Operativi\n
 *  Professore Emanuele Lattanzi\n
 *  Anno Accademico 2008 - 2009
 */

#include "random.h"
#include <stdlib.h>
#include <limits.h>

/*! \fn int bounded_rand(int min, int max)
 *  \brief La funzione restituisce un numero intero casuale nell'intervallo
 *         compreso tra i due parametri.
 *         L'uso di questa funzione prevede che il generatore di numeri pseudo
 *         casuali sia stato gia' inizializzato.
 *  \param min          Estremo sinistro dell'intervallo chiuso
 *  \param max          Estremo destro dell'intervallo chiuso
 *  \return             Restituisce un numero casuale
 */
int bounded_rand(int min, int max)
{
    int range = max - min < 0 ? max - min - 1 : max - min + 1;
    int value = (int) (range * ((float) random() / (float) LONG_MAX));
	
    return value == range ? min : min + value;
}
