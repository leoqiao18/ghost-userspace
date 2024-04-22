# Makefile for assembling and linking an assembly program

# Compiler and linker configurations
AS=nasm
ASFLAGS=-f elf32
LD=ld
LDFLAGS=-m elf_i386

# Source and object files
SRC=mult.asm \
	mem.asm \
	no-op.asm
OBJ=$(SRC:.asm=.o)
EXEC=infinite_nop

# Default target
all: $(EXEC)

$(EXEC): $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

# Rule to build object files
%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

# Clean up
clean:
	rm -f $(OBJ) $(EXEC)

# Phony targets
.PHONY: all clean
