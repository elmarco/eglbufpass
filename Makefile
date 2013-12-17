CFLAGS=-g -Wall `pkg-config --cflags libdrm`
LIBS=-lEGL -lGL -lgbm -lGLEW -ldrm -lX11

OBJ = server_egl.o main.o client.o fdpass.o

all: egl_pass

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

egl_pass: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f egl_pass $(OBJ)
