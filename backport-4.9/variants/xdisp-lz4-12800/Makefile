ifneq ($(KERNELRELEASE),)
include Kbuild
else
MANIFEST ?= $(CURDIR)/../../env/target-manifest.env

ifneq (,$(wildcard $(MANIFEST)))
include $(MANIFEST)
endif

.PHONY: modules clean

modules:
	@test -f "$(MANIFEST)" || { echo "Missing $(MANIFEST)"; exit 2; }
	@test -n "$(KERNEL_BUILD_DIR)" || { echo "KERNEL_BUILD_DIR is not set in $(MANIFEST)"; exit 2; }
	$(MAKE) -C "$(KERNEL_BUILD_DIR)" M="$(CURDIR)" ARCH=arm64 \
		CROSS_COMPILE="$(CROSS_COMPILE)" modules

clean:
	@test -n "$(KERNEL_BUILD_DIR)" || exit 0
	$(MAKE) -C "$(KERNEL_BUILD_DIR)" M="$(CURDIR)" clean
endif
