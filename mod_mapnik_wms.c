/**
 * Mapnik WMS module for Apache
 *
 * This is the module code, in pure C. There's no functionality here,
 * instead we just make calls into the C++ code from wms.cpp.
 *
 * part of the Mapnik WMS server module for Apache
 */
#include "apr.h"
#include "apr_strings.h"
#include "apr_thread_proc.h"
#include "apr_optional.h"
#include "apr_buckets.h"
#include "apr_lib.h"
#include "apr_poll.h"

#define APR_WANT_STRFUNC
#define APR_WANT_MEMFUNC
#include "apr_want.h"

#include "util_filter.h"
#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_main.h"
#include "http_log.h"
#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(mapnik_wms);
#endif
#include "util_script.h"
#include "ap_mpm.h"
#include "mod_core.h"
#include "mod_cgi.h"
#include "wms.h"

module AP_MODULE_DECLARE_DATA mapnik_wms_module;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <assert.h>

//pthread_mutex_t planet_lock = PTHREAD_MUTEX_INITIALIZER;

struct wms_cfg *get_wms_cfg(request_rec *r)
{
    // fprintf(stderr, "%s:%d get_wms_cfg (%d)\n", __FILE__, __LINE__, getpid());
    return (struct wms_cfg *) ap_get_module_config(r->server->module_config, &mapnik_wms_module);
}

static int wms_handler(request_rec *r)
{
    // fprintf(stderr, "> %s:%d wms_handler (%d)\n", __FILE__, __LINE__, getpid());
    
    /*
    if (strcmp(r->handler, "wms-handler"))
    {
        fprintf(stderr, "< %s:%d wms_handler (%d) DECLINED\n", __FILE__, __LINE__, getpid());
        return DECLINED;
    }
    */

    if (!get_wms_cfg(r)->active) return DECLINED;

    /* We set the content type before doing anything else */
    ap_set_content_type(r, "text/html");

    /* If the request is for a header only, and not a request for
     * the whole content, then return OK now. We don't have to do
     * anything else. */
    if (r->header_only) 
    {
         // fprintf(stderr, "< %s:%d wms_handler (%d) OK\n", __FILE__, __LINE__, getpid());
         return OK;
    }


    int rr = wms_handle(r);
    // fprintf(stderr, "< %s:%d wms_handler (%d)\n", __FILE__, __LINE__, getpid());
    return rr;
}

static void child_init(apr_pool_t *p, server_rec *s)
{
    // fprintf(stderr, "> %s:%d child_init (%d, %s)\n", __FILE__, __LINE__, getpid(), (s) ? s->server_admin : "NIL");
    while(s)
    {
        // fprintf(stderr, ": %s:%d child_init (%d, %s)\n", __FILE__, __LINE__, getpid(), (s) ? s->server_admin : "NIL");
        struct wms_cfg *cfg = ap_get_module_config(s->module_config, &mapnik_wms_module);
        if (!cfg->initialized)
            wms_initialize(s, cfg, NULL);
        s=s->next;
    }
    // fprintf(stderr, "< %s:%d child_init (%d, %s)\n", __FILE__, __LINE__, getpid(), (s) ? s->server_admin : "NIL");
    fflush(stderr);
}

// WmsSrs option - defines the SRS that are supported by this server
static const char *handle_srs_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    if (cfg->srs_count >= CAPACITY_CONFIG_SRS)
    {
        fprintf(stderr, ": %s:%d handle_srs_option: cannot add more SRS, compiled-in limit is %d\n",  
           __FILE__, __LINE__, CAPACITY_CONFIG_SRS);
        return NULL;
    }
    cfg->srs[cfg->srs_count++] = word;
    return NULL;
}

// WmsSrsDef option - re-defines SRS 
static const char *handle_srs_def_option(cmd_parms *cmd, void *mconfig, const char *srs, const char *def)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    if (cfg->srs_def_count >= CAPACITY_CONFIG_SRS_DEF)
    {
        fprintf(stderr, ": %s:%d handle_srs_option: cannot add more SRS definitions, compiled-in limit is %d\n",  
           __FILE__, __LINE__, CAPACITY_CONFIG_SRS_DEF);
        return NULL;
    }
    char *x = (char *) apr_pcalloc(cfg->pool, strlen(srs)+strlen(def)+3);
    sprintf(x, "%s/%s", srs, def);
    cfg->srs_def[cfg->srs_def_count++] = x;
    return NULL;
}

// WmsKeySrsDef option - re-defines specific SRS for specific API key
static const char *handle_key_srs_def_option(cmd_parms *cmd, void *mconfig, const char *key, const char *srs, const char *def)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    if (cfg->key_srs_def_count >= CAPACITY_CONFIG_KEY_SRS_DEF)
    {
        fprintf(stderr, ": %s:%d handle_key_srs_def_option: cannot add more key-specific SRS defintions, compiled-in limit is %d\n",  
           __FILE__, __LINE__, CAPACITY_CONFIG_KEY_SRS_DEF);
        return NULL;
    }
    char *x = (char *) apr_pcalloc(cfg->pool, strlen(key)+strlen(srs)+strlen(def)+3);
    sprintf(x, "%s/%s/%s", key, srs, def);
    cfg->key_srs_def[cfg->key_srs_def_count++] = x;
    return NULL;
}
static const char *handle_log_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->logfile = word;
    return NULL;
}
static const char *handle_datasource_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    if (cfg->datasource_count >= CAPACITY_CONFIG_DATASOURCE)
    {
        fprintf(stderr, ": %s:%d handle_datasource_option: cannot add more datasources, compiled-in limit is %d\n",  
           __FILE__, __LINE__, CAPACITY_CONFIG_DATASOURCE);
        return NULL;
    }
    cfg->datasource[cfg->datasource_count++] = word;
    return NULL;
}
static const char *handle_font_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    if (cfg->font_count >= CAPACITY_CONFIG_FONT)
    {
        fprintf(stderr, ": %s:%d handle_font_option: cannot add more fonts, compiled-in limit is %d\n",  
           __FILE__, __LINE__, CAPACITY_CONFIG_FONT);
        return NULL;
    }
    cfg->font[cfg->font_count++] = word;
    return NULL;
}
static const char *handle_map_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->map = word;
    return NULL;
}
static const char *handle_title_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->title = word;
    return NULL;
}
static const char *handle_top_layer_title_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->top_layer_title = word;
    return NULL;
}
static const char *handle_top_layer_name_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->top_layer_name = word;
    return NULL;
}
static const char *handle_layer_group_option(cmd_parms *cmd, void *mconfig, const char *args)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    struct layer_group *lg = (struct layer_group *) apr_pcalloc(cfg->pool, sizeof(struct layer_group));
    lg->next = cfg->first_layer_group;
    const char *word = ap_getword_conf(cfg->pool, &args);
    lg->grouphandle = word;
    word = ap_getword_conf(cfg->pool, &args);
    lg->groupname = word;
    word = ap_getword_conf(cfg->pool, &args);
    lg->allow_transparency = (!strcasecmp(word, "1") || !strcasecmp(word, "true"));
    word = ap_getword_conf(cfg->pool, &args);
    lg->sublayers = word;
    cfg->first_layer_group = lg;
    return NULL;
}
static const char *handle_module_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->active = !strcasecmp(word, "on");
    return NULL;
}
static const char *handle_sub_layer_option(cmd_parms *cmd, void *mconfig, int yesno)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->include_sub_layers = yesno;
    return NULL;
}
static const char *handle_debug_option(cmd_parms *cmd, void *mconfig, int yesno)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->debug = yesno;
    return NULL;
}
static const char *handle_url_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->url = word;
    return NULL;
}
static const char *handle_prefix_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->prefix = word;
    return NULL;
}
#ifdef USE_KEY_DATABASE
static const char *handle_key_option(cmd_parms *cmd, void *mconfig, const char *mapkey, const char *maptype)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    struct wms_key *next = cfg->key_db;
    struct wms_key *this = apr_pcalloc(cfg->pool, sizeof(struct wms_key));
    this->key = apr_pstrdup(cfg->pool, mapkey);
    this->demo = !strcmp(maptype, "demo");
    this->next = next;
    cfg->key_db = this;
    return NULL;
}
static const char *handle_mdw_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->max_demo_width = atoi(word);
    return NULL;
}
static const char *handle_mdh_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->max_demo_height = atoi(word);
    return NULL;
}
#endif
static const char *handle_maw_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->max_width = atoi(word);
    return NULL;
}
static const char *handle_mah_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->max_height = atoi(word);
    return NULL;
}
static const char *handle_miw_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->min_width = atoi(word);
    return NULL;
}
static const char *handle_mih_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->min_height = atoi(word);
    return NULL;
}
static const char *handle_minlat_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->miny = atof(word);
    return NULL;
}
static const char *handle_minlon_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->minx = atof(word);
    return NULL;
}
static const char *handle_maxlat_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->maxy = atof(word);
    return NULL;
}
static const char *handle_maxlon_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->maxx = atof(word);
    return NULL;
}
static const char *handle_default_dpi_option(cmd_parms *cmd, void *mconfig, const char *word)
{
    // fprintf(stderr, ": %s:%d handle_option (%d, %p)\n", __FILE__, __LINE__, getpid(), cmd->server);
    struct wms_cfg *cfg = ap_get_module_config(cmd->server->module_config, &mapnik_wms_module);
    cfg->default_dpi = atof(word);
    return NULL;
}
static int wms_post_config(apr_pool_t *pconf, apr_pool_t *plog,
   apr_pool_t *ptemp, server_rec *s)
{

    void *data;
    const char *userdata_key = "mod_wms_init_module";
    // fprintf(stderr, "> %s:%d wms_post_config (%d)\n", __FILE__, __LINE__, getpid());

    apr_pool_userdata_get(&data, userdata_key, s->process->pool);

    if (!data)
    {
        apr_pool_userdata_set((const void *) 1, userdata_key,
             apr_pool_cleanup_null, s->process->pool);
        // fprintf(stderr, "< %s:%d wms_post_config (%d) first call ignore\n", __FILE__, __LINE__, getpid());
        return OK;
    }

    // fprintf(stderr, "< %s:%d wms_post_config (%d)\n", __FILE__, __LINE__, getpid());
    return OK;
}

static void register_hooks(__attribute__((unused)) apr_pool_t *p)
{
    ap_hook_child_init(child_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config(wms_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(wms_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

static const command_rec wms_options[] =
{
    AP_INIT_ITERATE(
        "WmsSrs",
        handle_srs_option,
        NULL,
        RSRC_CONF,
        "WmsSrs takes a list of allowed SRS names for its argument. They must be supported by the underlying Mapnik installation."
    ),
    AP_INIT_ITERATE(
        "MapnikDatasources",
        handle_datasource_option,
        NULL,
        RSRC_CONF,
        "MapnikDatasources is the path to Mapnik data source modules."
    ),
    AP_INIT_ITERATE(
        "MapnikFonts",
        handle_font_option,
        NULL,
        RSRC_CONF,
        "MapnikFonts takes a list of ttf files to make availalbe to Mapnik."
    ),
    AP_INIT_TAKE1(
        "MapnikMap",
        handle_map_option,
        NULL,
        RSRC_CONF,
        "MapnikMap is the path to the map file."
    ),
    AP_INIT_TAKE1(
        "WmsTitle",
        handle_title_option,
        NULL,
        RSRC_CONF,
        "WmsTitle is the title for your WMS server you want to return for GetCapability requests."
    ),
    AP_INIT_TAKE1(
        "WmsModule",
        handle_module_option,
        NULL,
        RSRC_CONF,
        "WmsModule must be set to 'On' for WMS services to function."
    ),
    AP_INIT_TAKE1(
        "WmsTopLayerTitle",
        handle_top_layer_title_option,
        NULL,
        RSRC_CONF,
        "WmsTopLayerTitle is the title for the top-level layer."
    ),
    AP_INIT_TAKE1(
        "WmsTopLayerName",
        handle_top_layer_name_option,
        NULL,
        RSRC_CONF,
        "WmsTopLayerName is the name for the top-level layer."
    ),
    AP_INIT_FLAG(
        "WmsIncludeSubLayers",
        handle_sub_layer_option,
        NULL,
        RSRC_CONF,
        "If WmsIncludeSubLayers is given, Mapnik's sub layers will be exposed."
    ),
    AP_INIT_RAW_ARGS(
        "WmsLayerGroup",
        handle_layer_group_option,
        NULL,
        RSRC_CONF,
        "WmsLayerGroup may be used to encapsulate groups of Mapnik layers into WMS layer names."
    ),
    AP_INIT_FLAG(
        "WmsDebug",
        handle_debug_option,
        NULL,
        RSRC_CONF,
        "If WmsDebug is set, the map file will be loaded for each request instead of once at startup."
    ),
    AP_INIT_TAKE1(
        "MapnikLog",
        handle_log_option,
        NULL,
        RSRC_CONF,
        "MapnikLog is the name of the log file to write Mapnik debug output to."
    ),
    AP_INIT_TAKE1(
        "WmsUrl",
        handle_url_option,
        NULL,
        RSRC_CONF,
        "WmsUrl is the URL under which your WMS server can be reached from the outside. It is used in constructing the GetCapabilities response."
    ),
    AP_INIT_TAKE1(
        "WmsPrefix",
        handle_prefix_option,
        NULL,
        RSRC_CONF,
        "WmsPrefix is the path component that WMS requests must begin with. The default is an empty string."
    ),
#ifdef USE_KEY_DATABASE
    AP_INIT_TAKE2(
        "WmsKey",
        handle_key_option,
        NULL,
        RSRC_CONF,
        "WmsKey takes two arguments - key and map type. If at least one WmsKey is given, then only requests that have one of the given keys will be allowed. The map type can be any string, and if it is 'demo', map size limits apply."
    ),
    AP_INIT_TAKE1(
        "WmsMaxDemoWidth",
        handle_mdw_option,
        NULL,
        RSRC_CONF,
        "WmsMaxDemoWidth is the maximum image width served for demo accounts."
    ),
    AP_INIT_TAKE1(
        "WmsMaxDemoHeight",
        handle_mdh_option,
        NULL,
        RSRC_CONF,
        "WmsMaxDemoHeight is the maximum image height served for demo accounts."
    ),
    AP_INIT_TAKE3(
        "WmsKeySrsDef",
        handle_key_srs_def_option,
        NULL,
        RSRC_CONF,
        "WmsKeySrsDef takes a key and a SRS name (e.g. EPSG:1234) and assigns a PROJ init string to it."
    ),
#endif
    AP_INIT_TAKE2(
        "WmsSrsDef",
        handle_srs_def_option,
        NULL,
        RSRC_CONF,
        "WmsSrsDef takes a SRS name (e.g. EPSG:1234) and assigns a PROJ init string to it."
    ),
    AP_INIT_TAKE1(
        "WmsMaxWidth",
        handle_maw_option,
        NULL,
        RSRC_CONF,
        "WmsMaxWidth is the maximum image width."
    ),
    AP_INIT_TAKE1(
        "WmsMaxHeight",
        handle_mah_option,
        NULL,
        RSRC_CONF,
        "WmsMaxHeight is the maximum image height."
    ),
    AP_INIT_TAKE1(
        "WmsMinWidth",
        handle_miw_option,
        NULL,
        RSRC_CONF,
        "WmsMinWidth is the minimum image width (defaults to 1)."
    ),
    AP_INIT_TAKE1(
        "WmsMinHeight",
        handle_mih_option,
        NULL,
        RSRC_CONF,
        "WmsMinHeight is the minimum image height (defaults to 1)."
    ),
    AP_INIT_TAKE1(
        "WmsExtentMinLon",
        handle_minlon_option,
        NULL,
        RSRC_CONF,
        "WmsExtentMinLon is the minimum longitude of data"
    ),
    AP_INIT_TAKE1(
        "WmsExtentMaxLon",
        handle_maxlon_option,
        NULL,
        RSRC_CONF,
        "WmsExtentMaxLon is the maximum longitude of data"
    ),
    AP_INIT_TAKE1(
        "WmsExtentMinLat",
        handle_minlat_option,
        NULL,
        RSRC_CONF,
        "WmsExtentMinLat is the minimum latitude of data"
    ),
    AP_INIT_TAKE1(
        "WmsExtentMaxLat",
        handle_maxlat_option,
        NULL,
        RSRC_CONF,
        "WmsExtentMaxLat is the maximum latitude of data"
    ),
    AP_INIT_TAKE1(
        "WmsDefaultDpi",
        handle_default_dpi_option,
        NULL,
        RSRC_CONF,
        "WmsDefaultDpi is the standard resolution"
    ),
    {NULL}
};

static void *create_wms_conf(apr_pool_t *p, server_rec *s)
{
    struct wms_cfg *newcfg;
    // fprintf(stderr, "> %s:%d create_wms_conf (%d)\n", __FILE__, __LINE__, getpid());

    // Allocate memory from the provided pool.
    newcfg = (struct wms_cfg *) apr_pcalloc(p, sizeof(struct wms_cfg));

    newcfg->pool = p;
    newcfg->srs_count = 0;
    newcfg->srs_def_count = 0;
    newcfg->key_srs_def_count = 0;
    newcfg->font_count = 0;
    newcfg->datasource_count = 0;
    newcfg->title = 0;
    newcfg->url = 0;
    newcfg->prefix = 0;
    newcfg->map = 0;
    newcfg->initialized = 0;
    newcfg->top_layer_name = "OpenStreetMap WMS";
    newcfg->top_layer_title = "OpenStreetMap WMS";
    newcfg->include_sub_layers = 0;
    newcfg->key_db = 0;
    newcfg->max_demo_width = 0;
    newcfg->max_demo_height = 0;
    newcfg->debug = 0;
    newcfg->minx = -179.9999;
    newcfg->maxx = 179.9999;
    newcfg->miny = -89.9999;
    newcfg->maxy = 89.9999;
    newcfg->default_dpi = 90.0;
    newcfg->active = 0;
    newcfg->first_layer_group = 0;
    newcfg->min_width = 1;
    newcfg->min_height = 1;

    // Return the created configuration struct.
    // fprintf(stderr, "< %s:%d create_wms_conf (%d)\n", __FILE__, __LINE__, getpid());
    return (void *) newcfg;
}

module AP_MODULE_DECLARE_DATA mapnik_wms_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,           /* dir config creater */
    NULL,           /* dir merger --- default is to override */
    create_wms_conf,/* server config */
    NULL,           /* merge server config */
    wms_options,    /* command apr_table_t */
    register_hooks  /* register hooks */
};

