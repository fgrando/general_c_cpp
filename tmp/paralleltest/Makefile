BUILDS := buildA buildB buildC

# On this system make reports its own path in Windows-native form
# (C:/...), which the MSYS shell can't resolve; force PATH lookup instead.
MAKE := make
SHELL := /bin/bash
export VAR1 := MYVAR1

.PHONY: all consolidate-logs $(BUILDS)
all:
	@start=$$(date +%s); \
	$(MAKE) --no-print-directory $(BUILDS); \
	$(MAKE) --no-print-directory consolidate-logs; \
	end=$$(date +%s); \
	echo "Total time: $$((end - start))s"

consolidate-logs:
	cat $(addsuffix .log,$(BUILDS)) > build.log

$(BUILDS):
	set -o pipefail; $(MAKE) -C $@ -j1 all | tee $@.log 2>&1;
#	set -o pipefail; $(MAKE) -C $@    all | tee $@.log 2>&1;

