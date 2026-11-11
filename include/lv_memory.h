#pragma once
#include <stddef.h>
#include <esp_heap_caps.h>
static inline void *lv_psram_alloc(size_t n) { return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
static inline void *lv_psram_realloc(void *p,size_t n) { return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
