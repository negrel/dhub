PROJECT_DIR ?= $(shell git rev-parse --show-toplevel)
BUILD_DIR ?= $(PROJECT_DIR)/build
CMD_DIR := $(PROJECT_DIR)/cmd
MODULES_DIR := $(PROJECT_DIR)/modules

CC := clang
CFLAGS ?= -Wall -Wextra -Werror

DEPS_CFLAGS := $(shell pkg-config --cflags --libs basu libuv libudev)

ifeq ($(DEBUG), 1)
	CFLAGS += -g -DMODDIR=\"$(BUILD_DIR)/modules\"
else
	CFLAGS += -O2
endif

SRCS := $(shell find $(PROJECT_DIR)/src -type f -name '*.c')

CMD_DHUB_SRCS := $(shell find $(CMD_DIR)/dhub -type f -name '*.c')
CMD_DHUB_CFLAGS := -I$(PROJECT_DIR)/cmd/dhub -I$(PROJECT_DIR)/include \
	-I$(PROJECT_DIR)/src $(DEPS_CFLAGS)

.PHONY: all
all: clean $(BUILD_DIR)/dhub

dhub/%: $(BUILD_DIR)/dhub build/modules
	valgrind --leak-check=full $(BUILD_DIR)/dhub $*

build/modules: build/module/echo build/module/power_udev

build/module/%: $(BUILD_DIR)/modules/%.so
	@true

build/%: $(BUILD_DIR)/%
	@true

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: format
format:
	find $(CMD_DIR) $(PROJECT_DIR)/src $(PROJECT_DIR)/include \
		\( -name '*.c' -or -name '*.h' \) -exec clang-format -i {} \;

.PHONY: compile_flags.txt
compile_flags.txt:
	echo $(CFLAGS) $(CMD_DHUB_CFLAGS) | tr ' ' '\n' > compile_flags.txt

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/modules:
	mkdir -p $(BUILD_DIR)/modules

$(BUILD_DIR)/dhub: $(BUILD_DIR) $(SRCS) $(CMD_DHUB_SRCS)
# -rdynamic so modules can access D-Hub symbols (log functions, libuv, etc).
	$(CC) \
		-rdynamic \
		-I$(PROJECT_DIR)/cmd/dhub -I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		$(CMD_DHUB_SRCS) $(SRCS) \
		$(CFLAGS) \
		$(DEPS_CFLAGS) \
		-o "$@"

$(BUILD_DIR)/modules/%.so: $(MODULES_DIR)/%.c $(BUILD_DIR)/modules
	$(CC) \
		-shared -fPIC \
		$(CFLAGS) \
		-I$(PROJECT_DIR)/include \
		$(DEPS_CFLAGS) \
		"$<" \
		-o "$@"

$(BUILD_DIR)/modules/power_udev.so: $(MODULES_DIR)/power/udev.c $(BUILD_DIR)/modules
	$(CC) \
		-shared -fPIC \
		$(CFLAGS) \
		-I$(PROJECT_DIR)/include -I$(PROJECT_DIR)/src \
		$(DEPS_CFLAGS) \
		"$<" \
		-o "$@"
