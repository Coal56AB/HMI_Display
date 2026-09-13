#include "storage_fixture.h"
#include "hmi_ui.h"
#include "hmi_gfx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t frame[320u*480u];
static int16_t samples[4][240];
static unsigned sent,writes,largest;
static HmiUi *interrupt_ui;
void hmi_draw_dynamic(const HmiState *state);
static void collect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    unsigned row,n=(unsigned)w*h;(void)user;assert(x+w<=320&&y+h<=480&&stride>=w);
    sent+=n;writes++;if(n>largest)largest=n;
    for(row=0;row<h;row++)memcpy(frame+(y+row)*320+x,p+row*stride,w*sizeof(*p));
    if(interrupt_ui){HmiUi *ui=interrupt_ui;interrupt_ui=0;hmi_ui_dispatch(ui,HMI_ACTION_PAGE,HMI_PAGE_PARAMETERS);}
}
/* Independent full plot rasterization, without changing the diff history. */
static void verify_plot(HmiUi *ui){
    unsigned y,row,x,w=ui->state.graph_page==HMI_GRAPH_SPEED&&ui->state.drive_mode==HMI_DRIVE_SF?280u:256u;
    for(y=106;y<284;y+=2){unsigned h=284-y;if(h>2)h=2;
        ui_begin_rect(30,(int)y,(int)w,(int)h,0);hmi_draw_dynamic(&ui->state);
        for(row=0;row<h;row++)for(x=0;x<w;x++)assert(frame[(y+row)*320+30+x]==ui_strip_data()[row*w+x]);
    }
}
static void update(HmiUi *ui,HmiRect r){
    sent=writes=largest=0;hmi_invalidate(r);hmi_ui_render(ui);verify_plot(ui);
    assert(sent<=(unsigned)r.width*r.height);
}
int main(void){
    HmiUi ui;unsigned ch,i,page,mode;uint32_t transferred=0,unfiltered=0;
    fixture_init();hmi_ui_init(&ui,(HmiDisplay){collect,0});
    ui.state.page=HMI_PAGE_GRAPHS;ui.state.telemetry_flags=128;
    for(ch=0;ch<4;ch++){
        ui.state.graph[ch]=(HmiGraphChannel){samples[ch],240,-100,100,0,1,"",""};
        for(i=0;i<240;i++)samples[ch][i]=(int16_t)((i*7+ch*17)%181-90);
    }
    for(page=0;page<5;page++)for(mode=0;mode<3;mode++){
        unsigned width=page==HMI_GRAPH_SPEED&&mode==HMI_DRIVE_SF?280u:256u;
        HmiRect plot={30,106,(uint16_t)width,178};
        ui.state.graph_page=(HmiGraphPage)page;ui.state.drive_mode=(HmiDriveMode)mode;
        hmi_invalidate_all();hmi_ui_render(&ui);verify_plot(&ui);
        update(&ui,plot);assert(!sent&&!writes);
        for(i=1;i<=240;i++){
            unsigned old=ui.state.graph_cursor?ui.state.graph_cursor-1:0;
            unsigned first=i>1?i-2:0,last=i<240?i:239;
            HmiRect r;if(old<first)first=old;if(old>last)last=old;
            for(ch=0;ch<4;ch++)samples[ch][i-1]=(int16_t)((i*11+ch*23)%201-100);
            ui.state.graph_cursor=(uint8_t)i;
            r=(HmiRect){(uint16_t)(30+first*(width-1)/239),106,0,178};
            r.width=(uint16_t)(31+last*(width-1)/239-r.x);
            update(&ui,r);transferred+=sent;unfiltered+=(uint32_t)r.width*r.height;
            assert(largest<=712u);
        }
        /* In-place changes, overlapping traces, hidden channels, scale and
         * stopped sweeps must all erase the previous final colors exactly. */
        ui.state.graph[1].visible^=1;update(&ui,plot);
        ui.state.graph_valid_count=89;ui.state.graph_cursor=0;update(&ui,plot);
        ui.state.graph_valid_count=0;ui.state.graph[0].minimum=-40;ui.state.graph[0].maximum=40;update(&ui,plot);
        update(&ui,plot);assert(!sent);
    }
    assert(transferred<unfiltered);
    printf("graph diff: 15 modes, 3600 sweep updates, %lu / %lu pixels (%lu%%), exact reconstruction PASS\n",
        (unsigned long)transferred,(unsigned long)unfiltered,(unsigned long)(100u*transferred/unfiltered));
    ui.state.graph_cursor=120;interrupt_ui=&ui;
    hmi_invalidate((HmiRect){30,106,256,178});hmi_ui_render(&ui);
    assert(!interrupt_ui&&hmi_dirty_pending()&&ui.state.page==HMI_PAGE_PARAMETERS);
    hmi_ui_render(&ui);assert(!hmi_dirty_pending());
    ui.state.page=HMI_PAGE_GRAPHS;hmi_invalidate_all();hmi_ui_render(&ui);verify_plot(&ui);
    update(&ui,(HmiRect){30,106,256,178});assert(!sent);
    puts("graph diff: interrupted flush and page restoration PASS");
    return 0;
}
