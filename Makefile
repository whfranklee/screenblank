EXEC = screenblank
OBJS = screenblank.o
SRC  = screenblank.c

CC = gcc
LD = ld
CFLAGS += -O2 -std=gnu99 -Wall 
LDFLAGS += 

all:$(EXEC)

$(EXEC):$(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -pthread

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@ -pthread

clean:
	@rm -vf $(EXEC) *.o *~
