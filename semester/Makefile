CC = gcc
CXX = g++
CFLAGS = -g -DTERMBOX -DSCI_LEXER -Wall -I../../include -I../../src -I../../../lexilla/include \
  -I../../../lexilla/lexlib -I ../ -I../termbox_next/src
CXXFLAGS = $(CFLAGS)

scintilla = ../../bin/scintilla.a ../../../lexilla/bin/liblexilla.a
termbox = ../termbox_next/bin/termbox.a

all: semester 
semester.o: semester.c ; $(CC) $(CFLAGS) -c $<
semester: semester.o $(scintilla) $(termbox); $(CXX) $^ -o $@
clean: ; rm -f semester *.o
