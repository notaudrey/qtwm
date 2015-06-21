CC = clang
TARGET = qtwm
CFLAGS = -pipe -Wall  -lxcb -lxcb-xinerama

all: $(TARGET)

$(TARGET): clean
	$(CC) $(CFLAGS) -o $(TARGET) src/*.c

clean:
	rm -f *.o *.a *.out *.la *.lo *.so $(TARGET)

