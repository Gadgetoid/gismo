MAP_PACK ?= ../../maps/cambridge.gmap
ROUTE_GRAPH ?= ../../maps/cambridge.groute

check: ../host/jpeg_tool
	node test.js "$$(uv run --quiet expectations.py $(MAP_PACK) $(ROUTE_GRAPH))" $(MAP_PACK) $(ROUTE_GRAPH)

../host/jpeg_tool:
	$(MAKE) -C ../host jpeg_tool

browser: ../host/jpeg_tool
	uv run --quiet browser_test.py

.PHONY: check browser
