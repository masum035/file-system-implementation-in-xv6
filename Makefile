CC = gcc  #compiler
TARGET = p3test #target file name

all:
	$(CC) p3test.c -o $(TARGET)

clean:
	rm $(TARGET)	