CC=gcc

CFLAGS=-Wall -O2

LDFLAGS=-pthread

SRC=multi_code.c 

OBJ=$(SRC:.c=.o)

nyuenc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) 

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) 

clean:
	rm -f $(OBJ) nyuenc
