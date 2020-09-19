all: imgclone

INCL=-I/usr/include
LINK=-L/usr/lib -L/usr/local/lib -I/usr/lib/arm-linux-gnueabihf -pthread
CC=gcc -g $(INCL)

imgclone: imgclone.c
	$(CC) $(LINK) $^ -o $@

clean:
	rm imgclone
	
install:
	chmod 777 imgclone
	cp imgclone /usr/local/bin
