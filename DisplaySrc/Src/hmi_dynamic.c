#include "hmi_state.h"
#include "hmi_gfx.h"
#include "hmi_plot.h"

#include <stdio.h>
#include <string.h>

static void center_pair(int center,int baseline,int px,uint16_t color,
                        const char *value,const char *unit,int gap){
    int a=ui_measure_cstr(px,value),b=ui_measure_cstr(px,unit),x=center-(a+(unit[0]?gap+b:0))/2;
    ui_text_cstr(x,baseline,px,color,value,1);
    if(unit[0])ui_text_cstr(x+a+gap,baseline,px,color,unit,1);
}

#if !HMI_EXTERNAL_ASSETS
static void clear_box(int x,int y,int w,int h){ui_fill_rect(x,y,w,h,4324u);}
#endif
static void schema_background(int x,int w,uint16_t color){
    /* Exact palette keys, limited to the existing box: symbols and outlines
     * use other colors. No extra image, shape, border or framebuffer. */
    static const uint16_t palette[]={4324u,6531u,12610u,16547u};unsigned i;
    for(i=0;i<4;i++)ui_replace_color_rect(x,65,w,55,palette[i],color);
}

static uint16_t indicator(const HmiState *s,unsigned index){
    static const uint16_t colors[]={36051u,34276u,62946u,62154u};
    return colors[s->visual_valid?s->visual[index]&3u:0];
}
static float graph_root(float v){
    float x=v>1?v:1;unsigned i;if(v<=0)return 0;
    for(i=0;i<20;i++)x=(x+v/x)*0.5f;
    return x;
}
static const uint16_t plot_colors[5][4]={{62946,34276,11516,50044},{62530,9339,34276,47996},
    {62530,34276,47996,62530},{62530,34276,47996,0},{62530,9339,47996,0}};
static int plot_y(const HmiGraphChannel *g,unsigned i){
    int y=(int)(((int32_t)g->maximum-g->samples[i])*177/((int32_t)g->maximum-g->minimum));
    return y<0?0:y>177?177:y;
}
int hmi_plot_capture(const HmiState *s,HmiPlotImage *image){
    unsigned ch,i,count=(s->graph_page>=HMI_GRAPH_D||
        (s->graph_page==HMI_GRAPH_SPEED&&s->drive_mode!=HMI_DRIVE_UF))?3u:4u;
    if(s->graph_page>HMI_GRAPH_Q)return 0;
    for(ch=0;ch<count;ch++)if(s->graph[ch].sample_count>HMI_PLOT_POINTS)return 0;
    image->width=(uint16_t)((s->graph_page==HMI_GRAPH_SPEED&&s->drive_mode==HMI_DRIVE_SF)?279:255);
    image->cursor=s->graph_cursor;image->valid_count=s->graph_valid_count;
    for(ch=0;ch<HMI_GRAPH_CHANNELS;ch++){
        const HmiGraphChannel *g=&s->graph[ch];image->count[ch]=0;
        image->color[ch]=plot_colors[s->graph_page][ch];
        if(ch>=count||!g->samples||!g->visible||g->sample_count<2||g->maximum<=g->minimum)continue;
        image->count[ch]=g->sample_count;
        for(i=0;i<g->sample_count;i++)image->y[ch][i]=(uint8_t)plot_y(g,i);
    }
    return 1;
}
static void plot_background(unsigned w){
    unsigned i;ui_set_clip(30,106,(int)w+1,178);ui_fill_rect(30,106,(int)w+1,178,4357u);
    for(i=0;i<5;i++)ui_line(30,106+(int)(177*i/4),30+(int)w,106+(int)(177*i/4),12841u);
    for(i=0;i<6;i++)ui_line(30+(int)(w*i/5),106,30+(int)(w*i/5),283,12841u);
}
void hmi_plot_draw(const HmiPlotImage *image){
    unsigned ch,i;int w=image->width;plot_background((unsigned)w);
    for(ch=0;ch<HMI_GRAPH_CHANNELS;ch++){
        int px=0,py=0;
        for(i=0;i<image->count[ch];i++){
            int x,y;if(image->valid_count&&i>=image->valid_count)break;
            x=30+(int)(i*(unsigned)w/(image->count[ch]-1u));y=106+image->y[ch][i];
            if(i&&i!=image->cursor&&ui_rect_visible(px,py<y?py:y,x-px+1,(py<y?y-py:py-y)+1))
                ui_line(px,py,x,y,image->color[ch]);
            px=x;py=y;
        }
    }
    if(image->cursor){int x=30+(image->cursor-1)*w/239;ui_line(x,106,x,283,61342u);}
    ui_set_clip(1,1,318,478);
}
static void draw_live_graph(const HmiState *s){
    unsigned ch,i,count=(s->graph_page>=HMI_GRAPH_D||
        (s->graph_page==HMI_GRAPH_SPEED&&s->drive_mode!=HMI_DRIVE_UF))?3u:4u;
    int w=(s->graph_page==HMI_GRAPH_SPEED&&s->drive_mode==HMI_DRIVE_SF)?279:255;
    const uint16_t (*colors)[4]=plot_colors;
    plot_background((unsigned)w);
    ui_set_clip(1,35,318,268);
    for(i=0;ui_rect_visible(1,284,318,19)&&i<6;i++){
        char text[24];uint32_t ms=s->graph_offset_ms+(5u-i)*(s->graph_division_ms?s->graph_division_ms:200u);
        if(ms>=1000u)(void)snprintf(text,sizeof(text),"-%.1f с",ms/1000.0);
        else (void)snprintf(text,sizeof(text),ms?"-%lu мс":"%lu мс",(unsigned long)ms);
        ui_text_cstr(23+(int)(i*(unsigned)w/5u),297,7,40245u,text,0);
    }
    for(ch=0;ch<2;ch++){
        unsigned index=ch?(s->graph_page<=HMI_GRAPH_SPEED&&count==4?3u:2u):0u;
        const HmiGraphChannel *g=&s->graph[index];float scale=s->graph_scale[index]?s->graph_scale[index]:10;
        if(ch&&w==279)continue;
        if(!ui_rect_visible(ch?288:9,101,ch?30:21,187))continue;
        ui_set_clip(ch?288:9,101,ch?30:21,187);
        for(i=0;i<5;i++){
            char text[24];float v=(g->maximum-(g->maximum-g->minimum)*i/4.0f)/scale;
            if(g->sample_count)(void)snprintf(text,sizeof(text),"%.3g",(double)v);
            else memcpy(text,"—",4);
            ui_text_cstr(ch?288:9,109+(int)((i*177u+2u)/4u),7,ch?colors[s->graph_page][index]:40245u,text,0);
        }
    }
    for(ch=0;ch<count;ch++){
        const HmiGraphChannel *g=&s->graph[ch];float scale=s->graph_scale[ch]?s->graph_scale[ch]:10;
        uint16_t color=g->visible?colors[s->graph_page][ch]:23661u;
        int px=0,py=0,started=0,center=count==4?44+(int)ch*77:60+(int)ch*103;
        int left=count==4?(ch==0?13:ch==1?91:ch==2?168:245):13+(int)ch*103;
        float sum=0,squares=0,lo=0,hi=0,last=0;char text[24];
        const char *unit="";int decimals=2;int stats_visible=ui_rect_visible(left-4,361,count==4?73:99,68);
        if(s->graph_page==HMI_GRAPH_POWER){unit=ch==3?"В":"А";if(ch==3)decimals=0;}
        else if(s->graph_page==HMI_GRAPH_SPEED){unit=ch<3?(s->drive_mode==HMI_DRIVE_VF&&ch==2?"А":"Гц"):"";if(unit[0])decimals=1;}
        else if(ch<2)unit="А";
        ui_set_clip(30,106,w+1,178);
        if(g->samples&&g->sample_count){
            lo=hi=g->samples[0]/scale;
            for(i=0;i<g->sample_count;i++){
                int x,y;
                if(stats_visible){float v=g->samples[i]/scale;sum+=v;squares+=v*v;if(v<lo)lo=v;if(v>hi)hi=v;last=v;}
                if((s->graph_valid_count&&i>=s->graph_valid_count)||!g->visible||g->sample_count<2||g->maximum<=g->minimum)continue;
                x=30+(int)((uint32_t)i*(unsigned)w/(g->sample_count-1u));
                y=106+plot_y(g,i);
                if(started&&i!=s->graph_cursor&&ui_rect_visible(px,py<y?py:y,x-px+1,(py<y?y-py:py-y)+1))ui_line(px,py,x,y,color);
                px=x;py=y;started=1;
            }
        }
        if(!ui_rect_visible(left-4,361,count==4?73:99,68))continue;
        if(s->graph_cursor&&g->sample_count)last=g->samples[s->graph_cursor-1]/scale;
        ui_set_clip(left-4,361,count==4?73:99,68);
        if(g->sample_count){
            int a,b,x;float stats[4];static const int offsets[]={23,22,24,21};
            (void)snprintf(text,sizeof(text),"%.*f",decimals,(double)last);
            a=ui_measure_cstr(14,text);b=ui_measure_cstr(9,unit);x=center-(a+(unit[0]?4+b:0))/2;
            ui_text_cstr(x,377,14,color,text,1);
            if(unit[0])ui_text_cstr(x+a+4,377,9,48664u,unit,1);
            stats[0]=graph_root(squares/g->sample_count);stats[1]=sum/g->sample_count;stats[2]=hi;stats[3]=lo;
            for(i=0;i<4;i++){
                (void)snprintf(text,sizeof(text),"%.*f",decimals,(double)stats[i]);
                ui_text_cstr(left+offsets[i],394+(int)i*11,9,50777u,text,0);
            }
        }else ui_text_cstr(center-5,377,14,color,"—",1);
    }
    if(s->graph_cursor){int x=30+(s->graph_cursor-1)*w/239;ui_set_clip(30,106,w+1,178);ui_line(x,106,x,283,61342u);}
    ui_set_clip(1,1,318,478);
}
static void draw_graph(const HmiState *s){
#if HMI_EXTERNAL_ASSETS
    draw_live_graph(s);
#else
    unsigned ch,i;const int x0=30,y0=106,w=254,h=177;
    if(s->telemetry_flags&128u){draw_live_graph(s);return;}
    ui_set_clip(1,1,318,478);
    ui_fill_rect(x0,y0,w+1,h+1,4357u);
    for(i=0;i<=4u;i++)ui_line(x0,y0+(int)(h*i/4u),x0+w,y0+(int)(h*i/4u),12841u);
    for(i=0;i<=5u;i++)ui_line(x0+(int)(w*i/5u),y0,x0+(int)(w*i/5u),y0+h,12841u);
    for(ch=0;ch<HMI_GRAPH_CHANNELS;ch++){
        const HmiGraphChannel *g=&s->graph[ch];int px=0,py=0,started=0;
        if(g->label){int tw=ui_measure_cstr(10,g->label),cx=42+(int)ch*77;
            ui_fill_rect(10+(int)ch*77,342,64,18,4324u);
            ui_text_cstr(cx-tw/2,356,10,g->visible?g->color_rgb565:48664u,g->label,1);
        }
        if(!g->visible||!g->samples||g->sample_count<2u||g->maximum<=g->minimum)continue;
        for(i=0;i<g->sample_count;i++){
            int x=x0+(int)((uint32_t)i*w/(g->sample_count-1u));
            int32_t v=g->samples[i];int y=y0+(int)((int32_t)(g->maximum-v)*h/(g->maximum-g->minimum));
            if(y<y0)y=y0;
            if(y>y0+h)y=y0+h;
            if(started)ui_line(px,py,x,y,g->color_rgb565);
            px=x;py=y;started=1;
        }
    }
#endif
}

static void draw_journal(const HmiState *s){
    uint32_t offset=s->journal_paged?0:s->journal_page>0?((uint32_t)s->journal_page-1u)*7u:0;
    uint16_t row,count=offset<s->journal_count?(uint16_t)(s->journal_count-offset):0;
    if(s->journal_paged){uint32_t remaining=s->journal_count>(s->journal_page-1u)*7u?s->journal_count-(s->journal_page-1u)*7u:0;count=(uint16_t)remaining;}
    if(count>7u)count=7u;
    ui_set_clip(9,133,302,254);ui_fill_rect(9,133,302,254,4357u);
    for(row=0;row<7u;row++)ui_line(9,167+(int)row*35,310,167+(int)row*35,12809u);
    for(row=0;row<count;row++){
        const HmiJournalItem *item=&s->journal[offset+row];char date[11]={0};const char *time="";
        uint16_t color=15709u;const char *type="Инфо.",*icon="i";int y=149+(int)row*35;
        if(item->timestamp){size_t n=strlen(item->timestamp);if(n>10u)n=10u;memcpy(date,item->timestamp,n);if(strlen(item->timestamp)>11u)time=item->timestamp+11;}
        if(item->type&&strcmp(item->type,"warn")==0){color=62754u;type="Предупр.";icon="!";}
        else if(item->type&&strcmp(item->type,"fault")==0){color=62154u;type="Авария";icon="×";}
        ui_text_cstr(14,y,12,61342u,date,0);ui_text_cstr(14,y+13,12,44503u,time,0);
        ui_text_cstr(81,y+7,12,color,icon,1);ui_text_cstr(91,y+6,12,color,type,0);
        if(item->message)ui_text_cstr(154,y+6,12,61342u,item->message,0);
    }
    ui_set_clip(1,1,318,478);
}

/* Dynamic overlays intentionally reuse the exact exported font rasterizer.
 * Static layout coordinates remain generated from the HTML. */
void hmi_draw_dynamic(const HmiState *s) {
    char value[24];int width,x;
#if !HMI_EXTERNAL_ASSETS
    uint16_t actual_color;
#endif
    if(!s)return;
    if(ui_rect_visible(0,0,320,40)&&s->clock&&((s->telemetry_flags&128u)||strcmp(s->clock,"14:32")!=0)){
            ui_set_clip(1,1,318,478);ui_fill_round_rect(258,3,54,29,4,2211u);
            ui_round_rect(258,3,54,29,4,12842u);
            ui_text_cstr(258+(54-ui_measure_cstr(16,s->clock))/2,24,16,57116u,s->clock,1);
    }
    if(!s->dynamic_values&&!(s->telemetry_flags&128u))return;
    ui_set_clip(1,1,318,478);
    if(s->telemetry_flags&128u){
        uint8_t f=s->telemetry_flags;uint16_t color=36051u;
        const char *status="СТОП";int status_width=35,mode_x=0;
        if(s->power_state==HMI_POWER_FAULT){status="АВАРИЯ";color=62154u;status_width=52;}
        else if(f&24u){status="РАЗРЯД";color=62946u;status_width=52;}
        else if(s->power_state==HMI_POWER_CHARGE){status="ЗАРЯД";color=62946u;status_width=44;}
        else if(s->power_state==HMI_POWER_READY){status="ГОТОВ";color=34276u;status_width=42;mode_x=67;}
        else if(s->power_state==HMI_POWER_RUN||s->power_state==HMI_POWER_REGULATING){
            status="РАБОТА";color=34276u;status_width=50;mode_x=76;
        }else if(s->power_state==HMI_POWER_FAULT){status="АВАРИЯ";color=57928u;status_width=52;}
        if(ui_rect_visible(7,1,97,33)){
            /* Rebuild the icon from geometry, with one current color. */
            ui_set_clip(7,1,15,33);ui_fill_gradient_v(1,1,319,34,15052u,10793u);
            ui_line(11,15,9,18,color);ui_line(9,18,9,21,color);ui_line(9,21,12,24,color);ui_line(12,24,16,24,color);ui_line(16,24,19,21,color);ui_line(19,21,19,18,color);ui_line(19,18,17,15,color);ui_line(14,10,14,18,color);
            ui_set_clip(22,1,82,33);ui_fill_gradient_v(1,1,319,34,15052u,10793u);
            ui_text_fit_cstr(23,24,13,color,status,1,status_width);
            if(mode_x){
                if(s->drive_mode==HMI_DRIVE_UF){
                    ui_text_fit_cstr(mode_x,22,9,34276u,"(U",1,9);
                    ui_text_fit_cstr(mode_x+11,22,9,34276u,"/",1,4);
                    ui_text_fit_cstr(mode_x+15,22,9,34276u,"F)",1,8);
                }else ui_text_fit_cstr(mode_x,22,9,34276u,s->drive_mode==HMI_DRIVE_SF?"(SF)":"(VF)",1,17);
            }
            ui_set_clip(1,1,318,478);
        }
        if(s->dialog!=HMI_DIALOG_NONE)return;
        if(s->page==HMI_PAGE_HOME){
            static const uint16_t backgrounds[]={4324u,6531u,12610u,16547u};
            static const int xs[]={68,117,169,217,270},ws[]={35,38,37,38,33};
            unsigned state=s->visual_valid?s->visual[0]:0;
            if(s->visual_valid&&s->visual[1]>state)state=s->visual[1];
            for(unsigned k=0;k<4;k++)ui_replace_color_rect(25,78,31,31,backgrounds[k],backgrounds[state&3u]);
            for(unsigned k=0;k<5;k++)schema_background(xs[k],ws[k],backgrounds[s->visual_valid?s->visual[k+2]&3u:0]);
        }
    }
    if(s->dialog!=HMI_DIALOG_NONE)return;
    if(s->page==HMI_PAGE_HOME&&ui_rect_visible(0,80,320,40)&&(s->telemetry_flags&128u||s->power_state==HMI_POWER_CHARGE)&&s->precharge_seconds>=0.0f){
        uint16_t timer_color=indicator(s,3);
        (void)snprintf(value,sizeof(value),"%.1f",(double)s->precharge_seconds);
        ui_set_clip(1,1,318,478);ui_fill_round_rect(123,92,26,17,2,2211u);
        ui_round_rect(123,92,26,17,2,timer_color);
        width=ui_measure_cstr(10,value);x=136-(width+7)/2;
        ui_text_cstr(x,104,10,timer_color,value,1);ui_text_cstr(x+width+3,104,10,timer_color,"с",1);
    }
    if(s->page==HMI_PAGE_GRAPHS){draw_graph(s);return;}
    if(s->page==HMI_PAGE_JOURNAL&&s->journal){draw_journal(s);return;}
    if(s->page!=HMI_PAGE_HOME)return;
    if(ui_rect_visible(0,40,320,40)){
        if(s->sensor_mask&(1u<<0)){
        if(!ui_text_layers_home())ui_fill_rect(20,51,43,24,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->mains_voltage);center_pair(40,64,10,indicator(s,0),value,"В",2);
        (void)snprintf(value,sizeof(value),"%.1f",(double)s->mains_frequency);center_pair(40,73,10,indicator(s,1),value,"Гц",3);
        }
        if(s->sensor_mask&(1u<<2)){
        if(!ui_text_layers_home())ui_fill_rect(118,51,37,12,4357u);
        (void)snprintf(value,sizeof(value),"%.2f",(double)s->precharge_current);center_pair(136,61,10,indicator(s,3),value,"А",3);
        }
        if(s->sensor_mask&(1u<<1)){
        if(!ui_text_layers_home())ui_fill_rect(170,51,36,12,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->dc_bus_voltage);center_pair(188,61,10,indicator(s,4),value,"В",3);
        }
    }
    if(ui_rect_visible(0,96,320,48)){
        if(s->sensor_mask&(1u<<3)){
        if(!ui_text_layers_home())ui_fill_rect(66,119,39,15,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->temperature_rectifier);center_pair(85,132,10,indicator(s,7),value,"°C",2);
        }
        if(s->sensor_mask&(1u<<4)){
        if(!ui_text_layers_home())ui_fill_rect(117,119,39,15,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->temperature_precharge);center_pair(136,132,10,indicator(s,8),value,"°C",2);
        }
        if(s->sensor_mask&(1u<<5)){
        if(!ui_text_layers_home())ui_fill_rect(169,119,39,15,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->temperature_dc);center_pair(188,132,10,indicator(s,9),value,"°C",2);
        }
        if(s->sensor_mask&(1u<<6)){
        if(!ui_text_layers_home())ui_fill_rect(217,119,39,15,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->temperature_inverter);center_pair(236,132,10,indicator(s,10),value,"°C",2);
        }
        if(s->sensor_mask&(1u<<7)){
        if(!ui_text_layers_home())ui_fill_rect(268,107,39,15,4357u);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->temperature_motor);center_pair(287,120,10,indicator(s,11),value,"°C",2);
        }

    }
    if(ui_text_layers_home())return; /* HmiUi draws these values once. */
#if !HMI_EXTERNAL_ASSETS
    actual_color=s->regulation_active?63016u:36454u;
    if(ui_rect_visible(0,172,320,36)){
        clear_box(121,179,97,21);clear_box(226,179,82,21);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->modulation_set);center_pair(179,195,13,36454u,value,"%",4);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->modulation_actual);center_pair(265,195,12,actual_color,value,"%",3);
    }
    if(ui_rect_visible(0,200,320,36)){
        clear_box(121,206,97,21);clear_box(226,206,82,21);
        (void)snprintf(value,sizeof(value),"%.0f",(double)(s->rotation_set*60.0f));center_pair(179,223,13,36454u,value,"об/мин",4);
        (void)snprintf(value,sizeof(value),"%.0f",(double)(s->rotation_actual*60.0f));center_pair(265,222,12,actual_color,value,"об/мин",3);
    }
    if(ui_rect_visible(0,228,320,36)){
        clear_box(121,233,97,21);clear_box(226,233,82,21);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->current_limit_set);center_pair(179,250,13,36454u,value,"%",4);
        (void)snprintf(value,sizeof(value),"%.0f",(double)s->current_limit_actual);center_pair(265,250,12,actual_color,value,"%",3);

    }
    if(ui_rect_visible(0,296,320,40)){clear_box(15,304,137,20);(void)snprintf(value,sizeof(value),"%.0f",(double)s->output_voltage);ui_text_cstr(16,322,16,36454u,value,1);}
    if(ui_rect_visible(0,334,320,40)){clear_box(15,342,137,20);(void)snprintf(value,sizeof(value),"%.2f",(double)s->output_current);ui_text_cstr(16,360,16,36454u,value,1);}
    if(ui_rect_visible(0,372,320,40)){clear_box(15,380,137,20);(void)snprintf(value,sizeof(value),"%.2f",(double)s->output_power);ui_text_cstr(16,398,16,36454u,value,1);}
    if(ui_rect_visible(0,399,320,40)){clear_box(64,407,45,15);(void)snprintf(value,sizeof(value),"%.0f",(double)s->dc_bus_voltage);ui_text_cstr(66,420,9,36454u,value,1);}
    if(ui_rect_visible(0,296,320,40)){clear_box(170,304,135,20);(void)snprintf(value,sizeof(value),"%.1f",(double)s->rotor_frequency);ui_text_cstr(171,322,16,15709u,value,1);}
    if(ui_rect_visible(0,329,320,40)){clear_box(170,337,135,20);(void)snprintf(value,sizeof(value),"%.1f",(double)s->stator_frequency);ui_text_cstr(171,355,16,15709u,value,1);}
    if(ui_rect_visible(0,361,320,40)){clear_box(170,369,135,20);(void)snprintf(value,sizeof(value),"%.1f",(double)s->slip);ui_text_cstr(171,387,16,15709u,value,1);}
    if(ui_rect_visible(0,393,320,40)){
        clear_box(170,401,135,20);
        if(s->motor_load<0.0f)ui_text_cstr(171,419,16,15709u,"—",1);
        else{(void)snprintf(value,sizeof(value),"%.0f",(double)s->motor_load);ui_text_cstr(171,419,16,15709u,value,1);}
    }
#endif
}
