TARGET = arServer4
CC=gcc
CFLAGS = -g3
SRC = $(wildcard *.c)
INC = $(wildcard *.h)
LDFLAGS = -lm -ldl -lmysqlclient -lpthread `pkg-config --cflags --libs jack gstreamer-1.0 gstreamer-pbutils-1.0`

all: $(TARGET)

$(TARGET): $(SRC) $(INC)
	$(CC) -o $(TARGET) $(CFLAGS) $(SRC) $(INC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	mkdir -m 775 -p /opt/audiorack/bin
	mkdir -m 777 -p /var/opt/audiorack/logs
	cp -ut /opt/audiorack/bin/ arServer4
	cp -ut /opt/audiorack/bin/ runifnot
	chmod 755 /opt/audiorack/bin/*
	cp -pPRt /opt/audiorack/ ../support 
