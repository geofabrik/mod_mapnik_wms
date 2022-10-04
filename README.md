# mod_mapnik_wms

`mod_mapnik_wms` is an Apache module for building a
[Mapnik](https://github.com/mapnik/mapnik) based 
[WMS](https://wiki.openstreetmap.org/wiki/WMS) server. It was initially 
written by Frederik Ramm for internal [Geofabrik](https://www.geofabrik.de/) 
use, but now is available under the GPL.

## What is WMS?

[WMS (Web Map Service)](https://en.wikipedia.org/wiki/Web_Map_Service) is an OGC standard for serving maps. Essentially, it specifies a set of HTTP URL parameters used by a client to retrieve a
map image from a server.

Compared to the usual tile servers we use in OSM, a WMS offers more
flexibility but eats more server resources, because WMS images are not
cached but created individually for each request. This makes it unsuitable
for a much-used slippy map, but better suited for a GIS environment that 
might require maps in different projections, scales, or resolutions.

## What is mod_mapnik_wms?

mod_mapnik_wms is a module for the widely-used Apache web server that
speaks the WMS protocol. In fact the WMS standard is a bit convoluted
and has many extensions and optional extras, but mod_mapnik_wms
currently only supports the bare minimum: The `GetCapabilities` call
which tells the client what this server can do, and the `GetMap` call
which produces a map. For rendering the map, mod_mapnik_wms uses the
Mapnik library.

## Building mod_mapnik_wms

To build mod_mapnik_wms, you need to have the following installed:

-   Mapnik (with PROJ support - some versions of Ubuntu have this 
    disabled and hence will not work)
-   Apache2 development package (`apache2-dev` and `libapr1-dev`)
-   libgd2 development package

You should be able to build a Debian package by just running `debuild`,
but you can also choose to run "make". 

You may have to adjust some Mapnik version numbers (`grep -i mapnik debian/*`).

## Installing and Configuring mod_mapnik_wms

Before you install mod_mapnik_wms, make sure to have a working Mapnik
installation, including the style sheet you want to use, with the
database connection and shape files properly set up and configured. Test
that with generate_image.py from the OSM Mapnik package, or with
nik2img.py. **Do not continue with mod_mapnik_wms installation if you
are unsure whether your Mapnik installation works at all.**

Your Apache configuration must be modified to load the new module. If
you install the module through the Debian package, that should be done
automatically. Otherwise you will have to place the commands

    LoadFile /usr/lib/libstdc++.so.6
    LoadFile /usr/lib/libmapnik.so.0.6
    LoadFile /usr/lib/libgd.so.2
    LoadModule mapnik_wms_module /usr/lib/apache2/modules/mod_mapnik_wms.so

somewhere in your Apache config. The library version numbers may have to
be changed depending on what's available on your system (which is
hopefully the same you had on the build system).

Then you must configure the WMS module. This is done through Apache
configuration directives, usually in the virtual host's config file in
your `/etc/apache/sites-available/` directory. An example configuration
is provided in `server_config.example`.

The mod_mapnik_wms-specific configuration options are:

| Option | Default | Use |
|--------|---------|-----|
| `MapnikLog` | none | File to redirect the "clog" stream to. |
| `MapnikDatasources` | none (required) | Path to Mapnik data source modules (plugins), usually `/usr/lib/mapnik/input`. May occur more than once. |
| `MapnikFonts` | none (required) | Path to one font file used in map files. May occur more than once. mod_mapnik_wms cannot recurse a font directory - each font has to be specified separately. |
| `MapnikMap` | none (required) | Path to the map file (mapnik XML file). Currently only one is supported. |
| `WmsTitle` | unset | WMS server title you want to return for GetCapability requests. |
| `WmsUrl` | unset | The URL under which your WMS server can be reached from the outside. It is used in constructing the GetCapabilities response. Note that clients will use this URL to access the WMS service even if they have retrieved the capabilities document through another channel, e.g. if you have a port forwarding set up and have your client connect to localhost:1234 to retrieve the capabilities document, it will only use localhost:1234 for the map request if this is actually specified in the WmsUrl. |
| `WmsPrefix` | unset | The mod_mapnik_wms module will typically handle each and every request sent to the web server that has a valid WMS request in its query string. If this option is used, then only request URIs beginning with this string will be considered. |
| `WmsDebug` | `false` | If true, the map file will be loaded for each request instead of once at startup which makes fiddling with the style easier. |
| `WmsSrs` | none | A list of allowed SRS names, separated by spaces. They must be supported by the underlying Mapnik installation. You will usually want to have at least EPSG:4326 in that list. |
| `WmsExtentMinLon`, `WmsExtentMaxLon`, `WmsExtentMinLat`, `WmsExtentMaxLat` | -179.9999, -89.999, 179.9999, 89.999 | The data bounding box to be published in the capabilities document |
| `WmsMaxWidth`, `WmsMaxHeight`, `WmsMinWidth`, `WmsMinHeight` | 1 for min, none for max | The allowed image size range for WMS requests (in pixels) |
| `WmsDefaultDpi` | 90 | Default resolution (when not explicitly requested by the client). |

## Map Style

Built on Mapnik, mod_mapnik_wms supports any map style that Mapnik supports. 
Frequently you will create a style using CartoCSS and then compile that to 
Mapnik XML with a tool like [`magnacarto`](https://github.com/omniscale/magnacarto) or [`carto`](https://github.com/mapbox/carto). 

Note that while mod_mapnik_wms can process any tile server stile as-is, 
such styles will typically exhibit poor performance on low zoom levels (i.e.
large map scales) because tile servers are not optimised for that - they
can simply pre-render the map on these scales.

## Layers

WMS supports layers. The server can announce to the client which layers
it supports, and the client can make a selection from them. 

The following options control if and how layers are exposed to the client:

| Option | Default | Use |
|--------|---------|-----|
| `WmsTopLayerTitle` | OpenStreetMap WMS | WMS top layer title published in the `GetCapabilities` response. |
| `WmsTopLayerName` | OpenStreetMap WMS | WMS top layer name published in the `GetCapabilities` response. |
|` WmsIncludeSubLayers` | false | Expose individual layers to the WMS client. Must be used in conjunction with one or more `WmsLayerGroup` options. |
| WmsLayerGroup | unset | This option groups a number of Mapnik layers into one named WMS layer. It can appear multiple times and takes four arguments:<ol><li>WMS layer name</li><li>WMS layer description</li><li>1 if the layer should be drawn when transparency is requested by the client, 0 if not</li><li>comma-separated list of Mapnik layers that should be drawn when this WMS layer is requested</li></ol> |

## API keys

This module is used by Geofabrik to serve WMS content to paying
customers. Since not all WMS clients support HTTP authentication, Gepfabrol
embed a customer specific API key in the URL
(`http://servername/hashkey?...`). The module can check whether the
given key is allowed and block access otherwise.

You can enable this mechanism if you set `USE_KEY_DATABASE` when
compiling mod_mapnik_wms. You will then need to add one or more WmsKey 
directives to your Apache configuration (more conveniently, a file included
from there) to specify the allowed keys.

With `USE_KEY_DATABASE` the following additional configuration options are available:

| Option | Default | Use |
|--------|---------|-----|
| `WmsKey` | unset | Takes two parameters, an API key and a map type string. The map type string is currently ignored except if it is `demo` which has a special meaning, see below. If at least one WmsKey option is present, all incoming requests are checked for a valid API key. The API key must be the path component following `WmsPrefix`, or must be the first path component if `WmsPrefix` is unset. |
| `WmsMaxDemoWidth`, `WmsMaxDemoHeight` | unset | Maximum image width and height for requests with an API key that has the "demo" map type configured. Only available if compiled with `USE_KEY_DATABASE`. |
| `WmsKeySrsDef` | unset | Can appear multiple times. Takes three arguments - an API key, an SRS name (like EPSG:1234) and a PROJ init string that should be used to initialize this particular projection for this particular client. This allows for a per-client override of projection parameters. |

## Testing

You should be able to test your server with any WMS client, e.g.
OpenLayer or QGIS. However, a simple check can be performed
directly in the browser:

    http://servername/?LAYERS=&FORMAT=image%2Fjpeg&SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&EXCEPTIONS=application%2Fvnd.ogc.se_inimage&SRS=EPSG%3A4326&BBOX=-58.0078125,-13.359375,76.2890625,85.78125&WIDTH=382&HEIGHT=282

This should bring up a small map image with Europe and Northern Africa
in view.
