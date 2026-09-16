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

.PHONY: help
help:
	@echo "Usage: make DEPS_FILE=path/to/deps.txt [FORCE=1]"
	@echo "       make svn-login [DEPS_FILE=path/to/deps.txt] [SVN_URL=url] [SVN_USER=user]"
