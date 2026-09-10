#include "hmi.h"
#include "hmi_gfx.h"
#include "hmi_storage.h"
#if HMI_LITE
#include "hmi_assets_layout_lite.h"
#else
#include "hmi_assets_layout_full.h"
#endif

#include <stddef.h>
#include <string.h>

enum { OP_END, OP_CLIP, OP_FILL, OP_GRADIENT, OP_LINE, OP_LINE_AA,
       OP_LINE_WIDTH, OP_RECT, OP_FILL_ROUND, OP_ROUND, OP_CIRCLE,
       OP_CIRCLE_WIDTH, OP_TRIANGLE, OP_TEXT };

static int16_t s16(uint16_t value) { return (int16_t)value; }

#if HMI_LITE
/* Band rejection must not keep fetching the same scattered resource tables.
 * Cache only the current scene's block ranges, never a framebuffer. */
#define SCENE_BLOCK_CACHE 192u
static uint16_t scene_blocks[SCENE_BLOCK_CACHE][4];
static HmiSceneId cached_scene=HMI_SCENE_COUNT;
static uint16_t primitive_cache[32][12]; /* id+1, then decoded words */
void hmi_scene_cache_reset(void){
    cached_scene=HMI_SCENE_COUNT;memset(primitive_cache,0,sizeof(primitive_cache));
}
#endif

#if HMI_EXTERNAL_ASSETS
void hmi_scene_cache_reset(void){} /* Raster UI has no decoded geometry cache. */
#endif

static uint16_t read_varint(const uint8_t **stream) {
    uint16_t value=0;unsigned shift=0;uint8_t b;
    do { b=hmi_read_u8((*stream)++);value|=(uint16_t)((uint16_t)(b&127u)<<shift);shift+=7u; } while(b&128u);
    return value;
}

/* A shared template plus overrides reconstructs one primitive on the stack.
 * No scene decompression buffer and no heap allocation are needed. */
static void decode_primitive(uint16_t id,uint16_t words[11]) {
#if HMI_LITE
    uint16_t *entry=primitive_cache[id%32u];
    if(entry[0]==id+1u){memcpy(words,entry+1,22);return;}
#endif
    const uint8_t *o=hmi_generated_primitive_offsets+(uint32_t)id*3u;
    uint32_t offset=(uint32_t)hmi_read_u8(o)|((uint32_t)hmi_read_u8(o+1)<<8)|((uint32_t)hmi_read_u8(o+2)<<16);
    const uint8_t *p=hmi_generated_primitive_data+offset;
    uint16_t tid=read_varint(&p),mask=read_varint(&p);unsigned i;
    hmi_read_copy(words,hmi_generated_templates+(uint32_t)tid*11u,11u*sizeof(uint16_t));
    for(i=0;i<11u;i++)if(mask&(1u<<i)){
        uint8_t v=hmi_read_u8(p++);
        if(v<255u)words[i]=hmi_read_u16(hmi_generated_values+v);
        else {words[i]=(uint16_t)((uint16_t)hmi_read_u8(p)|((uint16_t)hmi_read_u8(p+1)<<8));p+=2;}
    }
    if(words[0]==OP_LINE||words[0]==OP_LINE_AA||words[0]==OP_LINE_WIDTH){
        int y0=s16(words[5]),y1=y0+s16(words[7]);
        words[6]=(uint16_t)(words[4]+s16(words[6]));words[7]=(uint16_t)y1;
        words[2]=(uint16_t)(s16(words[2])+(y0<y1?y0:y1));
        words[3]=(uint16_t)(words[3]+(y0<y1?y1-y0:y0-y1));
    }
#if HMI_LITE
    entry[0]=(uint16_t)(id+1u);memcpy(entry+1,words,22);
#endif
}

void hmi_draw_generated_scene(HmiSceneId id,uint16_t band_y,uint16_t band_h) {
    const HmiGeneratedScene *scene;
    if(ui_draw_raster_scene((unsigned)id))return;
    uint16_t block_at;
    if ((unsigned)id >= (unsigned)HMI_SCENE_COUNT) id = HMI_SCENE_HOME_READY;
    scene = &hmi_generated_scenes[id];
#if HMI_LITE
    if(cached_scene!=id&&scene->block_count<=SCENE_BLOCK_CACHE){
        for(block_at=0;block_at<scene->block_count;block_at++){
            uint16_t bid=hmi_read_u16(hmi_generated_scene_blocks+scene->first_block+block_at);
            hmi_read_copy(scene_blocks[block_at],hmi_generated_block_bounds+(uint32_t)bid*2u,4);
            hmi_read_copy(scene_blocks[block_at]+2,hmi_generated_block_offsets+bid,4);
        }
        cached_scene=id;
    }
#endif
    for(block_at=0;block_at<scene->block_count;block_at++){
        uint16_t first,end;
#if HMI_LITE
        if(scene->block_count<=SCENE_BLOCK_CACHE){
            const uint16_t *b=scene_blocks[block_at];
            if(b[1]<=band_y||b[0]>=(uint32_t)band_y+band_h)continue;
            first=b[2];end=b[3];
        }else
#endif
        {
        uint16_t block_id=hmi_read_u16(hmi_generated_scene_blocks+scene->first_block+block_at);
        const uint16_t *bounds=hmi_generated_block_bounds+(uint32_t)block_id*2u;
        if(hmi_read_u16(bounds+1)<=band_y||hmi_read_u16(bounds)>=(uint32_t)band_y+band_h)continue;
        first=hmi_read_u16(hmi_generated_block_offsets+block_id);
        end=hmi_read_u16(hmi_generated_block_offsets+block_id+1u);
        }
        for(uint16_t at=first;at<end;at++){
        uint16_t primitive_id=hmi_read_u16(hmi_generated_block_commands+at);
        uint16_t decoded[11];const uint16_t *p=decoded;
        decode_primitive(primitive_id,decoded);
        if(ui_live_graph_background()&&id>=HMI_SCENE_GRAPHS_DQD&&id<=HMI_SCENE_GRAPHS_SPEED){
            /* Live graph background preserves every original frame, grid,
             * label and gradient, omitting only demonstration data. */
            if(decoded[0]>=OP_LINE&&decoded[0]<=OP_LINE_WIDTH&&
               decoded[4]>=30&&decoded[4]<=309&&decoded[6]>=30&&decoded[6]<=309&&
               decoded[5]>=106&&decoded[5]<=283&&decoded[7]>=106&&decoded[7]<=283&&
               decoded[decoded[0]==OP_LINE_WIDTH?9:8]!=12841u&&
               decoded[decoded[0]==OP_LINE_WIDTH?9:8]!=12809u)continue;
            if(decoded[0]==OP_TEXT){
                unsigned baseline=decoded[5];
                uint8_t c=hmi_read_u8(hmi_generated_strings+decoded[8]);
                if((baseline>=109u&&baseline<=297u&&(decoded[4]==9u||decoded[4]==288u||baseline==297u))||
                   (baseline==54u&&(decoded[4]==62u||decoded[4]==156u||decoded[4]==262u)))continue;
                if(baseline==377u||((baseline==394u||baseline==405u||baseline==416u||baseline==427u)&&
                    ((c>='0'&&c<='9')||c=='-'||c=='+')))continue;
            }
        }
        uint16_t code=*p++,clip_id=*p++,by=*p++,bh=*p++;int draw;
        draw=((uint32_t)by+bh>(uint32_t)band_y&&by<(uint32_t)band_y+band_h);
        if(draw){uint16_t c[4];hmi_read_copy(c,hmi_generated_clip_rects+(uint32_t)clip_id*4u,sizeof(c));ui_set_clip(s16(c[0]),s16(c[1]),s16(c[2]),s16(c[3]));}
        switch (code) {
        case OP_FILL: if(draw)ui_fill_rect(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),p[4]);p+=5;break;
        case OP_GRADIENT: if(draw)ui_fill_gradient_v(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),p[4],p[5]);p+=6;break;
        case OP_LINE: if(draw)ui_line(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),p[4]);p+=5;break;
        case OP_LINE_AA: if(draw)ui_line_aa(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),p[4]);p+=5;break;
        case OP_LINE_WIDTH: if(draw)ui_line_width(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),s16(p[4]),p[5]);p+=6;break;
        case OP_RECT: if(draw)ui_rect(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),p[4]);p+=5;break;
        case OP_FILL_ROUND: if(draw)ui_fill_round_rect(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),s16(p[4]),p[5]);p+=6;break;
        case OP_ROUND: if(draw)ui_round_rect(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),s16(p[4]),p[5]);p+=6;break;
        case OP_CIRCLE: if(draw)ui_circle(s16(p[0]),s16(p[1]),s16(p[2]),p[3],s16(p[4]));p+=5;break;
        case OP_CIRCLE_WIDTH: if(draw)ui_circle_width(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),p[4]);p+=5;break;
        case OP_TRIANGLE: if(draw)ui_fill_triangle(s16(p[0]),s16(p[1]),s16(p[2]),s16(p[3]),s16(p[4]),s16(p[5]),p[6]);p+=7;break;
        case OP_TEXT: if(draw&&!ui_static_text_hidden(s16(p[0]),s16(p[1]))){ui_text_fit_cstr(s16(p[0]),s16(p[1]),s16(p[2]),p[3],
                                              (const char *)(hmi_generated_strings+p[4]),s16(p[5]),s16(p[6]));}p+=7;break;
        default: return;
        }
        }
    }

}

void hmi_state_defaults(HmiState *s) {
    if (!s) return;
    memset(s,0,sizeof(*s));
    s->page=HMI_PAGE_HOME;s->drive_mode=HMI_DRIVE_UF;s->power_state=HMI_POWER_READY;
    s->graph_page=HMI_GRAPH_POWER;s->param_section=HMI_PARAM_SYSTEM;s->dialog=HMI_DIALOG_NONE;
    s->scene_override=HMI_SCENE_COUNT;s->clock="14:32";s->run_state_label="ГОТОВ";
    s->sensor_mask=255;s->mains_voltage=220.0f;s->mains_frequency=50.0f;s->dc_bus_voltage=311.0f;
    s->modulation_set=65.0f;s->modulation_actual=65.0f;s->rotation_set=25.0f;
    s->rotation_actual=25.0f;s->current_limit_set=125.0f;
    s->motor_load=-1.0f;
    s->temperature_rectifier=41.0f;s->temperature_precharge=39.0f;s->temperature_dc=38.0f;
    s->temperature_inverter=43.0f;s->temperature_motor=41.0f;s->rotor_frequency=25.0f;
    s->stator_frequency=50.0f;
    s->control_labels[0]="МОДУЛЯЦИЯ";s->control_labels[1]="СКОРОСТЬ РОТОРА";
    s->control_labels[2]="ОГРАНИЧЕНИЕ ТОКА";
}

static HmiSceneId home_scene(const HmiState *s) {
    if (s->power_state==HMI_POWER_OFF) return HMI_SCENE_HOME_OFF;
    if (s->power_state==HMI_POWER_CHARGE) return HMI_SCENE_HOME_CHARGE;
    if (s->power_state==HMI_POWER_FAULT) return (s->telemetry_flags&128u)?HMI_SCENE_HOME_OFF:HMI_SCENE_HOME_FAULT;
    if (s->power_state==HMI_POWER_REGULATING) return HMI_SCENE_HOME_REGULATING;
    if (s->power_state==HMI_POWER_READY){
        if(s->selected_control==1u)return HMI_SCENE_HOME_SELECT_FREQ;
        if(s->selected_control==2u)return HMI_SCENE_HOME_SELECT_LIMIT;
        return HMI_SCENE_HOME_READY;
    }
    if (s->drive_mode==HMI_DRIVE_SF) return HMI_SCENE_HOME_SF;
    if (s->drive_mode==HMI_DRIVE_VF) return HMI_SCENE_HOME_VF;
    return s->setpoint_matches?HMI_SCENE_HOME_TARGET_EQUAL:HMI_SCENE_HOME_RUN;
}

HmiSceneId hmi_scene_for_state(const HmiState *s) {
    if (!s) return HMI_SCENE_HOME_READY;
    if ((unsigned)s->scene_override < (unsigned)HMI_SCENE_COUNT) return s->scene_override;
    if (s->dialog==HMI_DIALOG_KEYPAD)return HMI_SCENE_DIALOG_KEYPAD;
    if (s->dialog==HMI_DIALOG_FILTER)return HMI_SCENE_DIALOG_FILTER;
    if (s->dialog==HMI_DIALOG_CONFIRM)return HMI_SCENE_DIALOG_CONFIRM;
    if (s->dialog==HMI_DIALOG_SENSOR_DISPLAY)return HMI_SCENE_DIALOG_SENSOR_DISPLAY;
    if (s->dialog==HMI_DIALOG_SENSOR_SCHEMES)return HMI_SCENE_DIALOG_SENSOR_SCHEMES;
    if (s->dialog==HMI_DIALOG_ROM)return HMI_SCENE_DIALOG_ROM;
    if (s->dialog==HMI_DIALOG_PANEL_CONTROL)return HMI_SCENE_DIALOG_PANEL_CONTROL;
    if (s->dialog==HMI_DIALOG_PANEL_OUTPUT)return HMI_SCENE_DIALOG_PANEL_OUTPUT;
    if (s->dialog==HMI_DIALOG_PANEL_MOTOR)return HMI_SCENE_DIALOG_PANEL_MOTOR;
    if (s->dialog==HMI_DIALOG_AXIS)return HMI_SCENE_DIALOG_AXIS;
    if (s->dialog==HMI_DIALOG_TIME)return HMI_SCENE_DIALOG_TIME;
    if (s->dialog==HMI_DIALOG_HELP) {
        if(s->page==HMI_PAGE_GRAPHS)return HMI_SCENE_HELP_GRAPHS;
        if(s->page==HMI_PAGE_PARAMETERS)return HMI_SCENE_HELP_PARAMS;
        if(s->page==HMI_PAGE_JOURNAL)return HMI_SCENE_HELP_JOURNAL;
        return HMI_SCENE_HELP_HOME;
    }
    if(s->page==HMI_PAGE_HOME)return home_scene(s);
    if(s->page==HMI_PAGE_JOURNAL)return s->journal_page>1?HMI_SCENE_JOURNAL_PAGE2:HMI_SCENE_JOURNAL_PAGE1;
    if(s->page==HMI_PAGE_GRAPHS){
        if(s->graph_page==HMI_GRAPH_POWER)return HMI_SCENE_GRAPHS_POWER;
        if(s->graph_page==HMI_GRAPH_LIMIT)return HMI_SCENE_GRAPHS_LIMIT;
        if(s->graph_page==HMI_GRAPH_D)return HMI_SCENE_GRAPHS_DQD;
        if(s->graph_page==HMI_GRAPH_Q)return HMI_SCENE_GRAPHS_DQQ;
        if(s->drive_mode==HMI_DRIVE_SF)return HMI_SCENE_GRAPHS_SPEED_SF;
        if(s->drive_mode==HMI_DRIVE_VF)return HMI_SCENE_GRAPHS_SPEED_VF;
        return HMI_SCENE_GRAPHS_SPEED;
    }
    switch(s->param_section){
    case HMI_PARAM_INVERTER:return HMI_SCENE_PARAMS_INVERTER;
    case HMI_PARAM_EQUIVALENT:case HMI_PARAM_MOTOR:return HMI_SCENE_PARAMS_MOTOR;
    case HMI_PARAM_NETWORK:case HMI_PARAM_TEMPERATURE:case HMI_PARAM_PROTECTIONS:return HMI_SCENE_PARAMS_PROTECTIONS;
    case HMI_PARAM_COMMUNICATION:return HMI_SCENE_PARAMS_COMMUNICATION;
    case HMI_PARAM_CALIBRATION:return HMI_SCENE_PARAMS_CALIBRATION;
    case HMI_PARAM_AUTO:return HMI_SCENE_PARAMS_AUTO;
    case HMI_PARAM_AUTO_RUNNING:return HMI_SCENE_PARAMS_AUTO_RUNNING;
    case HMI_PARAM_AUTO_DONE:return HMI_SCENE_PARAMS_AUTO_DONE;
    default:return HMI_SCENE_PARAMS_SYSTEM;
    }
}
