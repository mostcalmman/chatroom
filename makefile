compile_all: src/*
	gcc -pthread -o build/client src/client.c src/common.c
	gcc -pthread -o build/server src/server.c src/common.c

server: src/*
	gcc -pthread -o build/server src/server.c src/common.c

client: src/*
	gcc -pthread -o build/client src/client.c src/common.c

all: compile_all