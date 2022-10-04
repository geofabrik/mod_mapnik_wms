/**
 * Mapnik WMS module for Apache
 *
 * This is the C++ code where Mapnik is actually called to do something.
 *
 */

#define BOOST_SPIRIT_THREADSAFE

/* for jpeg compression */
#define BUFFER_SIZE 4096
#define QUALITY 90

extern "C"
{
#include <gd.h>
#include <gdfonts.h>
#include <httpd.h>
#include <http_log.h>
#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mapnik_wms);
#endif 
#include <http_protocol.h>
#include <apr_strings.h>
#include <apr_pools.h>
}

#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/font_engine_freetype.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/version.hpp>
#if MAPNIK_VERSION >= 300000
#define image_data_32 image_rgba8
#define image_32 image_rgba8
#include <mapnik/image.hpp>
#include <mapnik/image_view_any.hpp>
#else
#include <mapnik/graphics.hpp>
#endif
#include <mapnik/layer.hpp>
#include <mapnik/expression.hpp>
#include <mapnik/color_factory.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/image_scaling.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/octree.hpp>

#include "wms.h"

#include <iostream>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "logbuffer.h"
#include "apachebuffer.h"


extern "C" 
{
struct wms_cfg *get_wms_cfg(request_rec *r);
bool load_configured_map(server_rec *s, struct wms_cfg *cfg)
{
    //cfg->mapnik_map = (mapnik::Map *) apr_pcalloc(p, sizeof(mapnik::Map));
    try 
    {
        cfg->mapnik_map = new mapnik::Map();
        load_map(*((mapnik::Map *)cfg->mapnik_map), cfg->map);
        ((mapnik::Map *) cfg->mapnik_map)->set_aspect_fix_mode(mapnik::Map::ADJUST_CANVAS_HEIGHT);
    }
    catch (const std::exception & ex)
    {
        ap_log_error(APLOG_MARK, APLOG_EMERG, 0, s,
             "error initializing map: %s.", ex.what());
        std::clog << "error loading map: " << ex.what() << std::endl;
        delete (mapnik::Map *) cfg->mapnik_map;
        cfg->mapnik_map = NULL;
        return false;
    }
    return true;
};

int wms_initialize(server_rec *s, struct wms_cfg *cfg, apr_pool_t *p)
{
    std::cerr << "> " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ")" << std::endl;

    if (!cfg->active)
    {
        return OK;
    }

    if (!cfg->datasource_count)
    {
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") Internal Server Error" << std::endl;
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (!cfg->font_count)
    {
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") Internal Server Error" << std::endl;
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    for (int i=0; i<cfg->datasource_count; i++)
        mapnik::datasource_cache::instance().register_datasources(cfg->datasource[i]);

    for (int i=0; i<cfg->font_count; i++)
        mapnik::freetype_engine::register_font(cfg->font[i]);

    if (!cfg->map)
    {
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") Internal Server Error" << std::endl;
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (!cfg->url)
    {
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") Internal Server Error" << std::endl;
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    if (!cfg->title)
    {
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") Internal Server Error" << std::endl;
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    cfg->initialized = 1;

    if (cfg->debug)
    {
        // will create map later
        std::clog << "debug mode, load map file later" << std::endl;
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") OK" << std::endl;
        return OK;
    }

    if (!load_configured_map(s, cfg))
    {
        std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") Internal Server Error" << std::endl;
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    std::cerr << "< " << __FILE__ << ":" << __LINE__ << " wms_initialize (" << getpid() << ") OK" << std::endl;
    return OK;
}
} /* extern C */

/**
 * Used as a data sink for libgd functions. Sends PNG data to Apache.
 */
static int gd_png_sink(void *ctx, const char *data, int length)
{
    request_rec *r = (request_rec *) ctx;
    return ap_rwrite(data, length, r);
}

/**
 * Decodes URI, overwrites original buffer.
 */
void decode_uri_inplace(char *uri)
{
    char *c, *d, code[3] = {0, 0, 0};

    for (c = uri, d = uri; *c; c++)
    {
        if ((*c=='%') && isxdigit(*(c+1)) && isxdigit(*(c+2)))
        {
            strncpy(code, c+1, 2); 
            *d++ = (char) strtol(code, 0, 16);
            c+=2;
        } 
        else 
        {
            *d++ = *c;
        }
    }
    *d = 0;
}

/**
 * Sends a HTTP error return code and logs the error.
 */
int http_error(request_rec *r, int code, const char *fmt, ...)
{
    va_list ap;
    char msg[1024]; 
    va_start(ap, fmt);
    vsnprintf(msg, 1023, fmt, ap);
    msg[1023]=0;
    std::clog << msg << std::endl;
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "%s", msg);
    ap_set_content_type(r, "text/plain");
    ap_rputs(msg, r);
    return code;
}

/**
 * Sends a WMS error message in the format requested by the client.
 * This is either an XML document, or an image. The HTTP return code
 * is 200 OK.
 */
int wms_error(request_rec *r, const char *code, const char *fmt, ...)
{
    va_list ap;
    char msg[1024]; 
    va_start(ap, fmt);
    vsnprintf(msg, 1023, fmt, ap);
    msg[1023]=0;
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "%s", msg);
    std::clog << msg << std::endl;

    char *args = r->args ? apr_pstrdup(r->pool, r->args) : 0;
    char *amp;
    char *current = args;
    char *equals;
    bool end = (args == 0);

    const char *exceptions = NULL;
    const char *width = 0;
    const char *height = 0;

    /* parse URL parameters into variables */
    while (!end)
    {
        amp = index(current, '&');
        if (amp == 0) { amp = current + strlen(current); end = true; }
        *amp = 0;
        equals = index(current, '=');
        if (equals > current)
        {
            *equals++ = 0;
            decode_uri_inplace(current);
            decode_uri_inplace(equals);

            if (!strcasecmp(current, "WIDTH")) width = equals;
            else if (!strcasecmp(current, "HEIGHT")) height = equals;
            else if (!strcasecmp(current, "EXCEPTIONS")) exceptions = equals;
            else if (!strcasecmp(current, "EXCEPTION")) exceptions = equals;
        }
        current = amp + 1;
    }

    if (!exceptions || !strcmp(exceptions, "application/vnd.ogc.se_xml") || !width || !height)
    {
        /* XML error message was requested. */
        ap_set_content_type(r, exceptions);
        ap_rprintf(r, "<?xml version='1.0' encoding='UTF-8' standalone='no' ?>\n"
            "<!DOCTYPE ServiceExceptionReport SYSTEM 'http://www.digitalearth.gov/wmt/xml/exception_1_1_0.dtd'>\n"
            "<ServiceExceptionReport version='1.1.0'>\n"
            "<ServiceException code='%s'>\n%s\n</ServiceException>\n"
            "</ServiceExceptionReport>", code, msg);
    }
    else if (!strcmp(exceptions, "application/vnd.ogc.se_inimage"))
    {
        /* Image error message was requested. We use libgd to create one. */
        int n_width = atoi(width);
        int n_height = atoi(height);
        if (n_width > 0 && n_width < 16384 && n_height > 0 && n_height < 16384)
        {
            gdImagePtr img = gdImageCreate(n_width, n_height);
            (void) gdImageColorAllocate(img, 255, 255, 255);
            int black = gdImageColorAllocate(img, 0, 0, 0);
            gdImageString(img, gdFontGetSmall(), 0, 0, (unsigned char *) msg, black);
            gdSink sink;
            sink.context = (void *) r;
            sink.sink = gd_png_sink;
            ap_set_content_type(r, "image/png");
            gdImagePngToSink(img, &sink);
        }
        else
        {
            return http_error(r, HTTP_INTERNAL_SERVER_ERROR, "Cannot satisfy requested exception type (%s)", exceptions);
        }
    }
    else if (!strcmp(exceptions, "application/vnd.ogc.se_blank"))
    {
        /* Empty image in error was requested. */
        int n_width = atoi(width);
        int n_height = atoi(height);
        if (n_width > 0 && n_width < 16384 && n_height > 0 && n_height <= 16384)
        {
            gdImagePtr img = gdImageCreate(n_width, n_height);
            gdImageColorAllocate(img, 255, 255, 255);
            gdSink sink;
            sink.context = (void *) r;
            sink.sink = gd_png_sink;
            ap_set_content_type(r, "image/png");
            gdImagePngToSink(img, &sink);
        }
        else
        {
            return http_error(r, HTTP_INTERNAL_SERVER_ERROR, "Cannot satisfy requested exception type (%s)", exceptions);
        }
    }
    else
    {
        return http_error(r, HTTP_INTERNAL_SERVER_ERROR, "Invalid exception type (%s)", exceptions);
    }
    return OK;
}

/**
 * Generates PNG and sends it to the client.
 * Based on PNG writer code from Mapnik. 
 */
void send_image_response(request_rec *r, mapnik::image_32 image, unsigned int height, std::string filetype)
{
 
    apachebuffer abuf(r);
    std::ostream str(&abuf);

    // requested size is perfect fit
    if (height == image.height())
    {
       mapnik::save_to_stream(image, str, filetype);
       return;
    }

    // frequent bug in clients: they request just a little too much of a bounding box,
    // yielding an image that is one scan line higher than requested. we chop it off
    if (height == image.height() -1)
    {
        mapnik::image_view<mapnik::image<mapnik::rgba8_t>> view(0, 0, 
            image.width(), height, image);
        mapnik::save_to_stream(view, str, filetype);
        return;
    }

    // all other cases: scale image
    mapnik::image_data_32 scaled_image(image.width(), height);
    mapnik::scale_image_agg<mapnik::image_data_32>(scaled_image, image, 
        mapnik::SCALING_BILINEAR,       // scaling method
        1.0,                            // x factor
        height * 1.0 / image.height(),  // y factor
        0.0,                            // x_off_f
        0.0,                            // y_off_f
        1.0,                            // filter_factor
        0.0);                           // nodata_value
    mapnik::save_to_stream(scaled_image, str, filetype);
    return;
}

/**
 * Creates a GetCapabilties response and sends it to the client.
 */
int wms_getcap(request_rec *r)
{
    struct wms_cfg *config = get_wms_cfg(r);

    std::string srs_list;
    std::string bbox_list;

    char buffer[1024];
    sprintf(buffer, "   <LatLonBoundingBox minx='%f' miny='%f' maxx='%f' maxy='%f' />\n", 
        config->minx, config->miny, config->maxx, config->maxy);
    bbox_list.append(buffer);
    sprintf(buffer, "   <BoundingBox SRS='EPSG:4326' minx='%f' miny='%f' maxx='%f' maxy='%f' />\n", 
        config->minx, config->miny, config->maxx, config->maxy);
    bbox_list.append(buffer);

    for (int i=0; i<config->srs_count; i++)
    {
        srs_list.append("    <SRS>");
        srs_list.append(config->srs[i]);
        srs_list.append("</SRS>\n");
    }

    ap_set_content_type(r, "application/vnd.ogc.wms_xml");
    ap_rprintf(r, 
        "<?xml version='1.0' encoding='UTF-8' standalone='no' ?>\n"
        "<!DOCTYPE WMT_MS_Capabilities SYSTEM 'http://schemas.opengis.net/wms/1.1.1/WMS_MS_Capabilities.dtd'\n"
        " [\n"
        " <!ELEMENT VendorSpecificCapabilities EMPTY>\n"
        " ]>  <!-- end of DOCTYPE declaration -->\n"
        "\n"
        "<WMT_MS_Capabilities version='1.1.1'>\n"
        "\n"
        "<Service>\n"
        "  <Name>OGC:WMS</Name>\n"
        "  <Title>%s</Title>\n"
        "  <OnlineResource xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='%s%s?'/>\n"
        "</Service>\n"
        "\n"
        "<Capability>\n"
        "  <Request>\n"
        "    <GetCapabilities>\n"
        "      <Format>application/vnd.ogc.wms_xml</Format>\n"
        "      <DCPType>\n"
        "        <HTTP>\n"
        "          <Get><OnlineResource xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='%s%s?'/></Get>\n"
        "        </HTTP>\n"
        "      </DCPType>\n"
        "    </GetCapabilities>\n"
        "    <GetMap>\n"
        "      <Format>image/png</Format>\n"
        "      <Format>image/png8</Format>\n"
        "      <Format>image/jpeg</Format>\n"
        "      <DCPType>\n"
        "        <HTTP>\n"
        "          <Get><OnlineResource xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='%s%s?'/></Get>\n"
        "        </HTTP>\n"
        "      </DCPType>\n"
        "    </GetMap>\n"
        "  </Request>\n"
        "  <Exception>\n"
        "    <Format>application/vnd.ogc.se_xml</Format>\n"
        "    <Format>application/vnd.ogc.se_inimage</Format>\n"
        "    <Format>application/vnd.ogc.se_blank</Format>\n"
        "  </Exception>\n"
        "  <UserDefinedSymbolization SupportSLD='0' UserLayer='0' UserStyle='0' RemoteWFS='0'/>\n", 
                config->title, config->url, r->uri, config->url, r->uri, config->url, r->uri);

    // FIXME more of this should be configurable.
    ap_rprintf(r, 
        "  <Layer>\n"
        "    <Name>%s</Name>\n"
        "    <Title>%s</Title>\n"
        "%s\n"
        "%s\n"
        "    <Attribution>\n"
        "        <Title>www.openstreetmap.org/CC-BY-SA2.0</Title>\n"
        "        <OnlineResource xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='http://www.openstreetmap.org/'/>\n"
        "    </Attribution>\n"
        "    <Layer queryable='0' opaque='1' cascaded='0'>\n"
        "        <Name>%s</Name>\n"
        "        <Title>%s</Title>\n"
        "        <Abstract>Full OSM Mapnik rendering.</Abstract>\n"
        "%s\n"
        "%s\n",
            config->top_layer_name, config->top_layer_title, srs_list.c_str(), bbox_list.c_str(), 
            config->top_layer_name, config->top_layer_title, srs_list.c_str(), bbox_list.c_str());

    if (config->include_sub_layers)
    {
        for (struct layer_group *lg = config->first_layer_group; lg; lg = lg->next)
        {
            ap_rprintf(r, 
            "        <Layer queryable='0' opaque='%d' cascaded='0'><Name>%s</Name><Title>%s</Title></Layer>\n", 
                lg->allow_transparency ? 1 : 0, lg->grouphandle, lg->groupname);
        }
    }

    ap_rprintf(r, 
        "    </Layer>\n"
        "  </Layer>\n"
        "</Capability>\n"
        "</WMT_MS_Capabilities>");
    return OK;
}

/**
 * Handles the GetMap request.
 */
int wms_getmap(request_rec *r)
{
    int rv = OK;
    struct wms_cfg *config = get_wms_cfg(r);

    const char *layers = 0;
    char *srs = 0;
    const char *bbox = 0;
    const char *width = 0;
    const char *height = 0;
    const char *styles = 0;
    const char *format = 0;
    const char *transparent = 0;
    const char *bgcolor = 0;
    const char *exceptions = "application/vnd.ogc.se_xml";

    char *args = r->args ? apr_pstrdup(r->pool, r->args) : 0;
    char *amp;
    char *current = args;
    char *equals;

    double scale = config->default_dpi / 90.0;
    int quality = 85;

    bool end = (args == 0);

    FILE *f = fopen(config->logfile, "a");
    std::streambuf *old = NULL;
    logbuffer *o = NULL;
    if (f)
    {
        o = new logbuffer(f);
        old = std::clog.rdbuf();
        std::clog.rdbuf(o);
    }

    /* 
     * in debug mode, the map is loaded/parsed for each request. that makes
     * it easier to make changes (no apache restart required)
     */

    if ((config->debug) && (!load_configured_map(r->server, config)))
    {
        return wms_error(r, "InvalidDimensionValue", "error parsing map file");
    }

    if (!config->mapnik_map)
    {
        return wms_error(r, "InvalidDimensionValue", "error parsing map file");
    }

    /* parse URL parameters into variables */
    while (!end)
    {
        amp = index(current, '&');
        if (amp == 0) { amp = current + strlen(current); end = true; }
        *amp = 0;
        equals = index(current, '=');
        if (equals > current)
        {
            *equals++ = 0;
            decode_uri_inplace(current);
            decode_uri_inplace(equals);

            if (!strcasecmp(current, "LAYERS")) layers = equals;
            else if (!strcasecmp(current, "SRS")) srs = equals;
            else if (!strcasecmp(current, "BBOX")) bbox = equals;
            else if (!strcasecmp(current, "WIDTH")) width = equals;
            else if (!strcasecmp(current, "HEIGHT")) height = equals;
            else if (!strcasecmp(current, "STYLES")) styles = equals;
            else if (!strcasecmp(current, "FORMAT")) format = equals;
            else if (!strcasecmp(current, "TRANSPARENT")) transparent = equals;
            else if (!strcasecmp(current, "BGCOLOR")) bgcolor = equals;
            else if (!strcasecmp(current, "EXCEPTIONS")) exceptions = equals;
            else if (!strcasecmp(current, "DPI"))
            {
                scale = atof(equals) / 90.0;
            }
            else if (!strcasecmp(current, "QUALITY"))
            {
                quality = atoi(equals);
            }
            else if (!strcasecmp(current, "format_options"))
            {
                const char *dpi = strcasestr(equals, "dpi:");
                if (dpi) scale = atof(dpi+4) / 90.0;
                const char *qual = strcasestr(equals, "quality:");
                if (qual) scale = atoi(qual+8);
            }
            else if (!strcasecmp(current, "map_resolution"))
            {
                scale = atof(equals) / 90.0;
            }
        }
        current = amp + 1;
    }

    if (!layers) return wms_error(r, "MissingDimensionValue", "required parameter 'layers' not set");
    if (!srs) return wms_error(r, "MissingDimensionValue", "required parameter 'srs' not set");
    if (!bbox) return wms_error(r, "MissingDimensionValue", "required parameter 'bbox' not set");
    if (!width) return wms_error(r, "MissingDimensionValue", "required parameter 'width' not set");
    if (!height) return wms_error(r, "MissingDimensionValue", "required parameter 'height' not set");
    if (!styles) return wms_error(r, "MissingDimensionValue", "required parameter 'styles' not set");
    if (!format) return wms_error(r, "MissingDimensionValue", "required parameter 'format' not set");

    std::clog << "layers parameter: '" << layers << "'" << std::endl;

    int n_width = atoi(width);
    int n_height = atoi(height);

    if ((config->max_width && n_width > config->max_width) || (n_width < config->min_width))
        return wms_error(r, "InvalidDimensionValue", 
            "requested width (%d) is not in range %d...%d", n_width, config->min_width, config->max_width);
    if ((config->max_height && n_height > config->max_height) || (n_height < config->min_height))
        return wms_error(r, "InvalidDimensionValue", 
            "requested height (%d) is not in range %d...%d", n_height, config->min_height, config->max_height);

    if (scale < 10.0/90.0 || scale > 1200.0/90.0)
    {
        return wms_error(r, "InvalidDimensionValue", "requested DPI is not in range 10...1200", n_height);
    }
    if (quality < 10 || quality > 100)
    {
        return wms_error(r, "InvalidDimensionValue", "requested quality is not in range 10...100", n_height);
    }

    double bboxvals[4];
    int bboxcnt = 0;
    char *dup = apr_pstrdup(r->pool, bbox);
    char *tok = strtok(dup, ",");
    while(tok)
    {
        if (bboxcnt<4) bboxvals[bboxcnt] = strtod(tok, NULL);
        bboxcnt++;
        tok = strtok(NULL, ",");
    }
    if (bboxcnt != 4)
    {
        return wms_error(r, "InvalidDimensionValue", "Invalid BBOX parameter ('%s'). Must contain four comma-separated values.", bbox);
    }

    /*
     * commented out due to client brokenness 
     *
    if (bboxvals[0] > bboxvals[2] ||
        bboxvals[1] > bboxvals[3] ||
        bboxvals[0] < -180 ||
        bboxvals[2] < -180 ||
        bboxvals[1] < -90 ||
        bboxvals[3] < -90 ||
        bboxvals[0] > 180 ||
        bboxvals[2] > 180 ||
        bboxvals[1] > 90 ||
        bboxvals[3] > 90)
    {
        return wms_error(r, "InvalidDimensionValue", "Invalid BBOX parameter ('%s'). Must describe an area on earth, with minlon,minlat,maxlon,maxlat", bbox);
    }
    */


    // split up layers into a proper C++ set for easy access
    std::set<std::string> layermap;
    dup = apr_pstrdup(r->pool, layers);
    char *saveptr1;
    const char *token = strtok_r(dup, ",", &saveptr1);
    bool transparency_allowed = false;
    while(token)
    {
        // if one of the layers requested is the "top" layer
        // then kill layer selection and return everything.
        if  (!strcmp(token, config->top_layer_name))
        {
            std::clog << "layermap clear, found top-level layer '" << token << "'" << std::endl;
            layermap.clear();
            break;
        }

        // if the layer refers to a layer group handle,
        // add all members of that layer group
        transparency_allowed = true;
	std::clog << "layermap handle token '" << token << "'" << std::endl;
        for (struct layer_group *lg = config->first_layer_group; lg; lg = lg->next)
        {
            if (!strcmp(token, lg->grouphandle))
            {
	        std::clog << "-> found group handle" << std::endl;
                char *saveptr2;
                char *dup2 = apr_pstrdup(r->pool, lg->sublayers);
                const char *token2 = strtok_r(dup2, ",", &saveptr2);
                while (token2)
                {
                    layermap.insert(token2);
		    std::clog << "   -> layermap insert" << token2 << std::endl;
                    token2 = strtok_r(NULL, ",", &saveptr2);
                }
                token = NULL;
                if (!lg->allow_transparency) transparency_allowed = false;
                break;
            }
        }

        // else add that layer verbatim.
        // if (token) layermap.insert(token);

        token = strtok_r(NULL, ",", &saveptr1);
    }
    std::clog << "layermap end processing" << std::endl;
    
    std::clog << "NEW REQUEST: " << r->the_request << std::endl;

    char *type;
    char *customer_id;
    char *user_key = NULL;

    if (config->prefix)
    {
        if (strncmp(r->uri, config->prefix, strlen(config->prefix)))
        {
            std::clog << "uri '" << r->uri << "' does not begin with configured prefix '" << config->prefix << "'" << std::endl;
            return http_error(r, HTTP_NOT_FOUND, "Not Found", exceptions);
        }
    }

#ifdef USE_KEY_DATABASE
    /*
     * See README for what the key database is about. It is basically
     * an access control scheme where clients have to give a certain
     * key in the URL to be granted access.
     */

    if (config->key_db)
    {
        user_key = apr_pstrdup(r->pool, r->uri + 1 + (config->prefix ? strlen(config->prefix) : 0));
        std::clog << "checking key '" << user_key << "' extracted from URI '" << r->uri << "'" << std::endl;
        char *delim = strpbrk(user_key, "/?");
        if (delim) 
        {
            *(delim) = 0;
        }

        struct wms_key *k = config->key_db;
        while(k)
        {
            if (!strcmp(k->key, user_key))
            {
                break;
            }
            k = k->next;
        }

        if (!k)
        {
            std::clog << "key " << user_key << " not configured" << std::endl;
            return http_error(r, HTTP_FORBIDDEN, "Key not known", exceptions);
        }

        if (k->demo)
        {
            if (config->max_demo_width && n_width > config->max_demo_width)
                return wms_error(r, "InvalidDimensionValue", 
                    "requested width (%d) is not in demo range 1...%d", n_width, config->max_demo_width);
            if (config->max_demo_height && n_height > config->max_demo_height)
                return wms_error(r, "InvalidDimensionValue", 
                    "requested height (%d) is not in demo range 1...%d", n_height, config->max_demo_height);
        }
    }
#endif

    /** check if given SRS is supported by configuration */
    char proj_srs_string[8192];
    proj_srs_string[0] = 0;
    for (char *i = srs; *i; i++) *i=tolower(*i);

    for (int i=0; i<config->srs_count; i++)
    {
        if (!strcasecmp(config->srs[i], srs))
        {
            sprintf(proj_srs_string, 
                "%s", // for older proj versions, needs "+init=%s"
                srs);
            break;
        }
    }

#ifdef USE_KEY_DATABASE
    char srscmp[256];
    if (user_key)
    {
        sprintf(srscmp, "%s/%s/", user_key, srs);

        for (int i=0; i<config->key_srs_def_count; i++)
        {
            if (!strncasecmp(config->key_srs_def[i], srscmp, strlen(srscmp)))
            {
                strcpy(proj_srs_string, config->key_srs_def[i] + strlen(srscmp));
                std::clog << "setting custom SRS for key " << user_key << " SRS " << srs << " to: " << proj_srs_string << std::endl;
            }
        }
    }
#endif

    if (!strlen(proj_srs_string))
    {
        return wms_error(r, "InvalidSRS", "The given SRS ('%s') is not supported by this WMS service.", srs);
    }

    using namespace mapnik;
    Map *mymap = (Map *)config->mapnik_map;

    /* If you have a flaky database connection you might want to set this to > 1. 
     * This is really a brute force way of handling problems. */
    int attempts = 1;

    while(attempts-- > 0)
    {
        try 
        {
            std::clog << "Configuring map parameters" << std::endl;
            mymap->set_srs(proj_srs_string);
            mymap->zoom_to_box(box2d<double>(bboxvals[0], bboxvals[1], bboxvals[2], bboxvals[3]));
            mymap->resize(n_width, n_height);
            // since the map object will be reused later, we cannot make persistent changes to it
            boost::optional<mapnik::color> oldbackground;

            if (transparent && !strcasecmp(transparent, "true") && transparency_allowed)
            {
                std::clog << "transparent" << std::endl;
                oldbackground = mymap->background();
                mymap->set_background(mapnik::color(0,0,0,0));
            }
            else if (bgcolor && strlen(bgcolor)>2)
            {
                std::clog << "bgcolor=" << bgcolor << std::endl;
                if (!strncmp("0x", bgcolor, 2))
                {
                    bgcolor += 2;
                }
                if (*bgcolor == '#')
                {
                    bgcolor++;
                }
                char buffer[16];
                snprintf(buffer, 16, "#%s", bgcolor);
                oldbackground = mymap->background();
                mymap->set_background(mapnik::color(buffer));
            }

            // make those layers that are not in the WMS "layers" 
            // parameter invisible 
            
            for (std::vector<mapnik::layer>::reverse_iterator i = mymap->layers().rbegin(); i != mymap->layers().rend(); i++)
            {
                if (layermap.empty())
		{
                   i->set_active(true);
		   std::clog << "layer enable " << i->name() << " (empty layermap)" << std::endl;
		}
		else if (layermap.find(i->name()) != layermap.end())
		{
                   i->set_active(true);
		   std::clog << "layer disable " << i->name() << " (present in layermap)" << std::endl;
		}
		else
		{
                   i->set_active(false);
		   std::clog << "layer disable " << i->name() << " (not in layermap)" << std::endl;
		}
            }

            image_32 buf(mymap->width(),mymap->height());
            agg_renderer<image_32> ren(*mymap, buf, scale, 0u, 0u);

            /*
             * broken clients will request a width/height that does not match, so this log line
             * is worth looking out for. we will fix this to return the right image but quality
             * suffers.
             */
            std::clog << "Start rendering (computed height is " << mymap->height() << ", requested is " << n_height << ")" << std::endl;
            ren.apply();
            std::clog << "Rendering complete" << std::endl;

            if (oldbackground) mymap->set_background(oldbackground.get());
            
            attempts = 0; // exit loop later

            if (!strcmp(format, "image/png"))
            {
                ap_set_content_type(r, "image/png");
                std::clog << "Start streaming PNG response" << std::endl;
                send_image_response(r, buf, n_height, "png32");
                std::clog << "PNG response complete" << std::endl;
            }
            else if (!strcmp(format, "image/png8"))
            {
                ap_set_content_type(r, "image/png");
                std::clog << "Start streaming PNG response (palette image)" << std::endl;
                send_image_response(r, buf, n_height, "png8");
                std::clog << "PNG response complete" << std::endl;
            }
            else if (!strcmp(format, "image/jpeg"))
            {
                char typespec[20];
                snprintf(typespec, 20, "jpeg%d", quality);
                ap_set_content_type(r, "image/jpeg");
                std::clog << "Start streaming JPEG response" << std::endl;
                send_image_response(r, buf, n_height, typespec);
                std::clog << "JPEG response complete" << std::endl;
            }
            else
            {
                rv = wms_error(r, "InvalidFormat", "Cannot deliver requested data format ('%s')", format);
            }
        }
/*
        catch ( const mapnik::config_error & ex )
        {
            rv = http_error(r, HTTP_INTERNAL_SERVER_ERROR, "mapnik config exception: %s", ex.what());
        }
*/
        catch ( const std::exception & ex )
        {
            rv = http_error(r, HTTP_INTERNAL_SERVER_ERROR, "standard exception (pid %d): %s", getpid(), ex.what());
        }
        catch ( ... )
        {
            rv = http_error(r, HTTP_INTERNAL_SERVER_ERROR, "other exception");
        }
        if (attempts) ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "re-trying...");
    }

    // reset clog stream
    if (old) 
    {
        std::clog.rdbuf(old);
        delete o;
        fclose(f);
    }

    if (config->debug) delete (mapnik::Map *) config->mapnik_map;

    return rv;
}

extern "C" 
{
    /**
     * Called by the Apache hook. Parses request and delegates to
     * proper method.
     */
    int wms_handle(request_rec *r)
    {
        char *args = r->args ? apr_pstrdup(r->pool, r->args) : 0;
        char *amp;
        char *current = args;
        char *equals;
        bool end = (args == 0);

        const char *request = 0;
        const char *service = 0;
        const char *version = 0;

        /* parse URL parameters into variables */
        while (!end)
        {
            amp = index(current, '&');
            if (amp == 0) { amp = current + strlen(current); end = true; }
            *amp = 0;
            equals = index(current, '=');
            if (equals > current)
            {
                *equals++ = 0;
                decode_uri_inplace(current);
                decode_uri_inplace(equals);

                if (!strcasecmp(current, "REQUEST")) request = equals;
                else if (!strcasecmp(current, "SERVICE")) service = equals;
                else if (!strcasecmp(current, "VERSION")) version = equals;
            }
            current = amp + 1;
        }

        if (!request)
        {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "%s", "REQUEST not set - declining");
            std::clog << "REQUEST not set - declining" << std::endl;
            return DECLINED;
        }
        else if (!strcmp(request, "GetMap"))
        {
            return wms_getmap(r); 
        }
        else if (!strcmp(request, "GetCapabilities"))
        {
            return wms_getcap(r);
        }
        else
        {
            return wms_error(r, "Request type '%s' is not supported.", request);
        }
    }
}
