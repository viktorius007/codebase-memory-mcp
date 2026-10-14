PYTHON ?= python3
HEADER ?= ../../internal/cbm/lsp/rust_std_inventory.h

.PHONY: check generate
check:
	$(PYTHON) generate.py --check --output $(HEADER)

generate:
	$(PYTHON) generate.py --output $(HEADER)
