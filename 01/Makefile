HEADER = ./header
SOURCE = ./sources

server: $(SOURCE)/*
	gcc $(SOURCE)/* -I$(HEADER) -g -o server

.PHONY: clean
clean:
	rm server