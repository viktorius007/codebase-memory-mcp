PYTHON ?= python3
HEADER ?= ../../internal/cbm/lsp/rust_std_inventory.h

.PHONY: check generate probe
check:
	$(PYTHON) generate.py --check --output $(HEADER)

generate:
	$(PYTHON) generate.py --output $(HEADER)

probe:
	@if test -z "$(WORK_DIR)"; then echo "FAIL: WORK_DIR is required"; exit 2; fi
	@$(PYTHON) probe.py --header $(HEADER) --work-dir "$(WORK_DIR)"
