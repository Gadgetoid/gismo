# /// script
# requires-python = ">=3.10"
# dependencies = ["pyproj"]
# ///

import random
import sys

from pyproj import Transformer

helmert = Transformer.from_pipeline(
    "+proj=pipeline +step +proj=axisswap +order=2,1 +step +proj=unitconvert +xy_in=deg +xy_out=rad "
    "+step +proj=push +v_3 +step +proj=cart +ellps=WGS84 +step +inv +proj=helmert +x=446.448 +y=-125.157 +z=542.06 "
    "+rx=0.1502 +ry=0.247 +rz=0.8421 +s=-20.4894 +convention=position_vector +step +inv +proj=cart +ellps=airy "
    "+step +proj=pop +v_3 +step +proj=tmerc +lat_0=49 +lon_0=-2 +k=0.9996012717 +x_0=400000 +y_0=-100000 +ellps=airy"
)
default = Transformer.from_crs("EPSG:4326", "EPSG:27700")

generator = random.Random(1)
points = [(52.2053, 0.1218), (50.0657, -5.7132), (58.6373, -3.0689), (51.4778, -0.0014), (52.678456, -0.449838)]
points += [(generator.uniform(49.9, 60.8), generator.uniform(-7.5, 1.8)) for _ in range(200)]

for latitude, longitude in points:
    easting, northing = helmert.transform(latitude, longitude)
    default_easting, default_northing = default.transform(latitude, longitude)
    sys.stdout.write(f"{latitude:.9f},{longitude:.9f},{easting:.4f},{northing:.4f},{default_easting:.4f},{default_northing:.4f}\n")
