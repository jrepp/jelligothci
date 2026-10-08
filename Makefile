.PHONY: build run test sanitize core-test esp-bootstrap esp-sync esp-build esp-flash esp-monitor docs-check docs-fix docs-guide
.PHONY: run-shapes
.PHONY: hooks-install hooks-check lint-c format-c

build:
	cmake --preset desktop
	cmake --build --preset desktop

run: build
	./build/desktop/jelligotchi --pet --save build/pet-save --debug-socket build/jelli-debug.sock

run-shapes: build
	./build/desktop/jelligotchi --shapes

test: build
	ctest --preset desktop

sanitize:
	cmake --preset sanitize
	cmake --build --preset sanitize
	ctest --preset sanitize

core-test:
	cmake --preset core
	cmake --build --preset core
	ctest --preset core

esp-bootstrap:
	./scripts/esp bootstrap

esp-sync:
	./scripts/esp sync

esp-build:
	./scripts/esp build

esp-flash:
	@test -n "$(PORT)" || (echo 'Usage: make esp-flash PORT=/dev/cu.usbmodem…'; exit 2)
	./scripts/esp flash "$(PORT)"

esp-monitor:
	@test -n "$(PORT)" || (echo 'Usage: make esp-monitor PORT=/dev/cu.usbmodem…'; exit 2)
	./scripts/esp monitor "$(PORT)"

docs-check:
	./scripts/docs validate --dry-run --skip-build

docs-fix:
	./scripts/docs validate --skip-build

docs-guide:
	./scripts/docs bootstrap

hooks-install:
	./scripts/pre-commit validate-config
	git config --local core.hooksPath .githooks

hooks-check:
	./scripts/pre-commit run --all-files

lint-c:
	./scripts/c-lint check

format-c:
	./scripts/c-lint format
