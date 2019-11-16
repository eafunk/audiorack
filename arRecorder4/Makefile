TARGET = arRecorder4
CC=gcc
CFLAGS = -g3
SRC = $(wildcard *.c)
INC = $(wildcard *.h)
LDFLAGS = -lm -lpthread `pkg-config --cflags --libs jack gstreamer-1.0 gstreamer-audio-1.0 gstreamer-app-1.0`

all: $(TARGET)

$(TARGET): $(SRC) $(INC)
	$(CC) -o $(TARGET) $(CFLAGS) $(SRC) $(INC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	mkdir -m 775 -p /opt/audiorack/bin
	cp -ut /opt/audiorack/bin/ $(TARGET)
	chmod 755 /opt/audiorack/bin/*
