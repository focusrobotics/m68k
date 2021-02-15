#****************************************************************************
# Copyright (c) 2021 by Focus Robotics Inc.
#
# MIT License
#
# Creation_Date :  Sat Feb 13 2021
# Created by    :  Andrew Worcester
#
#*****************************************************************************
# Top level makefile to build the m68k model and the code which runs on it


MAINFILES        = sproj_model/sim.c
MUSASHIFILES     = Musashi/m68kcpu.c Musashi/m68kdasm.c
MUSASHIGENCFILES = Musashi/m68kops.c Musashi/m68kopac.c Musashi/m68kopdm.c Musashi/m68kopnz.c
MUSASHIGENHFILES = Musashi/m68kops.h
MUSASHIGENERATOR = Musashi/m68kmake

CFILES   = $(MAINFILES) $(MUSASHIFILES) $(MUSASHIGENCFILES)
OFILES   = $(CFILES:%.c=%.o)

CC        = gcc
WARNINGS  = -Wall -pedantic -g
CFLAGS    = $(WARNINGS) -IMusashi -Isproj_model
LFLAGS    = $(WARNINGS)

TARGET = sproj_sim

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(OFILES) $(TARGET) $(MUSASHIGENERATOR)


all: $(TARGET)

clean:
	rm -f $(DELETEFILES)

$(TARGET): $(MUSASHIGENHFILES) $(OFILES) Makefile
	$(CC) -o $@ $(OFILES) $(LFLAGS)

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)
	cd Musashi; ./m68kmake

$(MUSASHIGENERATOR):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)  $(MUSASHIGENERATOR).c

ttytest: ttytest.c
	$(CC) -o $@ ttytest.c
