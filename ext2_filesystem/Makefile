# Define the compiler
CC = gcc

# Define compiler flags
CFLAGS = -Wall -Wextra -std=c11

# Define the target executable
TARGET = recext2fs

# Define the source files
SRCS = recext2fs.c

# Define the object files (source files with .o extension)
OBJS = $(SRCS:.c=.o)

# The default target
all: $(TARGET)

# The target to build the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# The target to build the object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# The target to clean up the build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets (targets that don't correspond to files)
.PHONY: all clean
