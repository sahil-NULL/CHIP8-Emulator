all:
	g++ -Isrc/include -Lsrc/lib -o main chip8.c -lmingw32 -lSDL2main -lSDL2
debug:
	g++ -Isrc/include -Lsrc/lib -o main chip8.c -lmingw32 -lSDL2main -lSDL2 -DDEBUG
