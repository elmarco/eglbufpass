CFLAGS=-g -Wall
LIBS=-lEGL -lGL -lgbm -lGLEW

all: server_egl


server_egl.o: server_egl.c

server_egl: server_egl.o
	$(CC) $(CFLAGS) -o server_egl server_egl.o $(LIBS)

clean:
	rm -f server_egl server_egl.o
