#ifndef BLACKHAT_CJAG_H
#define BLACKHAT_CJAG_H

#include "util/watchdog.h"

typedef struct _cjag_config_t {
    int send;
    int color_output;
    int channels;
    int cache_size;
    int cache_ways;
    int cache_slices;
    int cache_miss_threshold;
    int cache_kill_count;
    int cache_probe_count;
    int jag_send_count;
    int jag_recv_count;
    int set_offset;
    int timeout;

    watchdog_t *watchdog;
    void *addresses;    /// holds the 2mb pages
    volatile void **cache_sets; /// holds the eviction-sets (32 per slice, we will use the first 19 to find channels.)
    int n_addr_per_set;
    int n_pages;
    void **addr;    ///  a place to hold the channels eviction-sets 
} cjag_config_t;


void show_welcome(cjag_config_t *config);

void show_parameters(cjag_config_t *config);

void show_usage(char *binary, cjag_config_t *config);

void send_callback(cjag_config_t *config, int set);

void probe_callback(cjag_config_t *config, int set);

void receive_callback(cjag_config_t *config, int set);

void watch_callback(cjag_config_t *config, int set);

void timeout(void *arg);

void print_eviction_sets(void **addr, cjag_config_t *config);


#endif //BLACKHAT_CJAG_H
