# Compiler settings
CC = clang
LD = clang
CC_FLAGS = -O2 -Wall -Wextra -c
LD_FLAGS = -Wall -Wextra
OBJ = obj

# Compile rules.
edio: $(OBJ) $(OBJ)/edio.o
	$(CC) $(LD_FLAGS) $(OBJ)/edio.o -o edio

$(OBJ):
	mkdir $(OBJ)

$(OBJ)/edio.o: edio.c
	$(CC) $(CC_FLAGS) edio.c -o $(OBJ)/edio.o

# Clean rules
.PHONY: clean
clean:
	rm -rf $(OBJ) edio