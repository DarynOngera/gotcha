CC      := gcc
CFLAGS  := -O2 -Wall -Wextra
LDFLAGS := $(shell pkg-config --libs dbus-1)
CPPFLAGS:= $(shell pkg-config --cflags dbus-1)

SRC     := logger.c
TARGET  := build/notify_logger
PREFIX  ?= $(HOME)/.local

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@mkdir -p $(PREFIX)/bin
	cp $(TARGET) $(PREFIX)/bin/notify_logger
	@echo "Installed to $(PREFIX)/bin/notify_logger"
