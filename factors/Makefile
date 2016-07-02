CC      = clang
CFLAGS  = -Wall -Wextra -Werror -g
LDLIBS  = -lm

PROG    = factors
OBJS    = factors.o

all: $(PROG)

${PROG}: ${OBJS}
	${CC} ${CFLAGS} -o ${PROG} ${OBJS} ${LDLIBS}

clean:
	/bin/rm -f *.o ${PROG} core

.PHONY: clean
