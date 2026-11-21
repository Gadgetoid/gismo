CORE = ../../src/core
CXXFLAGS = -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -I$(CORE)

MAP_PACK ?= ../../maps/cambridge.gmap

ROUTE_GRAPH ?= ../../maps/cambridge.groute

check: test_core grid_vectors.csv map_tool jpeg_tool route_tool
	./test_core
	uv run --quiet check_zip.py zip_test.zip zip_input.bin
	uv run --quiet check_map_tool.py ./map_tool $(MAP_PACK)
	uv run --quiet check_jpeg.py ./jpeg_tool $(MAP_PACK)
	uv run --quiet check_routes.py ./route_tool $(ROUTE_GRAPH)

CORE_SOURCES = $(CORE)/nmea.cpp $(CORE)/grid.cpp $(CORE)/mission.cpp $(CORE)/session_log.cpp $(CORE)/filter.cpp $(CORE)/draw.cpp $(CORE)/settings.cpp $(CORE)/places.cpp $(CORE)/zip_writer.cpp

test_core: test_core.cpp $(CORE_SOURCES) $(wildcard $(CORE)/*.h)
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp $(CORE_SOURCES)

map_tool: map_tool.cpp $(CORE)/map_pack.cpp $(CORE)/inflate.cpp $(CORE)/map_pack.h $(CORE)/inflate.h
	$(CXX) $(CXXFLAGS) -O2 -o $@ map_tool.cpp $(CORE)/map_pack.cpp $(CORE)/inflate.cpp

jpeg_tool: jpeg_tool.cpp $(CORE)/jpeg.cpp $(CORE)/jpeg.h
	$(CXX) $(CXXFLAGS) -O2 -o $@ jpeg_tool.cpp $(CORE)/jpeg.cpp

route_tool: route_tool.cpp $(CORE)/route.cpp $(CORE)/route.h
	$(CXX) $(CXXFLAGS) -O2 -o $@ route_tool.cpp $(CORE)/route.cpp

grid_vectors.csv: grid_vectors.py
	uv run --quiet grid_vectors.py > $@

clean:
	rm -f test_core map_tool jpeg_tool route_tool zip_test.zip zip_input.bin

.PHONY: check clean
