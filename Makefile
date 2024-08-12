.DEFAULT_GOAL = build

CC = gcc
MAKE = make
                                                                                                
SRC_DIR = ./src
INC_DIR = ./h ./mem-pool/include
OBJ_DIR = ./obj
LIB = libmessage_queue.so
STATIC_LIB = libmessage_queue.a

$(OBJ_DIR):
	@mkdir -p $@

INC_FLAGS = $(addprefix -I,$(INC_DIR))
CCFLAGS = $(INC_FLAGS) 
CCFLAGS += -Wall -Wextra -Werror -Wmissing-prototypes -g -Wshadow -Wundef -Wcast-align -Wunreachable-code -O1 -std=c11

mem_pool = mem-pool/*.o

$(mem_pool):
	@cd mem-pool && $(MAKE)

SUBMOD_OBJS = $(mem_pool)

clean_submod:
	cd mem-pool && rm -f *.o *.so
	@echo sub-module cleaned.

_OBJS = message_queue.o

OBJS = $(patsubst %,$(OBJ_DIR)/%,$(_OBJS))

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(OBJ_DIR)
	$(CC) $(CCFLAGS) -fpic -c -o $@ $<

$(LIB) : $(mem_pool) $(OBJS)
	$(CC) -shared -fpic -o $@ $^  -lpthread

$(STATIC_LIB) : $(mem_pool) $(OBJS)
	ar rcs $@ $^

build: $(LIB) $(STATIC_LIB)
	@echo build complete

LDFLAGS = -l:libev.a -l:$(STATIC_LIB)
example : $(STATIC_LIB)
	$(CC) $(CCFLAGS) -c -o obj/example.o src/example.c
	LIBRARY_PATH=. $(CC) $(CCFLAGS)  obj/example.o -o $@ $(LDFLAGS)

clean: clean_submod
	rm -rf $(OBJ_DIR)
	rm -f $(LIB) $(STATIC_LIB)

