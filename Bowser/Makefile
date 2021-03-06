################################################
#
# Bowser
# 
# This makefile is free software; you can redistribute it and/or
# modify it under the terms of the enclosed JBQ code license.
# See data/LICENSE.Makefile
# 
# This makefile is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# License for more details.
# 
#
################################################

# the file name, ...

BINARY := Bowser
VTOP := 1.1.1
VDATE := $(shell date +%Y%m%d)
VERSION := $(VTOP)-$(VDATE)
LIBS := -lbe -lbnetapi -lnetwork -ltextencoding -ltranslation -ltracker

################################################

# output directories

OBJDIR := obj
DEPDIR := dep
SRCDIR := src
DATADIR := data

# compiler, linker, ...

CC := g++ -c -pipe
CFLAGS := 
CFLAGS += -O3 
CFLAGS += -fno-pic 
CFLAGS += -ffast-math 
CFLAGS += -Wall -W -Wno-multichar -Wp,-Wall 
CFLAGS += -fPIC
#CFLAGS += -Werror

LD := g++
LDFLAGS := $(LIBS)

DEP := g++ -MM

ZIP := zip -r -9 -y

################################################

# the engine

MAKEFILE := Makefile

FULLNAME := $(subst \ ,_,$(BINARY))-$(VERSION)

DATA := $(shell find $(DATADIR) -type f)

EXTRADATA:= $(wildcard data.zip)

BASESOURCES := $(shell cd $(SRCDIR) && ls -1 *.cpp)
SOURCES := $(addprefix $(SRCDIR)/,$(BASESOURCES))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(basename $(BASESOURCES))))
DEPENDS := $(addprefix $(DEPDIR)/,$(addsuffix .d,$(basename $(BASESOURCES))))

BASEHEADERS := $(shell cd $(SRCDIR) && ls -1 *.h)
HEADERS := $(addprefix $(SRCDIR)/,$(BASEHEADERS))

.PHONY : default release clean binarchive sourcearchive all

.DELETE_ON_ERROR : $(BINARY)

default : $(BINARY)

clean :
	@echo cleaning
	@rm -rf $(BINARY) $(OBJDIR) $(DEPDIR) $(filter-out data.zip,$(wildcard *.zip)) *.zip~

all : sourcearchive binarchive

sourcearchive : $(FULLNAME)-src.zip

binarchive : $(FULLNAME).zip

$(BINARY) : $(OBJECTS)
	@echo linking $@
	@$(LD) $(LDFLAGS) $(OBJECTS) -o $@
	@rc $(SRCDIR)/Bowser.rdef
	@xres -o $@ $(SRCDIR)/Bowser.rsrc
	@mimeset -f $@
	
$(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	@echo compiling $@
	@mkdir -p $(OBJDIR)
	@$(CC) $< $(CFLAGS) $(CFLAGS.default) -o $@

$(DEPDIR)/%.d : $(SRCDIR)/%.cpp
	@echo generating dependencies for $<
	@mkdir -p $(DEPDIR)
	@$(DEP) $< > $@

$(FULLNAME).zip : $(BINARY) $(DATA) $(EXTRADATA) $(MAKEFILE)
	@rm -rf $@~
	@mkdir -p $@~/$(FULLNAME)/
	@copyattr -d -r $(BINARY) $(DATADIR)/* $@~/$(FULLNAME)/
ifneq "$(EXTRADATA)" ""
	@unzip $(EXTRADATA) -d $@~/$(FULLNAME)/
endif
	@find $@~ -type f | xargs mimeset
	@rm -rf $@~/$(FULLNAME)/CVS $@~/$(FULLNAME)/protocol/CVS $@~/$(FULLNAME)/help/CVS $@~/$(FULLNAME)/scripts/CVS
	@rm -rf $@~/$(FULLNAME)/CVS $@~/$(FULLNAME)/help/content/CVS $@~/$(FULLNAME)/help/images/CVS
	@cd $@~ && $(ZIP) $@ $(FULLNAME)
	@mv -f $@~/$@ .
	@rm -rf $@~

$(FULLNAME)-src.zip : $(SOURCES) $(HEADERS) $(DATA) $(EXTRADATA) $(MAKEFILE)
	@rm -rf $@~
	@mkdir -p $@~/$(FULLNAME)-src/
	@copyattr -d -r $(SRCDIR) $(DATADIR) $(MAKEFILE) $@~/$(FULLNAME)-src/
ifneq "$(EXTRADATA)" ""
	@copyattr -d -r $(EXTRADATA) $@~/$(FULLNAME)-src/
endif
	@find $@~ -type f | xargs mimeset
	@rm -rf $@~/$(FULLNAME)-src/src/CVS $@~/$(FULLNAME)-src/data/CVS
	@rm -rf $@~/$(FULLNAME)-src/CVS $@~/$(FULLNAME)-src/data/protocol/CVS $@~/$(FULLNAME)-src/data/help/CVS $@~/$(FULLNAME)-src/data/scripts/CVS
	@rm -rf $@~/$(FULLNAME)-src/CVS $@~/$(FULLNAME)-src/data/help/content/CVS $@~/$(FULLNAME)-src/data/help/images/CVS
	@cd $@~ && $(ZIP) $@ $(FULLNAME)-src
	@mv -f $@~/$@ .
	@rm -rf $@~

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),depend)
ifneq ($(MAKECMDGOALS),sourcearchive)
include $(DEPENDS)
endif
endif
endif
