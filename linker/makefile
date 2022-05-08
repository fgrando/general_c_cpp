
Q ?= @

-include config.mk

ifeq ($(OS), Windows_NT)
	COMPILER ?= GCC
	LIB_EXTENSION := dll
	BIN_EXTENSION := exe
else
	COMPILER ?= GCC
	LIB_EXTENSION := so
	BIN_EXTENSION := elf
endif


ifeq ($(COMPILER), GCC)
	CFLAGS_DEFAULTS += -Wall
	CFLAGS_DEFAULTS += -Wpedantic
	CFLAGS_DEFAULTS += -Werror
#	CFLAGS_DEFAULTS += -g

	CC := gcc
	LD := ld
	OBJDUMP := objdump -S
	OBJCOPY := objcopy
	LDFLAGS := -T script.ld
else
	error

endif

TARGET_EXEC := demo.$(BIN_EXTENSION)

CFLAGS += $(CFLAGS_DEFAULTS)
LFLAGS +=

DIRS :=.
BUILD_DIR := build

INCLUDE_FLAGS := $(foreach dir, $(DIRS), -I$(dir))
INCLUDE_FILES := $(foreach dir, $(DIRS), $(wildcard $(dir)/*.h))
C_FILES       := $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c))
C_NAMES 	  := $(notdir $(C_FILES))
OBJS	      := $(addprefix $(BUILD_DIR)/,$(C_NAMES:%.c=%.o))

$(BUILD_DIR)/%.o: %.c
	@echo 'cc: $<'
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -c "$<" -o "$@"


$(TARGET_EXEC): $(OBJS)
	$(Q)$(CC) $(OBJS) -o $@ $(LDFLAGS)
#	$(Q)$(LD) $(OBJS) -o $@ --verbose # use this to get the default linker script

clean:
	rm -f $(BUILD_DIR)/*.*
	rm -f *.$(BIN_EXTENSION)

.PHONY: clean