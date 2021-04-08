# node.js uiserver "build"

all: server client

client: client/webclient-project/src
	echo "installing arui-client"
	cd client/webclient-project && npm install && npm run build

server:
	echo "installing arui-server"
	npm install

install:
	mkdir -m 775 -p /opt/audiorack/bin/arui
	mkdir -m 775 -p /opt/audiorack/bin/arui/client
	cp -ut /opt/audiorack/bin/arui *.js
	cp -ut /opt/audiorack/bin/arui *.json
	cp -rt /opt/audiorack/bin/arui node_modules
	cp -rt /opt/audiorack/bin/arui/client client/webclient-project/build/*

clean:
