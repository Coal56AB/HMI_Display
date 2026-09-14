#include "telemetry.h"
#include <stddef.h>
#include <string.h>

#ifndef __weak
#define __weak __attribute__((weak))
#endif
#define POINTS 240u
static volatile uint16_t head,tail;
static volatile uint8_t overflow;
static uint8_t queue[256],frame[114];
static unsigned used,reset_match;
static uint32_t last_rx;
static uint32_t parser_time;
static uint8_t online;
static uint32_t selection_until;
void telemetry_selected(uint32_t now){selection_until=now+2000u;}
__weak void telemetry_notice(unsigned code,float value){(void)code;(void)value;}
__weak void telemetry_clock(uint32_t stamp){(void)stamp;}
static int16_t samples[4][POINTS];
static uint32_t graph_stats_tick;
static int16_t graph_staging[POINTS];
static uint8_t staging_page,staging_channel,staging_count;
static uint8_t graph_page;
static const uint16_t fields[]={
    offsetof(HmiState,mains_voltage),offsetof(HmiState,mains_frequency),
    offsetof(HmiState,dc_bus_voltage),offsetof(HmiState,precharge_current),
    offsetof(HmiState,precharge_seconds),offsetof(HmiState,temperature_rectifier),
    offsetof(HmiState,temperature_precharge),offsetof(HmiState,temperature_dc),
    offsetof(HmiState,temperature_inverter),offsetof(HmiState,temperature_motor),
    offsetof(HmiState,modulation_set),offsetof(HmiState,modulation_actual),
    offsetof(HmiState,rotation_set),offsetof(HmiState,rotation_actual),
    offsetof(HmiState,current_limit_set),offsetof(HmiState,current_limit_actual),
    offsetof(HmiState,output_voltage),offsetof(HmiState,output_current),
    offsetof(HmiState,output_power),offsetof(HmiState,rotor_frequency),
    offsetof(HmiState,stator_frequency),offsetof(HmiState,slip),offsetof(HmiState,motor_load)
};
static const char *const labels[5][4]={
    {"U, В","I, А","P, кВт","DC, В"},
    {"Ротор, Гц","Статор, Гц","Зад., Гц","Нагр., %"},
    {"Лимит, %","Ток, %","Мод., %","Зад., %"},
    {"Id, А","Ud, В","Id зад., А","Ud зад., В"},
    {"Iq, А","Uq, В","Iq зад., А","Uq зад., В"}
};
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint16_t crc16(const uint8_t *p,unsigned n){
    uint16_t c=0xffff;unsigned i;
    while(n--){c^=(uint16_t)*p++<<8;for(i=0;i<8;i++)c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));}
    return c;
}
void telemetry_byte(uint8_t b){
    uint16_t next=(uint16_t)((head+1u)&255u);
    if(next==tail){overflow=1;return;}queue[head]=b;head=next;
}
void telemetry_rx_error(void){overflow=1;}
static void clear_graph(HmiUi *ui){
    unsigned i;graph_page=(uint8_t)ui->state.graph_page;ui->state.graph_cursor=0;ui->state.graph_valid_count=0;staging_count=0;
    for(i=0;i<4;i++){
        ui->state.graph[i].samples=samples[i];ui->state.graph[i].sample_count=0;
        ui->state.graph[i].label=labels[graph_page][i];
    }
    if(ui->state.page==HMI_PAGE_GRAPHS)hmi_invalidate_all();
}
void telemetry_init(HmiUi *ui){
    unsigned i;head=tail=0;overflow=0;used=reset_match=0;online=0;last_rx=parser_time=0;
    for(i=0;i<23;i++){float v=0;memcpy((uint8_t *)&ui->state+fields[i],&v,4);}
    ui->state.motor_load=-1;ui->state.power_state=HMI_POWER_OFF;
    ui->state.inverter_enabled=ui->state.regulation_active=0;
    ui->state.telemetry_flags=128;
    memcpy(ui->clock,"--:--",6);ui->graph_running=1;ui->graph_time_ms=HMI_GRAPH_LIVE_MIN_MS;
    for(i=0;i<4;i++){
        static const uint16_t colors[]={15709,63016,36454,62154};
        ui->state.graph[i].visible=1;ui->state.graph[i].color_rgb565=colors[i];
    }
    clear_graph(ui);hmi_invalidate_all();
}
static uint8_t apply(HmiUi *ui,const uint8_t *p,unsigned type,unsigned n,uint32_t now){
    unsigned i;
    if(type==11){
        uint32_t offset;if(n!=8)return 1;memcpy(&offset,p+4,4);
        if(ui->graph_running)offset=0;
        if(ui->state.graph_offset_ms!=offset){ui->state.graph_offset_ms=offset;if(ui->state.page==HMI_PAGE_GRAPHS&&ui->state.dialog==HMI_DIALOG_NONE)hmi_invalidate((HmiRect){7,284,306,15});}
        return 0;
    }
    if(type==10){
        if(!ui->graph_running)return 0;
        unsigned cursor=p[5],count=p[6];if(n!=35||p[4]>4||cursor<1||cursor>POINTS||count<1||count>4)return 1;
        if(p[4]!=ui->state.graph_page)return 2;
        for(i=0;i<4;i++)if(p[7+i]>2||(int16_t)get16(p+13+i*4)<=(int16_t)get16(p+11+i*4))return 1;
        unsigned old_cursor=ui->state.graph_cursor;int axes=0;
        for(i=0;i<count;i++){
            int16_t lo=(int16_t)get16(p+11+i*4),hi=(int16_t)get16(p+13+i*4);
            if(ui->state.graph[i].sample_count!=POINTS){memset(samples[i],0,sizeof(samples[i]));axes=1;}
            if(ui->state.graph[i].minimum!=lo||ui->state.graph[i].maximum!=hi)axes=1;
            samples[i][cursor-1]=(int16_t)get16(p+27+i*2);ui->state.graph[i].sample_count=POINTS;
            ui->state.graph[i].minimum=lo;ui->state.graph[i].maximum=hi;ui->state.graph_scale[i]=p[7+i]==2?1000:p[7+i]==1?100:10;
        }
        ui->state.graph_cursor=(uint8_t)cursor;ui->state.graph_valid_count=0;
        if(ui->state.page==HMI_PAGE_GRAPHS&&ui->state.dialog==HMI_DIALOG_NONE){
            if(axes)hmi_invalidate((HmiRect){7,101,306,188});
            else {unsigned width=(ui->state.graph_page==HMI_GRAPH_SPEED&&ui->state.drive_mode==HMI_DRIVE_SF)?279:255;
                unsigned x=30+(cursor-1)*width/239;hmi_invalidate((HmiRect){(uint16_t)(x>30?x-1:x),106,(uint16_t)(cursor==POINTS?2:(30+cursor*width/239-x+2)),178});
                if(old_cursor){unsigned first=old_cursor>1?old_cursor-2:0,last=old_cursor<POINTS?old_cursor:POINTS-1;
                    x=30+first*width/239;hmi_invalidate((HmiRect){(uint16_t)x,106,(uint16_t)(30+last*width/239-x+2),178});}}
            if(now-graph_stats_tick>=1000){graph_stats_tick=now;hmi_invalidate((HmiRect){7,361,306,67});}
        }return 0;
    }
    if(type==9){
        HmiState old=ui->state;if(n!=16)return 1;
        for(i=0;i<12;i++)if(p[4+i]>3)return 1;
        memcpy(ui->state.visual,p+4,12);ui->state.visual_valid=1;
        hmi_diff_and_invalidate(&old,&ui->state);return 0;
    }
    if(type==8){
        float values[5];if(n!=26||p[4]>100||p[5]>2)return 1;
        memcpy(values,p+6,20);for(i=0;i<5;i++)if(!(values[i]>0&&values[i]<10000))return 1;
        if(ui->state.param_section==HMI_PARAM_AUTO_RUNNING){
            if(p[5]==1||ui->auto_progress){ui->auto_progress=p[4];
                if(p[5]==2&&p[4]==100){memcpy(ui->parameters+HMI_VALUE_RS,values,20);ui->state.param_section=HMI_PARAM_AUTO_DONE;}
                hmi_invalidate((HmiRect){110,65,205,364});
            }
        }
        return 0;
    }
    if(type==6){if(n!=12)return 1;uint32_t stamp;memcpy(&stamp,p+4,4);telemetry_clock(stamp);return 0;}
    if(type==5){
        HmiState old=ui->state;float pending[3];
        if(n!=17||(p[4]&~63u))return 1;
        memcpy(pending,p+5,12);
        for(i=0;i<3;i++)if(!(pending[i]>=0&&pending[i]<=10000))return 1;
        memcpy(ui->state.pending_setpoints,pending,12);ui->state.pending_mask=p[4]&7u;ui->state.control_warning_mask=(p[4]>>3)&7u;
        hmi_diff_and_invalidate(&old,&ui->state);return 0;
    }
    if(type==7){
        unsigned page,ch,start,count;int16_t lo,hi;
        if(n<14)return 1;
        page=p[4];ch=p[5];start=p[6];count=p[7];
        lo=(int16_t)get16(p+10);hi=(int16_t)get16(p+12);
        if(page>4||ch>3||p[8]>2||p[9]>POINTS||count>47||start+count>POINTS||n!=14+count*2||hi<=lo)return 1;
        if(page!=ui->state.graph_page)return 2;
        if(!start){staging_page=(uint8_t)page;staging_channel=(uint8_t)ch;staging_count=0;}
        if(staging_page!=page||staging_channel!=ch)return 1;
        if(start+count==staging_count)return 0;
        if(start!=staging_count)return 1;
        for(i=0;i<count;i++)graph_staging[start+i]=(int16_t)get16(p+14+i*2);
        staging_count=(uint8_t)(start+count);
        if(staging_count==POINTS){
            unsigned first=POINTS,last=0,width=(ui->state.graph_page==HMI_GRAPH_SPEED&&ui->state.drive_mode==HMI_DRIVE_SF)?279:255;
            int axes=ui->state.graph_valid_count||ui->state.graph[ch].sample_count!=POINTS||ui->state.graph[ch].minimum!=lo||ui->state.graph[ch].maximum!=hi;
            for(i=0;i<POINTS;i++)if(samples[ch][i]!=graph_staging[i]){if(first==POINTS)first=i;last=i;}
            if(axes){first=0;last=POINTS-1;}
            if(ui->state.graph_cursor){unsigned c=ui->state.graph_cursor-1;if(c<first)first=c;if(c>last)last=c;}
            if(p[9]){unsigned c=p[9]-1;if(c<first)first=c;if(c>last)last=c;}
            ui->state.graph_cursor=p[9];ui->state.graph_valid_count=0;
            memcpy(samples[ch],graph_staging,sizeof(graph_staging));
            ui->state.graph[ch].sample_count=POINTS;ui->state.graph[ch].minimum=lo;ui->state.graph[ch].maximum=hi;
            ui->state.graph_scale[ch]=(uint16_t)(p[8]==2?1000:p[8]==1?100:10);
            if(ui->state.page==HMI_PAGE_GRAPHS&&ui->state.dialog==HMI_DIALOG_NONE){
                if(axes)hmi_invalidate((HmiRect){7,101,306,188});
                else if(first<POINTS){unsigned x=30+(first?first-1:0)*width/(POINTS-1),end=32+(last+1)*width/(POINTS-1);if(end>30+width+1)end=30+width+1;
                    hmi_invalidate((HmiRect){(uint16_t)x,106,(uint16_t)(end-x),178});}
                if(now-graph_stats_tick>=500){graph_stats_tick=now;hmi_invalidate((HmiRect){7,337,306,91});}
            }
        }
        return 0;
    }
    if(type==2){
        HmiState old=ui->state;
        if(n!=104||p[4]>HMI_POWER_FAULT||p[5]>HMI_DRIVE_VF||p[6]>2||(p[7]&128u))return 1;
        /* Reject NaN/Inf and absurd values before committing any field. */
        for(i=0;i<23;i++){
            float v;memcpy(&v,p+12+i*4,4);
            if(!(v>=-100000.0f&&v<=100000.0f))return 1;
        }
        ui->state.power_state=(HmiPowerState)p[4];ui->state.drive_mode=(HmiDriveMode)p[5];
        if(!selection_until||(int32_t)(now-selection_until)>=0||p[6]==ui->state.selected_control){ui->state.selected_control=p[6];selection_until=0;}ui->state.inverter_enabled=p[7]&1;
        ui->state.regulation_active=(p[7]>>1)&1;ui->state.setpoint_matches=(p[7]>>2)&1;
        ui->state.telemetry_flags=(uint8_t)(129u|((p[7]>>2)&30u));
        for(i=0;i<23;i++)memcpy((uint8_t *)&ui->state+fields[i],p+12+i*4,4);
        for(i=0;i<3;i++)ui->parameters[i]=i==0?ui->state.modulation_set:i==1?ui->state.rotation_set:ui->state.current_limit_set;
        /* p[8..11]: controller uptime, reserved for future clock display. */
        hmi_diff_and_invalidate(&old,&ui->state);
        if(old.telemetry_flags!=ui->state.telemetry_flags||old.power_state!=ui->state.power_state)
            hmi_invalidate((HmiRect){8,3,300,137});
        if(!online)telemetry_notice(101,0);
        {unsigned previous=old.power_state==HMI_POWER_REGULATING?HMI_POWER_RUN:old.power_state;
         unsigned current=ui->state.power_state==HMI_POWER_REGULATING?HMI_POWER_RUN:ui->state.power_state;
         if(previous!=current)telemetry_notice(110+current,ui->state.dc_bus_voltage);}
        if(old.modulation_set!=ui->state.modulation_set)telemetry_notice(150,ui->state.modulation_set);
        if(old.rotation_set!=ui->state.rotation_set)telemetry_notice(151,ui->state.rotation_set);
        if(old.current_limit_set!=ui->state.current_limit_set)telemetry_notice(152,ui->state.current_limit_set);
        if(old.drive_mode!=ui->state.drive_mode)telemetry_notice(153,(float)ui->state.drive_mode);
        if(old.selected_control!=ui->state.selected_control)telemetry_notice(154,(float)ui->state.selected_control);
        if((old.telemetry_flags^ui->state.telemetry_flags)&24u)telemetry_notice(120,(float)(ui->state.telemetry_flags&24u));
        last_rx=now;online=1;return 0;
    }
    if(type==3){
        unsigned ch,count;int16_t lo,hi;
        if(n<12)return 1;
        ch=p[5];count=p[10];lo=(int16_t)get16(p+6);hi=(int16_t)get16(p+8);
        if(ch>=4||count>POINTS||n!=12+count*2||hi<=lo||p[4]>4||p[11]>2)return 1;
        if(p[4]!=ui->state.graph_page||!ui->graph_running)return 2;
        for(i=0;i<count;i++)samples[ch][i]=(int16_t)get16(p+12+i*2);
        ui->state.graph[ch].sample_count=(uint16_t)count;
        ui->state.graph[ch].minimum=lo;ui->state.graph[ch].maximum=hi;
        ui->state.graph_scale[ch]=(uint16_t)(p[11]==2?1000:p[11]==1?100:10);
        if(ui->state.page==HMI_PAGE_GRAPHS&&ui->state.dialog==HMI_DIALOG_NONE)
            hmi_invalidate((HmiRect){9,80,302,347});
        return 0;
    }
    return 1;
}
/* Runtime PCHR is recognized only outside framed traffic. */
extern void telemetry_reset(void);
void telemetry_poll(HmiUi *ui,uint32_t now){
    unsigned budget=256;
    if(used&&now-parser_time>500u)used=0;
    if(graph_page!=ui->state.graph_page)clear_graph(ui);
    if(overflow){tail=head;used=reset_match=0;overflow=0;}
    while(tail!=head&&budget--){
        uint8_t b=queue[tail];tail=(uint16_t)((tail+1u)&255u);
        parser_time=now;
        if(!used){
            static const char reset[]="PCHR";
            reset_match=b==(uint8_t)reset[reset_match]?reset_match+1:(b=='P'?1:0);
            if(reset_match==4){reset_match=0;telemetry_reset();}
            if(b==0xa5){frame[used++]=b;reset_match=0;}continue;
        }
        if(used==1&&b!=0x5a){used=b==0xa5?1:0;continue;}
        frame[used++]=b;
        if(used==4&&(frame[3]<(frame[2]==11?8:12)||frame[3]>108||(frame[2]!=2&&frame[2]!=3&&frame[2]!=5&&frame[2]!=6&&frame[2]!=7&&frame[2]!=8&&frame[2]!=9&&frame[2]!=10&&frame[2]!=11))){used=0;continue;}
        if(used>=6&&used==(unsigned)frame[3]+6){
            unsigned n=frame[3];
            if(crc16(frame,n+4)==get16(frame+n+4)){
                uint8_t ack[17]={0xa5,0x5a,4,11};uint16_t c;
                memcpy(ack+4,frame+4,4);ack[8]=apply(ui,frame+4,frame[2],n,now);
                ack[9]=(uint8_t)ui->state.graph_page;
                ack[10]=(uint8_t)(ui->graph_running|(ui->state.page==HMI_PAGE_GRAPHS?2u:0u));
                memcpy(ack+11,&ui->graph_time_ms,4);c=crc16(ack,15);
                ack[15]=(uint8_t)c;ack[16]=(uint8_t)(c>>8);telemetry_send(ack,17);
            }
            used=0;
        }
    }
    if(online&&now-last_rx>10000u){
        HmiState old=ui->state;online=0;telemetry_notice(100,0);
        ui->state.inverter_enabled=ui->state.regulation_active=0;
        ui->state.telemetry_flags&=(uint8_t)~1u;
        ui->state.telemetry_flags|=64u;
        ui->state.telemetry_flags&=(uint8_t)~24u;
        hmi_invalidate((HmiRect){8,3,300,137});
        hmi_diff_and_invalidate(&old,&ui->state);
    }
}
