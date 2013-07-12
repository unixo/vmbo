CC = gcc
CFLAGS =
LIBS = -lm
INCLUDES = 
SRCS = random.c io_device.c mmu.c proc.c vmbo.c
OBJS = random.o io_device.o mmu.c proc.o vmbo.o

all: vmbo

vmbo: $(OBJS)
	${CC} ${CFLAGS} ${INCLUDES} ${LIBS} ${OBJS} -o vmbo -pthread

.c.o:
	${CC} ${CFLAGS} ${INCLUDES} -c $< 2>/dev/null

clean:
	rm -f *.o core *~ vmbo PROC_*

package:
	tar cvfz vmbo.tgz ${SRCS} *.h Makefile

indent:
	ls -1 *.[ch] | xargs indent --no-tabs --original
	make clean

