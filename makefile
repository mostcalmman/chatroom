compile_all: src/*
	gcc -lpthread -o build/client src/client.c src/common.c
	gcc -lpthread -o build/server src/server.c src/common.c

server: src/*
	gcc -lpthread -o build/server src/server.c src/common.c

client: src/*
	gcc -lpthread -o build/client src/client.c src/common.c

clean:
	rm -rf build/downloads
	rm -rf build/server_files

all: compile_all