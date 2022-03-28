# node.js uiserver "build"

all: uiserver

uiserver:
	echo "making arui-server"
	npm install --unsafe-perm=true --allow-root

install:
	mkdir -m 775 -p /opt/audiorack/bin/arui
	mkdir -m 775 -p /opt/audiorack/bin/arui/client
	cp -ut /opt/audiorack/bin keepalive
	cp -ut /opt/audiorack/bin/arui *.js
	cp -ut /opt/audiorack/bin/arui *.json
	cp -rt /opt/audiorack/bin/arui node_modules
	cp -rt /opt/audiorack/bin/arui/client client/*
	cp -rt /opt/audiorack/support ../support/arui_defconf.json
	
clean:
