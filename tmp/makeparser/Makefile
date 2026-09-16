# Makefile to parse a dependency list and `svn export` each entry.
#
# Deps file format (whitespace-separated columns):
#   <label>  <svn-url>  <revision>  <destination-path>
# Blank lines and lines starting with '#' are ignored.
#
# Usage:
#   make DEPS_FILE=path/to/deps.txt
#   make DEPS_FILE=path/to/deps.txt FORCE=1   # overwrite existing destinations
#   make svn-login DEPS_FILE=path/to/deps.txt [SVN_USER=user]   # cache credentials first

SHELL := /bin/bash

DEPS_FILE ?= deps.txt
FORCE ?= 0
SVN_URL ?=
SVN_USER ?=

PACK_DIR ?= pack
PACK_PATHS ?=
PACK_TAR ?= pack.tar

.PHONY: all
all: export-deps

.PHONY: export-deps
export-deps:
	@test -f "$(DEPS_FILE)" || { echo "Deps file not found: $(DEPS_FILE)" >&2; exit 1; }
	@while IFS= read -r line || [ -n "$$line" ]; do \
		line=$$(printf '%s' "$$line" | sed -e 's/\r$$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$$//'); \
		case "$$line" in \
			''|'#'*) continue ;; \
		esac; \
		set -- $$line; \
		label=$$1; url=$$2; rev=$$3; dest=$$4; \
		if [ -z "$$label" ] || [ -z "$$url" ] || [ -z "$$rev" ] || [ -z "$$dest" ]; then \
			echo "Skipping malformed line: $$line" >&2; \
			continue; \
		fi; \
		destdir=$$(dirname "$$dest"); \
		mkdir -p "$$destdir"; \
		if [ -e "$$dest" ] && [ "$(FORCE)" != "1" ]; then \
			echo "[$$label] $$dest already exists, skipping (use FORCE=1 to overwrite)"; \
			continue; \
		fi; \
		echo "[$$label] svn export -r $$rev $$url -> $$dest"; \
		svn export --force -r "$$rev" "$$url" "$$dest" || exit 1; \
	done < "$(DEPS_FILE)"

# Primes the local svn credential cache by making one authenticated request,
# so the (silent, non-interactive) `svn export` calls in export-deps don't
# hang waiting on a username/password prompt. Run this once beforehand,
# interactively, whenever credentials aren't already cached.
#
# By default it authenticates against the first URL found in DEPS_FILE;
# override with SVN_URL to target a specific repo instead. SVN_USER
# pre-fills the username so only the password is prompted for.
.PHONY: svn-login
svn-login:
	@repo_url="$(SVN_URL)"; \
	if [ -z "$$repo_url" ]; then \
		test -f "$(DEPS_FILE)" || { echo "Deps file not found: $(DEPS_FILE)" >&2; exit 1; }; \
		repo_url=$$(sed -e 's/\r$$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$$//' "$(DEPS_FILE)" \
			| grep -v '^$$' | grep -v '^#' | head -n1 | awk '{print $$2}'); \
	fi; \
	if [ -z "$$repo_url" ]; then \
		echo "No repository URL to authenticate against. Set SVN_URL=<url> or check DEPS_FILE." >&2; \
		exit 1; \
	fi; \
	echo "Authenticating against $$repo_url (credentials will be cached for later exports) ..."; \
	if [ -n "$(SVN_USER)" ]; then \
		svn info --username "$(SVN_USER)" "$$repo_url"; \
	else \
		svn info "$$repo_url"; \
	fi

# Maps MAP_PATH to the first available drive letter in MAP_DRIVES (a
# comma-separated list, e.g. F,G,H,I) using `subst`. A drive is considered
# available if it isn't currently in use (subst'd, mapped, or a real drive).
# Checked against `subst`'s own listing rather than just `-d X:/`, since a
# drive substed to a since-deleted folder still fails re-subst ("Drive
# already SUBSTed") even though `-d` would wrongly call it free.
.PHONY: mapdrive
mapdrive:
	@test -n "$(MAP_PATH)" || { echo "MAP_PATH is required, e.g. MAP_PATH='C:\\the path\\workdir'" >&2; exit 1; }; \
	test -n "$(MAP_DRIVES)" || { echo "MAP_DRIVES is required, e.g. MAP_DRIVES=F,G,H,I" >&2; exit 1; }; \
	drive=""; \
	drives="$(MAP_DRIVES)"; \
	substed="$$(subst)"; \
	IFS=','; \
	for d in $$drives; do \
		d=$$(printf '%s' "$$d" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$$//' -e 's/://'); \
		[ -n "$$d" ] || continue; \
		if ! printf '%s\n' "$$substed" | grep -qi "^$${d}:" && [ ! -d "$${d}:/" ]; then \
			drive=$$d; \
			break; \
		fi; \
	done; \
	unset IFS; \
	if [ -z "$$drive" ]; then \
		echo "No available drive letter among: $(MAP_DRIVES)" >&2; \
		exit 1; \
	fi; \
	echo "Mapping '$(MAP_PATH)' -> $${drive}:"; \
	subst "$${drive}:" "$(MAP_PATH)" || exit 1

# Copies each path in PACK_PATHS (a comma-separated list) into a fresh
# PACK_DIR, then tars up PACK_DIR into PACK_TAR.
.PHONY: pack
pack:
	@test -n "$(PACK_PATHS)" || { echo "PACK_PATHS is required, e.g. PACK_PATHS=\"C:\\a\\path,C:\\b\\path\"" >&2; exit 1; }; \
	case "$(PACK_DIR)" in \
		""|"."|".."|"/") echo "Refusing to use PACK_DIR='$(PACK_DIR)'" >&2; exit 1 ;; \
	esac; \
	rm -rf "$(PACK_DIR)" && mkdir -p "$(PACK_DIR)" || exit 1; \
	paths="$(PACK_PATHS)"; \
	IFS=','; \
	for p in $$paths; do \
		p=$$(printf '%s' "$$p" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$$//'); \
		[ -n "$$p" ] || continue; \
		if [ ! -e "$$p" ]; then \
			echo "Path not found: $$p" >&2; \
			exit 1; \
		fi; \
		echo "[pack] copying $$p -> $(PACK_DIR)/"; \
		cp -r "$$p" "$(PACK_DIR)/" || exit 1; \
	done; \
	unset IFS; \
	echo "[pack] creating $(PACK_TAR)"; \
	tar -cf "$(PACK_TAR)" -C "$(PACK_DIR)" . || exit 1; \
	echo "[pack] done: $(PACK_TAR)"

.PHONY: help
help:
	@echo "Usage: make DEPS_FILE=path/to/deps.txt [FORCE=1]"
	@echo "       make svn-login [DEPS_FILE=path/to/deps.txt] [SVN_URL=url] [SVN_USER=user]"
	@echo "       make mapdrive MAP_PATH='C:\\the path\\workdir' MAP_DRIVES=F,G,H,I"
	@echo "       make pack PACK_PATHS='C:\\a\\path,C:\\b\\path' [PACK_DIR=pack] [PACK_TAR=pack.tar]"
