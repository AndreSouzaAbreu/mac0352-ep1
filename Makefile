################################################################################

CC := gcc
CFLAGS := -Wall

DIR_BIN := bin
DIR_SRC := src

OBJ := $(shell find ${DIR_SRC} -type f)
BIN := ${DIR_BIN}/mosquitto

################################################################################

all: ${BIN}

clean:
	@rm ${BIN}

run: ${BIN}
	./${BIN} 1883

${BIN}: ${OBJ}
	mkdir ${DIR_BIN}
	${CC} ${CFLAGS} $^ -o $@

tags: ${OBJ}
	ctags $^

.PHONY: all clean run
