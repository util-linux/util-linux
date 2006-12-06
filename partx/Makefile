OBJ = bsd.o dos.o partx.o solaris.o unixware.o

all: addpart delpart partx

partx:	$(OBJ)

addpart: addpart.o

delpart: delpart.o

clean:
	rm -f $(OBJ) addpart.o delpart.o addpart delpart partx *~

$(OBJ): partx.h
