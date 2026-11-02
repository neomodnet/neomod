# neomod.js is linked from the game objects, so it needs a build directory configured for wasm:
#   mkdir wasm-release-build && cd wasm-release-build && emconfigure ../configure
WASM_BUILD ?= ../../wasm-release-build

neomod.js:
	emmake $(MAKE) -C $(WASM_BUILD) neomodjs
	@mkdir -p bin
	cp $(WASM_BUILD)/neomodjs/neomod.js $(WASM_BUILD)/neomodjs/neomod.wasm bin/

clean:
	rm -rf bin

.PHONY: neomod.js clean
