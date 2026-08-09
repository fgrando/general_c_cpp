BUILDS := buildA buildB buildC

.PHONY: all buildAPI $(BUILDS)

all: $(BUILDS)

# buildAPI must complete before ANY of the parallel builds start.
# Declaring it as a prerequisite of each build (no recipe here) creates
# the edge; the recipe below is shared by all four targets.
$(BUILDS): buildAPI

buildAPI $(BUILDS):
	$(MAKE) -C $@ -j1 all
