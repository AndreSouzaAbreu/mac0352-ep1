################################################################################

CC := gcc
CFLAGS := -Wall

DIR_BIN := bin
DIR_SRC := src

OBJ := $(wildcard ${DIR_SRC}/*.c ${DIR_SRC}/*.h)
BIN := ${DIR_BIN}/mosquitto

################################################################################

all: ${BIN}

clean:
	@rm ${BIN}

run: ${BIN}
	./${BIN} 1883

${BIN}: ${OBJ}
	${CC} ${CFLAGS} $^ -o $@

.PHONY: all clean run
