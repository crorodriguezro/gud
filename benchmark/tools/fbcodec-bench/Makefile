# fbcodec-bench Makefile
#
# Builds the isolated RGB565 framebuffer compression benchmark harness.
# This tool is standalone: it vendors LZ4 (already used by gud.ko),
# QOI and QOIR (single-header libraries) and links against the system
# CharLS shared object for JPEG-LS. It does not modify or depend on any
# in-tree production kernel/driver code.
#
# CharLS note: this development environment only has the CharLS *runtime*
# package installed (no -devel package, no root to install one), so there
# is no unversioned libcharls.so to link with -lcharls. CHARLS_LIB points
# directly at the versioned .so; override it if your system has the devel
# package (e.g. `make CHARLS_LIB=-lcharls`).
CHARLS_LIB ?= /usr/lib64/libcharls.so.2

CC ?= gcc
CSTD ?= -std=gnu11
OPT ?= -O2
WARN ?= -Wall -Wextra -Wno-unused-parameter -Wno-stringop-truncation
DEFS ?=
INCLUDES := -Isrc -Ivendor -Ivendor/charls

CFLAGS := $(CSTD) $(OPT) $(WARN) $(DEFS) $(INCLUDES)
LDLIBS := -lm $(CHARLS_LIB)

BUILD_DIR := build

SRCS := \
	src/common.c \
	src/lz4_util.c \
	src/codec_registry.c \
	src/main.c \
	src/transforms/predict.c \
	src/transforms/quant.c \
	src/transforms/temporal.c \
	src/transforms/rle.c \
	src/codecs/raw_codec.c \
	src/codecs/lz4_codec.c \
	src/codecs/generic_lz4_codec.c \
	src/codecs/predictor_codecs.c \
	src/codecs/quant_codecs.c \
	src/codecs/rle_codec.c \
	src/codecs/temporal_codecs.c \
	src/codecs/qoi_codec.c \
	src/codecs/qoir_codec.c \
	src/codecs/charls_codec.c \
	src/corpus/synthetic.c \
	src/corpus/capture.c \
	src/quality/metrics.c \
	src/quality/png_out.c \
	src/report/stats.c \
	src/report/csv_out.c \
	src/report/json_out.c \
	vendor/lz4/lz4.c

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

BIN := $(BUILD_DIR)/fbcodec-bench

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

TEST_COMMON_SRCS := \
	src/common.c \
	src/lz4_util.c \
	src/transforms/predict.c \
	src/transforms/quant.c \
	src/transforms/temporal.c \
	src/transforms/rle.c \
	vendor/lz4/lz4.c

TEST_TRANSFORMS_BIN := $(BUILD_DIR)/test_transforms
TEST_TEMPORAL_BIN := $(BUILD_DIR)/test_temporal

test: $(TEST_TRANSFORMS_BIN) $(TEST_TEMPORAL_BIN)
	$(TEST_TRANSFORMS_BIN)
	$(TEST_TEMPORAL_BIN)

$(TEST_TRANSFORMS_BIN): tests/test_transforms.c $(TEST_COMMON_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/test_transforms.c $(TEST_COMMON_SRCS) -o $@ -lm

$(TEST_TEMPORAL_BIN): tests/test_temporal.c $(TEST_COMMON_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/test_temporal.c $(TEST_COMMON_SRCS) -o $@ -lm

clean:
	rm -rf $(BUILD_DIR)
