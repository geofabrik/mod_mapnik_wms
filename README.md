# mod_mapnik_wms

**mod_mapnik_wms** is an Apache module for building a
[Mapnik](https://github.com/mapnik/mapnik) based 
[WMS](https://wiki.openstreetmap.org/wiki/WMS) server. It was initially 
written by Frederik Ramm for internal [Geofabrik](https://www.geofabrik.de/) 
use, but now is available under the GPL.

## What is WMS?

WMS (Web Map Service) is an OGC standard for serving maps. Essentially,
it specifies a set of HTTP URL parameters used by a client to retrieve a
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
currently only supports the bare minimum: The **GetCapabilities** call
which tells the client what this server can do, and the **GetMap** call
which produces a map. For rendering the map, mod_mapnik_wms uses the
Mapnik library.

## Building mod_mapnik_wms

To build mod_mapnik_wms, you need to have the following installed:

-   Mapnik (with PROJ support - some versions of Ubuntu have this 
    disabled and hence will not work)
-   Apache2 development package (apache2-dev and libapr1-dev)
-   libgd2 development package

You should be able to build a Debian package by just running "debuild",
but you can also choose to run "make". 

You may have to adjust some Mapnik version numbers (grep -i mapnik
debian/\*).

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
your `/etc/apache/sites_available` directory. An example configuration
is provided in `server_config.example`.

The mod_mapnik_wms-specific configuration options are:

<table>
<thead>
<tr class="header">
<th><p>Option</p></th>
<th><p>Default</p></th>
<th><p>Use</p></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p>MapnikLog</p></td>
<td><p>none</p></td>
<td><p>File to redirect the "clog" stream to.</p></td>
</tr>
<tr class="even">
<td><p>MapnikDatasources</p></td>
<td><p>none (required)</p></td>
<td><p>Path to Mapnik data source modules (plugins), usually <code>/usr/lib/mapnik/input</code>. May occur more than once.</p></td>
</tr>
<tr class="odd">
<td><p>MapnikFonts</p></td>
<td><p>none (required)</p></td>
<td><p>Path to one font file used in map files. May occur more than once. mod_mapnik_wms cannot recurse a font directory - each font has to be specified separately.</p></td>
</tr>
<tr class="even">
<td><p>MapnikMap</p></td>
<td><p>none (required)</p></td>
<td><p>Path to the map file (mapnik XML file). Currently only one is supported.</p></td>
</tr>
<tr class="odd">
<td><p>WmsTitle</p></td>
<td><p>unset</p></td>
<td><p>WMS server title you want to return for GetCapability requests.</p></td>
</tr>
<tr class="even">
<td><p>WmsUrl</p></td>
<td><p>unset</p></td>
<td><p>The URL under which your WMS server can be reached from the outside. It is used in constructing the GetCapabilities response. Note that clients will use this URL to access the WMS service even if they have retrieved the capabilities document through another channel, e.g. if you have a port forwarding set up and have your client connect to localhost:1234 to retrieve the capabilities document, it will only use localhost:1234 for the map request if this is actually specified in the WmsUrl.</p></td>
</tr>
<tr class="odd">
<td><p>WmsPrefix</p></td>
<td><p>unset</p></td>
<td><p>The mod_mapnik_wms module will typically handle each and every request sent to the web server that has a valid WMS request in its query string. If this option is used, then only request URIs beginning with this string will be considered.</p></td>
</tr>
<tr class="even">
<td><p>WmsDebug</p></td>
<td><p>false</p></td>
<td><p>If true, the map file will be loaded for each request instead of once at startup which makes fiddling with the style easier.</p></td>
</tr>
<tr class="odd">
<td><p>WmsSrs</p></td>
<td><p>none</p></td>
<td><p>A list of allowed SRS names, separated by spaces. They must be supported by the underlying Mapnik installation. You will usually want to have at least EPSG:4326 in that list.</p></td>
</tr>
<tr class="even">
<td><p>WmsExtentMinLon,<br />
WmsExtentMaxLon,<br />
WmsExtentMinLat,<br />
WmsExtentMaxLat</p></td>
<td><p>-179.9999, -89.999, 179.9999, 89.999</p></td>
<td><p>The data bounding box to be published in the capabilities document</p></td>
</tr>
<tr class="even">
<td><p>WmsMaxWidth,<br />
WmsMaxHeight,<br />
WmsMinWidth,<br />
WmsMinHeight</p></td>
<td><p>1 for min, none for max</p></td>
<td><p>The allowed image size range for WMS requests (in pixels)</p></td>
</tr>
<tr class="odd">
<td><p>WmsDefaultDpi</p></td>
<td><p>90</p></td>
<td><p>Default resolution (when not explicitly requested by the client).</p></td>
</tr>
</tbody>
</table>

## Map Style

Built on Mapnik, mod_mapnik_wms supports any map style that Mapnik supports. 
Frequently you will create a style using CartoCSS and then compile that to 
Mapnik XML with a tool like `magnacarto` or `carto`. 

Note that while mod_mapnik_wms can process any tile server stile as-is, 
such styles will typically exhibit poor performance on low zoom levels (i.e.
large map scales) because tile servers are not optimised for that - they
can simply pre-render the map on these scales.

## Layers

WMS supports layers. The server can announce to the client which layers
it supports, and the client can make a selection from them. 

The following options control if and how layers are exposed to the client:

<table>
<thead>
<tr class="header">
<th><p>Option</p></th>
<th><p>Default</p></th>
<th><p>Use</p></th>
</tr>
</thead>
<tbody>
<tr class="even">
<td><p>WmsTopLayerTitle</p></td>
<td><p>OpenStreetMap WMS</p></td>
<td><p>WMS top layer title published in the GetCapabilities response.</p></td>
</tr>
<tr class="odd">
<td><p>WmsTopLayerName</p></td>
<td><p>OpenStreetMap WMS</p></td>
<td><p>WMS top layer name published in the GetCapabilities response.</p></td>
</tr>
<tr class="even">
<td><p>WmsIncludeSubLayers</p></td>
<td><p>false</p></td>
<td><p>Expose individual layers to the WMS client. Must be used in conjunction with one or more WmsLayerGroup options.</p></td>
</tr>
<tr class="odd">
<td><p>WmsLayerGroup</p></td>
<td><p>unset</p></td>
<td><p>This option groups a number of Mapnik layers into one named WMS layer. It can appear multiple times and takes four arguments:<ol><li>WMS layer name</li><li>WMS layer description</li><li>1 if the layer should be drawn when transparency is requested by the client, 0 if not</li><li>comma-separated list of Mapnik layers that should be drawn when this WMS layer is requested</li></ol></p></td>
</tr>
</tbody>
</table>

## API keys

This module is used by Geofabrik to serve WMS content to paying
customers. Since not all WMS clients support HTTP authentication, Gepfabrol
embed a customer specific API key in the URL
(http://servername//hashkey?...). The module can check whether the
given key is allowed and block access otherwise.

You can enable this mechanism if you set USE_KEY_DATABASE when
compiling mod_mapnik_wms. You will then need to add one or more WmsKey 
directives to your Apache configuration (more conveniently, a file included
from there) to specify the allowed keys.

With USE_KEY_DATABASE the following additional configuration options are available:

<table>
<thead>
<tr class="header">
<th><p>Option</p></th>
<th><p>Default</p></th>
<th><p>Use</p></th>
</tr>
</thead>
<tbody>
<tr class="even">
<td><p>WmsKey</p></td>
<td><p>unset</p></td>
<td><p>Takes two parameters, an API key and a map type string. The map type string is currently ignored except if it is "demo" which has a special meaning, see below. If at least one WmsKey option is present, all incoming requests are checked for a valid API key. The API key must be the path component following WmsPrefix, or must be the first path component if WmsPrefix is unset.
</tr>
<tr class="odd">
<td><p>WmsMaxDemoWidth, WmsMaxDemoHeight</p></td>
<td><p>unset</p></td>
<td><p>Maximum image width and height for requests with an API key that has the "demo" map type configured. Only available if compiled with USE_KEY_DATABASE.</p></td>
</tr>
<tr class="even">
<td><p>WmsKeySrsDef</p></td>
<td><p>unset</p></td>
<td><p>Can appear multiple times. Takes three arguments - an API key, an SRS name (like EPSG:1234) and a PROJ init string that should be used to initialize this particular projection for this particular client. This allows for a per-client override of projection parameters.</p></td>
</tr>
</tbody>
</table>

## Testing

You should be able to test your server with any WMS client, e.g.
OpenLayer or QGIS. However, a simple check can be performed
directly in the browser:

    http://servername/?LAYERS=&FORMAT=image%2Fjpeg&SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&EXCEPTIONS=application%2Fvnd.ogc.se_inimage&SRS=EPSG%3A4326&BBOX=-58.0078125,-13.359375,76.2890625,85.78125&WIDTH=382&HEIGHT=282

This should bring up a small map image with Europe and Northern Africa
in view.
