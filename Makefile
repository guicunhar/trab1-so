CC = gcc
CFLAGS = -Wall -g

all: kernel app InterControllerSim

kernel: kernel.c
	$(CC) $(CFLAGS) -o kernel kernel.c

app: app.c
	$(CC) $(CFLAGS) -o app app.c

InterControllerSim: InterControllerSim.c
	$(CC) $(CFLAGS) -o InterControllerSim InterControllerSim.c

clean:
	rm -f kernel app InterControllerSim
