#include "hmi_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint16_t frame[320*480]; /* Host only. */
static void collect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    unsigned row;(void)user;for(row=0;row<h;row++)memcpy(frame+(y+row)*320+x,p+row*stride,w*2u);
}
int main(int argc,char **argv){
    HmiUi ui;FILE *f;unsigned i;hmi_ui_init(&ui,(HmiDisplay){collect,NULL});
    if(argc>2){
        int dialog=atoi(argv[2]);
        if(dialog==HMI_DIALOG_KEYPAD)hmi_ui_dispatch(&ui,HMI_ACTION_EDIT,HMI_VALUE_MODULATION);
        else if(dialog==HMI_DIALOG_ROM)hmi_ui_dispatch(&ui,HMI_ACTION_ROM,0);
        else hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,(int16_t)dialog);
    }
    hmi_ui_render(&ui);f=fopen(argc>1?argv[1]:"ui.ppm","wb");if(!f)return 1;
    fprintf(f,"P6\n320 480\n255\n");
    for(i=0;i<320u*480u;i++){
        uint16_t v=frame[i];unsigned char b[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
        if(fwrite(b,1,3,f)!=3){fclose(f);return 1;}
    }
    return fclose(f)!=0;
}
