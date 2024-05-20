HEADER = ./header
SOURCE = ./sources

server: $(SOURCE)/*
	gcc $(SOURCE)/* -I$(HEADER) -levent -g -o server

.PHONY: clean
clean:
	rm server