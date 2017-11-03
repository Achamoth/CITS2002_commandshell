CC= cc -std=c99
CFLAGS= -Wall -Werror -pedantic
LDFLAGS= -lm

HEADER=  mysh.h
OBJECTS= execute.o globals.o mysh.o parser.o
TARGET=  mysh

all: $(OBJECTS) $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(TARGET)

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o