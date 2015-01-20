.SUFFIXES:      .cpp .o .a .s

CC     := $(CROSS_COMPILE)gcc
CXX    := $(CROSS_COMPILE)g++
LD     := $(CROSS_COMPILE)g++
AR     := $(CROSS_COMPILE)ar rc
RANLIB := $(CROSS_COMPILE)ranlib

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

OPENZWAVE := ../openzwave-*

INCLUDES := -I $(OPENZWAVE)/cpp/src -I $(OPENZWAVE)/cpp/src/command_classes/ \
        -I $(OPENZWAVE)/cpp/src/value_classes/ -I $(OPENZWAVE)/cpp/src/platform/ \
        -I $(OPENZWAVE)/cpp/src/platform/unix -I $(OPENZWAVE)/cpp/tinyxml/

RELEASE_CFLAGS  := -Wall -Wno-unknown-pragmas  -Wno-format -O3 -DNDEBUG
CFLAGS  := -c $(RELEASE_CFLAGS) -DVERSION=\"$(GIT_VERSION)\"

LIBZWAVE := $(wildcard $(OPENZWAVE)/*.a)
LIBS := $(LIBZWAVE) -pthread -ludev -llua -lmosquitto -lwebsockets -ldl -lssl -lcrypto

%.o : %.cpp openDHANA-mqtt.h
	$(CXX) $(CFLAGS) $(INCLUDES) -o $@ $<

all: openDHANA-ozw openDHANA-scriptor openDHANA-ir

openDHANA-ir: openDHANA-ir.o openDHANA-mqtt.o
	g++ openDHANA-ir.o openDHANA-mqtt.o -o openDHANA-ir $(LDFLAGS) $(LIBS)

openDHANA-scriptor: openDHANA-scriptor.o openDHANA-mqtt.o
	g++ openDHANA-scriptor.o openDHANA-mqtt.o -o openDHANA-scriptor $(LDFLAGS) $(LIBS)

openDHANA-ozw: openDHANA-ozw.o openDHANA-mqtt.o $(LIBZWAVE)
	g++ openDHANA-ozw.o openDHANA-mqtt.o -o openDHANA-ozw $(LDFLAGS) $(LIBS)

clean:
	$(info Cleaning...)
	@rm -f *.o core	

install:
	$(info ---------------------------------------------------------------------------------)
	$(info After openDHANA is installed you need to check your configuration files)
	$(info and run each module in the foreground 'daemon=false' to verify that your)
	$(info configuration is correct. When that is done you should let your modules)
	$(info run as daemons 'daemon=true' and 'mqtt_log_file="..."'. Enable the init.d)
	$(info scripts with 'make startup-scripts')
	$(info )
	$(info If you had earlier configuration files, they are backuped with a .~number~ suffix)
	$(info ---------------------------------------------------------------------------------)

	install -D -o root openDHANA-scriptor /usr/local/bin/openDHANA-scriptor
	install -D -o root openDHANA-ir /usr/local/bin/openDHANA-ir
	install -D -o root openDHANA-ozw /usr/local/bin/openDHANA-ozw
	install -d -o root /var/log/openDHANA

	install -d -o root /etc/openDHANA/ir/
	install -d -o root /etc/openDHANA/ir/lua/
	install -d -o root /etc/openDHANA/ozw
	install -d -o root /etc/openDHANA/scriptor/
	install -d -o root /etc/openDHANA/scriptor/lua/
	
	install --backup=numbered -o root etc/openDHANA/ir/openDHANA-ir.* /etc/openDHANA/ir/
	install --backup=numbered -o root etc/openDHANA/ir/lua/*.lua /etc/openDHANA/ir/lua
	
	install --backup=numbered -o root etc/openDHANA/ozw/openDHANA-ozw.* /etc/openDHANA/ozw/
	
	install --backup=numbered -o root etc/openDHANA/scriptor/openDHANA-scriptor.* /etc/openDHANA/scriptor/
	install --backup=numbered -o root etc/openDHANA/scriptor/lua/*.lua /etc/openDHANA/scriptor/lua
	
	install --compare -o root etc/init.d/openDHANA-* /etc/init.d


startup-scripts:
	$(info Enabling init.d scripts)
	update-rc.d openDHANA-ir defaults
	update-rc.d openDHANA-ozw defaults
	update-rc.d openDHANA-scriptor defaults
