# Natural Earth (world map)

`kernel/worldmap_data.h` (the world map of Settings > Date/Time > Time Zone) is a 256x102 one-bit rasterization of the **Natural Earth 1:110m
land polygons**, made by `tools/gen_worldmap.py`. Source: https://github.com/nvkelso/natural-earth-vector (`geojson/ne_110m_land.geojson`).

Natural Earth is in the **public domain** ("All versions of Natural Earth raster + vector map data found on this website are in the public
domain. You may use the maps in any manner, including modifying the content and design, electronic dissemination, and offset printing.",
https://www.naturalearthdata.com/about/terms-of-use/). No attribution is required; it is recorded here anyway.
The GeoJSON itself is not stored in this repository -- only the small bitmap it produces.
