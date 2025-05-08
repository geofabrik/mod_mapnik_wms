/**
 * header file for wms
 *
 * see comments in wms.cpp
 */

#include <httpd.h>


#ifndef WMS_H_INCLUDED
#define WMS_H_INCLUDED
struct layer_group;

struct layer_group {
    const char *grouphandle;
    const char *groupname;
    const char *sublayers;
    int allow_transparency;
    struct layer_group *next;
};

struct wms_key {
    const char *key;
    int demo;
    struct wms_key *next;
};

struct wms_cfg {
    int active;
    int srs_count;
    #define CAPACITY_CONFIG_SRS 256
    const char *srs[CAPACITY_CONFIG_SRS];
    int srs_def_count;
    #define CAPACITY_CONFIG_SRS_DEF 256
    const char *srs_def[CAPACITY_CONFIG_SRS_DEF];
    int key_srs_def_count;
    #define CAPACITY_CONFIG_KEY_SRS_DEF 256
    const char *key_srs_def[CAPACITY_CONFIG_KEY_SRS_DEF];
    int datasource_count;
    #define CAPACITY_CONFIG_DATASOURCE 256
    const char *datasource[CAPACITY_CONFIG_DATASOURCE];
    int font_count;
    #define CAPACITY_CONFIG_FONT 256
    const char *font[CAPACITY_CONFIG_FONT];
    const char *title;
    const char *url;
    const char *prefix;
    const char *map;
    const char *top_layer_name;
    const char *top_layer_title;
    int include_sub_layers; 
    int initialized;
    void *mapnik_map;
    const char *logfile;
    struct wms_key *key_db;
    int max_width;
    int max_height;
    int max_demo_width;
    int max_demo_height;
    int min_width;
    int min_height;
    int debug;
    float minx;
    float maxx;
    float miny;
    float maxy;
    float default_dpi;
    struct layer_group *first_layer_group;
    apr_pool_t *pool;
};

#ifdef __cplusplus
extern "C"
{
#endif
int wms_getcap(request_rec *r);
int wms_handle(request_rec *r);
int wms_initialize(struct server_rec *s, struct wms_cfg *cfg, apr_pool_t *p);
#ifdef __cplusplus
}
#endif

#endif
