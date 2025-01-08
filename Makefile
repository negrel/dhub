PROJECT_DIR = $(shell git rev-parse --show-toplevel)
BUILD_DIR := $(PROJECT_DIR)/build
CMD_DIR := $(PROJECT_DIR)/cmd
MODULES_DIR := $(PROJECT_DIR)/modules

CC := clang
CFLAGS ?= -Wall -Wextra -Werror

ifeq ($(DEBUG), 1)
	CFLAGS += -g -DMODDIR=\"$(BUILD_DIR)/modules\"
else
	CFLAGS += -O2
endif

SRCS := $(shell find $(PROJECT_DIR)/src -type f -name '*.c')

CMD_DHUB_SRCS := $(shell find $(CMD_DIR)/dhub -type f -name '*.c')
CMD_DHUB_CFLAGS := -I$(PROJECT_DIR)/cmd/dhub -I$(PROJECT_DIR)/include \
	-I$(PROJECT_DIR)/src $(shell pkg-config --cflags --libs basu libuv)

.PHONY: all
all: clean $(BUILD_DIR)/dhub

dhub/%: $(BUILD_DIR)/dhub build/modules
	$(BUILD_DIR)/dhub $*

build/modules: build/module/echo

build/module/%: $(BUILD_DIR)/modules/%.so
	@true

build/%: $(BUILD_DIR)/%
	@true

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) compile_flags.txt

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
		$(shell pkg-config --cflags --libs basu libuv) \
		-o "$(BUILD_DIR)/dhub"

$(BUILD_DIR)/modules/%.so: $(BUILD_DIR)/modules
	$(CC) \
		-shared \
		$(CFLAGS) \
		-I$(PROJECT_DIR)/include \
		$(shell pkg-config --cflags basu libuv) \
		$(MODULES_DIR)/$*.c \
		-o "$(BUILD_DIR)/modules/$*.so"
