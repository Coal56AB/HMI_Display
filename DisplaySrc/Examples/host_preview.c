#include "hmi.h"

#include <stdio.h>
#include <string.h>

#define W 320u
#define H 480u
static uint16_t frame[W*H];

static void collect(uint16_t x,uint16_t y,uint16_t width,uint16_t height,
                    const uint16_t *pixels,uint16_t stride,void *user){
    uint16_t row;(void)user;
    for(row=0;row<height;row++)memcpy(&frame[(uint32_t)(y+row)*W+x],
        &pixels[(uint32_t)row*stride],(size_t)width*sizeof(uint16_t));
}

static int write_ppm(const char *path){
    FILE *f=fopen(path,"wb");uint32_t i;if(!f)return 0;
    (void)fprintf(f,"P6\n%u %u\n255\n",W,H);
    for(i=0;i<W*H;i++){
        uint16_t v=frame[i];unsigned char rgb[3]={
            (unsigned char)(((v>>11)&31u)*255u/31u),
            (unsigned char)(((v>>5)&63u)*255u/63u),
            (unsigned char)((v&31u)*255u/31u)};
        (void)fwrite(rgb,1u,3u,f);
    }
    return fclose(f)==0;
}

int main(int argc,char **argv){
    HmiState state;unsigned i;const char *out=argc>2?argv[2]:"preview.ppm";
    hmi_state_defaults(&state);
    if(argc>1)for(i=0;i<(unsigned)HMI_SCENE_COUNT;i++)if(strcmp(argv[1],hmi_generated_scenes[i].name)==0)state.scene_override=(HmiSceneId)i;
    hmi_init();hmi_render_full(&state,collect,NULL);
    if(!write_ppm(out)){(void)fprintf(stderr,"cannot write %s\n",out);return 1;}
    (void)printf("wrote %s, scene=%s, pixels=%lu\n",out,
        hmi_generated_scenes[hmi_scene_for_state(&state)].name,
        (unsigned long)hmi_dirty_pixel_count());return 0;
}
