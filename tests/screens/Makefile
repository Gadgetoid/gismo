REPOSITORY = ../..
BUILD = $(REPOSITORY)/build/screens
CARD = $(BUILD)/card
CXXFLAGS = -std=c++20 -fno-exceptions -fno-rtti -O1 -w -I windows -I $(REPOSITORY)/src/core -I $(REPOSITORY)/src/platform -I $(REPOSITORY)/src/app
APP_SOURCES = $(filter-out %/main.cpp,$(wildcard $(REPOSITORY)/src/app/*.cpp))
CORE_SOURCES = $(wildcard $(REPOSITORY)/src/core/*.cpp)
SOURCES = screens.cpp shim.cpp $(APP_SOURCES) $(CORE_SOURCES) $(REPOSITORY)/src/platform/bitmap_file.cpp $(REPOSITORY)/src/devices/junosb/device.cpp

screens: $(BUILD)/screens
	rm -rf $(CARD) $(BUILD)/frames $(BUILD)/*.png
	mkdir -p $(CARD)/Maps $(CARD)/Filters $(BUILD)/frames
	cp $(REPOSITORY)/maps/* $(CARD)/Maps/
	cp $(REPOSITORY)/filters/*.flt $(CARD)/Filters/
	$(BUILD)/screens $(abspath $(REPOSITORY)) $(abspath $(CARD)) $(abspath $(BUILD)/frames) > /dev/null
	uv run --quiet contact_sheet.py $(BUILD)/frames $(BUILD)
	uv run --quiet check_outing.py $(CARD)/Outings

$(BUILD)/screens: $(SOURCES) $(wildcard *.h windows/*.h $(REPOSITORY)/src/*/*.h)
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES)

.PHONY: screens
