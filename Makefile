SHELL := /bin/bash
TARGET = RealTimeGUI
SUMMARY = Audio driver real-time buffer settings assistant
DESCRIPTION = Detects your audio driver and current frequency\, then helps you tune and apply its real-time buffer settings
AUTHOR = ablyss
LICENSE = MIT
URLS = https:\/\/github.com\/ablyssx74\/$(TARGET)
REQUIRES = haiku\n    curl\n
PACKAGER = $(AUTHOR) <$(TARGET)@epluribusunix.net>
VENDOR = epluribusunix.net Project
VERSION = 1.0.2
REVISION = 1
PACKAGE_DIR := build/package
CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall
INCLUDES =
ARCH = x86_64

GUI_SRCS = $(TARGET).cpp
GUI_OBJS = $(GUI_SRCS:.cpp=.o)

HAS_RDEF := $(shell [ -f $(TARGET).rdef ] && echo yes || echo no)
ifeq ($(HAS_RDEF), yes)
    GUI_RSRCS = $(TARGET).rsrc
else
    GUI_RSRCS =
endif


LIBS =  -lbe -lmedia


.PHONY: all clean

all: $(TARGET)


$(TARGET): $(GUI_OBJS) $(GUI_RSRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(GUI_OBJS) $(LIBS)
ifeq ($(HAS_RDEF), yes)
	xres -o $(TARGET) $(GUI_RSRCS)
endif
	mimeset -f $(TARGET)

%.rsrc: %.rdef
	rc -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f *.o *.rsrc *.hpkg $(TARGET)
	rm -rf build


release: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(TARGET)/$(TARGET)/g' -e 's/$$(REVISION)/$(REVISION)/g' -e 's/$$(LICENSE)/$(LICENSE)/g' -e 's/$$(REQUIRES)/$(REQUIRES)/g' -e 's/$$(URLS)/$(URLS)/g' -e 's/$$(AUTHOR)/$(AUTHOR)/g' -e 's/$$(SUMMARY)/$(SUMMARY)/g' -e 's/$$(DESCRIPTION)/$(DESCRIPTION)/g' -e 's/$$(PACKAGER)/$(PACKAGER)/g'  -e 's/$$(VENDOR)/$(VENDOR)/g'  -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(TARGET).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(TARGET) $(PACKAGE_DIR)/apps/$(TARGET)
	ln -s ../apps/$(TARGET) $(PACKAGE_DIR)/bin/$(TARGET)
	ln -s ../../../../apps/$(TARGET) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(TARGET)
	package create -C $(PACKAGE_DIR) $(TARGET)-$(VERSION)-$(REVISION)-$(ARCH).hpkg



