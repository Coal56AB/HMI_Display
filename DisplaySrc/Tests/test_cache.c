#include "hmi.h"
#include "hmi_ui.h"
#include "storage_fixture.h"
#include <time.h>
static unsigned writes;
static void discard(uint16_t x,uint16_t y,uint16_t w,uint16_t h,
                    const uint16_t *p,uint16_t stride,void *user){
    (void)x;(void)y;(void)w;(void)h;(void)p;(void)stride;(void)user;
    writes++;
}
int main(void){
    HmiState state;clock_t start;
    fixture_init();hmi_init();hmi_state_defaults(&state);
    hmi_storage_loads=0;start=clock();
    hmi_render_full(&state,discard,0);
    printf("home: %lu loads, %.3f host seconds\n",(unsigned long)hmi_storage_loads,
           (double)(clock()-start)/CLOCKS_PER_SEC);
#if HMI_LITE
    assert(hmi_storage_loads<600); /* Previously 10683 decompressions. */
#else
    assert(hmi_storage_loads<30000); /* Procedural reference path, not the production UI. */
#endif
    /* Re-mount must invalidate both resource and decoded-scene caches. */
    fixture_init();hmi_storage_loads=0;
    hmi_render_full(&state,discard,0);
    assert(hmi_storage_loads>0);
    {
        HmiState next=state;next.clock="14:33";
        hmi_storage_loads=0;hmi_diff_and_invalidate(&state,&next);
        hmi_render_dirty(&next,discard,0);
        printf("clock: %lu loads\n",(unsigned long)hmi_storage_loads);
    }
    {
        HmiUi ui;fixture_init();hmi_ui_init(&ui,(HmiDisplay){discard,0});
        hmi_storage_loads=0;writes=0;start=clock();hmi_ui_render(&ui);
        printf("UI home including labels: %lu loads, %.3f host seconds\n",
               (unsigned long)hmi_storage_loads,(double)(clock()-start)/CLOCKS_PER_SEC);
#if HMI_EXTERNAL_ASSETS
        assert(hmi_storage_loads<1000); /* Ready raster: about 816 reads, formerly 2424. */
        assert(writes==60);
#else
        assert(hmi_storage_loads<550);
#endif
        writes=0;hmi_storage_loads=0;hmi_ui_render(&ui);
        assert(!writes&&!hmi_storage_loads);
    }
    return 0;
}
