# ============================================================================
# Builds the four independent execution engines of the cathedral.
# ============================================================================
BUILD := build

.PHONY: all clean
all: $(BUILD)/engine_c $(BUILD)/engine_rust $(BUILD)/engine_go $(BUILD)/Engine.class $(BUILD)/sidecar

$(BUILD):
	mkdir -p $(BUILD)

# ENGINE A — C gate-level VM
$(BUILD)/engine_c: engines/vm_gates.c | $(BUILD)
	gcc -O2 -std=c11 -Wall -o $@ engines/vm_gates.c

# ENGINE B — Rust gate-level VM (-O so overflow wraps instead of panicking)
$(BUILD)/engine_rust: engines/engine_rust.rs | $(BUILD)
	rustc -O engines/engine_rust.rs -o $@

# ENGINE C — Go gate-level VM (local toolchain only; no network fetch)
$(BUILD)/engine_go: engines/engine_go/main.go engines/engine_go/go.mod | $(BUILD)
	cd engines/engine_go && GOTOOLCHAIN=local GOFLAGS=-mod=mod go build -o ../../$(BUILD)/engine_go .

# ENGINE D — Java sane oracle
$(BUILD)/Engine.class: engines/Engine.java | $(BUILD)
	javac -d $(BUILD) engines/Engine.java

# Generic HTTP sidecar (used by --service mode to host CLI engines)
$(BUILD)/sidecar: services/sidecar/main.go services/sidecar/go.mod | $(BUILD)
	cd services/sidecar && GOTOOLCHAIN=local GOFLAGS=-mod=mod go build -o ../../$(BUILD)/sidecar .

clean:
	rm -rf $(BUILD)
