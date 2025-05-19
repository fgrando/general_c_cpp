Q ?= @

-include test/defs.mk

TARGET_EXEC := demo


CFLAGS_DEFAULTS += -Wall
CFLAGS_DEFAULTS += -Wpedantic
CFLAGS_DEFAULTS += -Werror
CFLAGS_DEFAULTS += -g
CFLAGS_DEFAULTS += -ggdb

CFLAGS += $(CFLAGS_DEFAULTS)
#CFLAGS += -fprofile-arcs -ftest-coverage -O0

LFLAGS +=

DIRS := .


BUILD_DIR := build

INCLUDE_FLAGS := $(foreach dir, $(DIRS), -I$(dir))
INCLUDE_FILES := $(foreach dir, $(DIRS), $(wildcard $(dir)/*.h))
C_FILES       := $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c))
C_NAMES       := $(notdir $(C_FILES))
OBJS          := $(addprefix $(BUILD_DIR)/,$(C_NAMES:%.c=%.o))

VPATH = $(DIRS)

$(info $$DIRS = $(DIRS))
$(info $$OBJS = $(OBJS))

$(BUILD_DIR)/%.o: %.c
	$(Q)echo 'cc: $<'
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -c "$<" -o "$@" $(LFLAGS)

$(TARGET_EXEC): $(OBJS)
	$(Q)echo 'linking $@'
	$(Q)$(CC) $(OBJS) -o $@

test: $(TARGET_EXEC)
	$(Q)echo 'running $@'
	$(Q) ./$(TARGET_EXEC)


clean:
	rm -f $(BUILD_DIR)/*.*
	rm -f $(TARGET_EXEC)

all: $(TARGET_EXEC)


.PHONY: clean all
