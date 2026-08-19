ifneq ($(KERNELRELEASE),)
include Kbuild
else
MANIFEST ?= $(CURDIR)/env/target-manifest.env

ifneq (,$(wildcard $(MANIFEST)))
include $(MANIFEST)
endif

.PHONY: modules clean xdisp-lz4-12800 xdisp-lz4-12800-clean xdisp-lz4-12800-pmtest xdisp-lz4-12800-pmtest-clean

modules:
	@test -f "$(MANIFEST)" || { echo "Missing $(MANIFEST); copy env/target-manifest.env.example first"; exit 2; }
	@test -n "$(KERNEL_BUILD_DIR)" || { echo "KERNEL_BUILD_DIR is not set in $(MANIFEST)"; exit 2; }
	$(MAKE) -C "$(KERNEL_BUILD_DIR)" M="$(CURDIR)" ARCH=arm64 CROSS_COMPILE="$(CROSS_COMPILE)" modules

clean:
	@test -n "$(KERNEL_BUILD_DIR)" || exit 0
	$(MAKE) -C "$(KERNEL_BUILD_DIR)" M="$(CURDIR)" clean

xdisp-lz4-12800:
	$(MAKE) -C variants/xdisp-lz4-12800 \
		MANIFEST="$(abspath $(MANIFEST))" modules

xdisp-lz4-12800-clean:
	$(MAKE) -C variants/xdisp-lz4-12800 \
		MANIFEST="$(abspath $(MANIFEST))" clean

xdisp-lz4-12800-pmtest:
	$(MAKE) -C variants/xdisp-lz4-12800-pmtest \
		MANIFEST="$(abspath $(MANIFEST))" modules

xdisp-lz4-12800-pmtest-clean:
	$(MAKE) -C variants/xdisp-lz4-12800-pmtest \
		MANIFEST="$(abspath $(MANIFEST))" clean
endif
