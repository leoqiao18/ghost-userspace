# Makefile for assembling and linking an assembly program

# Compiler and linker configurations
AS=nasm
ASFLAGS=-f elf64
LD=ld
LDFLAGS=
CXX_COMPILER=g++
CXX_FLAGS=-Wall

# Source and object files
ASM_TARGETS=mult \
	mem \
	no-op \
	simd

CXX_TARGETS=thread_io \
	    large_mem

TARGETS=$(ASM_TARGETS) $(CXX_TARGETS)

# Default target
all: $(TARGETS)

$(ASM_TARGETS): %:%.o
	$(LD) $(LDFLAGS) -o $@ $^
	
# Rule to build object files
%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

# C++ executable
$(CXX_TARGETS): %:%.cc
	$(CXX_COMPILER) $(CXX_FLAGS) -o $@ $^ -lpthread


# Clean up
clean:
	rm -f $(TARGETS) $(TARGETS:%=%.o)

# Phony targets
.PHONY: all clean
