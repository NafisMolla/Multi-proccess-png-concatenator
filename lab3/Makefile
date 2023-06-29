# Makefile, ECE252  
#Nafis Molla

CC = gcc       # compiler
CFLAGS = -Wall -g -std=gnu99 -Ilib# compilation flags
LD = gcc       # linker
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz -lcurl -lpthread  # link with libz and lcurl

OBJ_DIR = obj
SRC_DIR = lib
LIB_UTIL = $(OBJ_DIR)/zutil.o $(OBJ_DIR)/crc.o $(OBJ_DIR)/lab_png.o
SRCS   = paster2.c crc.c zutil.c lab_png.c
OBJS_PASTER2 = $(OBJ_DIR)/paster2.o $(LIB_UTIL) 

TARGETS= paster2

all: ${TARGETS}

paster2: $(OBJS_PASTER2) 
	mkdir -p $(OBJ_DIR)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

$(OBJ_DIR)/paster2.o: paster2.c 
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c 
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm paster2 obj/crc.o obj/lab_png.o obj/paster2.o obj/zutil.o

