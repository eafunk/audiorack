# node.js uiserver "build"

all: uiserver

uiserver:
	echo "making arui-server"
	npm install

install:
	mkdir -m 775 -p /opt/audiorack/bin/arui
	mkdir -m 775 -p /opt/audiorack/bin/arui/client
	cp -ut /opt/audiorack/bin/arui *.js
	cp -ut /opt/audiorack/bin/arui *.json
	cp -rt /opt/audiorack/bin/arui node_modules
	cp -rt /opt/audiorack/bin/arui/client client/*

clean:
