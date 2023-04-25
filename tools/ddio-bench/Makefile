CC = gcc
LD = gcc
CCFLAGS = -Wall -ansi -Wextra -O2 -g \
	-I enso/api/linux -m64 -march=native -mavx -lpci
LDFLAGS = -lpci -g -O2

SRCS = change-ddio.c
OBJS = $(subst .c,.o,$(SRCS))
EXEC = change-ddio

all: $(EXEC) .depend

.c.o:
	$(CC) $(CCFLAGS) -c $< -o $@

.depend: $(SRCS)
	$(RM) ./.depend
	$(CC) $(CCFLAGS) -MM $^ >> ./.depend

-include .depend

$(EXEC): $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS) -o $(EXEC)

clean:
	$(RM) $(EXEC) $(OBJS) .depend
