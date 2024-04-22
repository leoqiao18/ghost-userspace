# Makefile for assembling and linking an assembly program

# Compiler and linker configurations
AS=nasm
ASFLAGS=-f elf32
LD=ld
LDFLAGS=-m elf_i386

# Source and object files
TARGETS=mult \
	mem \
	no-op

# Default target
all: $(TARGETS)

$(TARGETS): %:%.o
	$(LD) $(LDFLAGS) -o $@ $^

# Rule to build object files
%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

# Clean up
clean:
	rm -f $(OBJS) $(TARGETS)

# Phony targets
.PHONY: all clean
