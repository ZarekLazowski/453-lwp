CC = gcc

CFLAGS = -Wall -g -fpic

.PHONY: lwp

#Make the libraries
lwp: liblwp.a liblwp.so

liblwp.a: lwp.o
	ar r liblwp.a lwp.o magic64.o

liblwp.so: lwp.o magic64.o
	$(CC) $(CFLAGS) -shared -o $@ lwp.o magic64.o

lwp.o: lwp.c lwp.h
	$(CC) $(CFLAGS) -c lwp.c lwp.h

magic64.o: magic64.S
	$(CC) -c -Wall -g magic64.S


#Clean up the ~ files
clean:
	rm *~


#Making tests
tests: nums

nums:
	gcc -o nums numbersmain.c -L. -llwp -lsnakes
