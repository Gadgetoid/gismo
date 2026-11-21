GRIMACE ?= ../../grimace
TOOLCHAIN = $(abspath $(GRIMACE))/cmake/toolchain.cmake
BUILD = build/junosb
CARD = build/card/GISmo
AREAS ?= $(wildcard local/areas/*.toml)
DOWNLOADS ?= downloads
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

all:
	cmake -S . -B $(BUILD) -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) -DGRIMACE_ARCH=arm -DGRIMACE_CE_VERSION=5.2 -DGRIMACE_DEVICE=junosb $(if $(GRIMACE_ARM_LLVM),-DGRIMACE_LLVM_ROOT=$(GRIMACE_ARM_LLVM))
	cmake --build $(BUILD)
	python3 $(abspath $(GRIMACE))/tests/check-imports.py $(BUILD) --arch arm --version 5.2 --device junosb

maps:
	for area in $(AREAS); do (cd tools && uv run --quiet build_area.py ../$$area --output ../build/maps/$$(basename $$area .toml) --downloads $(abspath $(DOWNLOADS))) || exit 1; done

example-maps:
	cd tools && uv run --quiet build_area.py ../areas/cambridge.toml --output ../maps --downloads $(abspath $(DOWNLOADS))

card: all maps
	rm -rf $(CARD)
	mkdir -p $(CARD)/Maps $(CARD)/Missions $(CARD)/Filters $(CARD)/Planner
	cp $(BUILD)/GISmo.exe $(BUILD)/KeyTest.exe $(BUILD)/CamTest.exe $(CARD)/
	cp maps/* $(wildcard build/maps/*/*) $(CARD)/Maps/
	cp missions/*.txt $(wildcard local/missions/*) $(CARD)/Missions/
	cp filters/*.flt filters/LICENSE-campsnap-filters $(CARD)/Filters/
	cp planner/*.html planner/*.js planner/*.css $(CARD)/Planner/
	cd tools && uv run --quiet make_nmea.py --output ../$(CARD)/replay.nmea.example

screens:
	$(MAKE) -C tests/screens

browser-test:
	$(MAKE) -C tests/planner browser

test:
	$(MAKE) -C tests/host
	$(MAKE) -C tests/planner

clean:
	rm -rf $(BUILD) build/card

.PHONY: all maps example-maps card screens browser-test test clean
