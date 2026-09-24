CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
SRC    = src/main.c src/lexer.c src/parser.c src/ast.c src/codegen.c
OUT    = wescc

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -f $(OUT) $(OUT).exe out.s

run: $(OUT)
	./$(OUT) tests/test1.wec -o out.s

.PHONY: all clean run