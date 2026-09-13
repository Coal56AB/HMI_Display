#include "hmi_ui.h"
#include "hmi_gfx.h"
#include <stdio.h>
#include <string.h>
#include "hmi_storage.h"
#if HMI_EXTERNAL_ASSETS
#include "hmi_help_flash.h"
static const char *help_line(unsigned page,unsigned line){
    static char text[128];hmi_read_copy(text,(const void *)(uintptr_t)(HMI_ASSET_BASE+help_offsets[page][line]),help_lengths[page][line]);return text;
}
#else
#include "hmi_help_text.h"
static const char *help_line(unsigned page,unsigned line){return help_pages[page][line];}
#endif

static unsigned help_bounds(const HmiUi *ui,unsigned wanted,unsigned *first,unsigned *last){
    unsigned context=(unsigned)ui->state.page,n=help_counts[context<4?context:0];
    unsigned context_page=context<4?context:0;
    unsigned at=0,page=0,begin=0,height=0;
    *first=*last=0;
    while(at<n){
        unsigned end=at;while(end<n&&help_line(context_page,end)[0])end++;
        unsigned h=(end-at)*15u+12u;
        if(height&&height+h>290u){if(page==wanted){*first=begin;*last=at;}page++;begin=at;height=0;}
        height+=h;at=end<n?end+1:end;
    }
    if(page==wanted){*first=begin;*last=n;}return page+1;
}
static int touch_card(const HmiUi *ui,int x,int y){
    if(ui->state.dialog!=HMI_DIALOG_NONE)return 0;
    if(ui->state.page==HMI_PAGE_GRAPHS&&x>=7&&x<312&&y>=101&&y<292)return HMI_DIALOG_AXIS;
    if(ui->state.page!=HMI_PAGE_HOME)return 0;
    if(x<8||x>=312)return 0;
    if(ui->state.journal_warning&&y>=140&&y<164)return 0;
    if(y>=35&&y<140)return HMI_DIALOG_SENSOR_DISPLAY;
    if(y>=149&&y<261)return HMI_DIALOG_PANEL_CONTROL;
    if(y>=265&&y<428)return x<156?HMI_DIALOG_PANEL_OUTPUT:(x>=164?HMI_DIALOG_PANEL_MOTOR:0);
    return 0;
}

typedef struct {
    const char *name,*unit;
    float initial,minimum,maximum;
    uint16_t y;
    uint8_t section,decimals;
} ValueDef;
#define SPECIAL 255u
#if HMI_EXTERNAL_ASSETS
#define NAME(text) NULL
#define UNIT(text) NULL
#else
#define NAME(text) text
#define UNIT(text) text
#endif
static const ValueDef values[HMI_VALUE_COUNT]={
    {NAME("МОДУЛЯЦИЯ"),UNIT("%"),65,0,100,0,SPECIAL,1},
    {NAME("ЧАСТОТА РОТОРА"),UNIT("Гц"),25,0,1000,0,SPECIAL,2},
    {NAME("ОГРАНИЧЕНИЕ ТОКА"),UNIT("%"),125,0,300,0,SPECIAL,1},
    {NAME("НОМ. НАПРЯЖЕНИЕ СЕТИ"),UNIT("В"),220,100,260,92,HMI_PARAM_SYSTEM,0},
    {NAME("НОМ. ЧАСТОТА СЕТИ"),UNIT("Гц"),50,40,70,126,HMI_PARAM_SYSTEM,1},
    {NAME("ЯРКОСТЬ ЭКРАНА"),UNIT("%"),80,0,100,161,HMI_PARAM_SYSTEM,0},
    {NAME("ЧАСТОТА ШИМ"),UNIT("кГц"),10,1,100,155,HMI_PARAM_INVERTER,1},
    {NAME("МЁРТВОЕ ВРЕМЯ"),UNIT("мкс"),3,0,100,189,HMI_PARAM_INVERTER,1},
    {NAME("МАКСИМАЛЬНАЯ ЧАСТОТА"),UNIT("Гц"),200,1,1000,224,HMI_PARAM_INVERTER,0},
    {NAME("МОЩНОСТЬ ДВИГАТЕЛЯ"),UNIT("кВт"),5.5f,0.01f,10000,155,HMI_PARAM_MOTOR,2},
    {NAME("НАПРЯЖЕНИЕ ДВИГАТЕЛЯ"),UNIT("В"),380,1,10000,189,HMI_PARAM_MOTOR,0},
    {NAME("ТОК ДВИГАТЕЛЯ"),UNIT("А"),11.2f,0.01f,10000,223,HMI_PARAM_MOTOR,2},
    {NAME("ЧАСТОТА ДВИГАТЕЛЯ"),UNIT("Гц"),50,1,1000,257,HMI_PARAM_MOTOR,1},
    {NAME("СКОРОСТЬ ДВИГАТЕЛЯ"),UNIT("об/мин"),1450,1,100000,292,HMI_PARAM_MOTOR,0},
    {NAME("ПАРЫ ПОЛЮСОВ"),UNIT(""),2,1,100,326,HMI_PARAM_MOTOR,0},
    {NAME("КПД"),UNIT("%"),89,1,100,360,HMI_PARAM_MOTOR,1},
    {NAME("COS Ф"),UNIT(""),0.86f,0,1,394,HMI_PARAM_MOTOR,2},
    {NAME("МАКСИМАЛЬНЫЙ ТОК"),UNIT("о.е."),1.25f,0.01f,10,92,HMI_PARAM_PROTECTIONS,2},
    {NAME("ПЕРЕНАПРЯЖЕНИЕ DC"),UNIT("о.е."),1.25f,0.01f,10,140,HMI_PARAM_PROTECTIONS,2},
    {NAME("ПОНИЖЕННОЕ DC"),UNIT("о.е."),0.68f,0.01f,10,188,HMI_PARAM_PROTECTIONS,2},
    {NAME("МАКСИМАЛЬНАЯ СКОРОСТЬ"),UNIT("о.е."),1.1f,0.01f,10,236,HMI_PARAM_PROTECTIONS,2},
    {NAME("МАКСИМАЛЬНАЯ МОЩНОСТЬ"),UNIT("о.е."),1.2f,0.01f,10,284,HMI_PARAM_PROTECTIONS,2},
    {NAME("ТЕМПЕРАТУРА ИНВЕРТОРА"),UNIT("о.е."),1,0.01f,10,333,HMI_PARAM_TEMPERATURE,2},
    {NAME("ТЕМПЕРАТУРА ДВИГАТЕЛЯ"),UNIT("о.е."),1,0.01f,10,381,HMI_PARAM_TEMPERATURE,2},
    {NAME("АДРЕС MODBUS"),UNIT(""),1,1,247,126,HMI_PARAM_COMMUNICATION,0},
    {NAME("СКОРОСТЬ ОБМЕНА"),UNIT("бит/с"),115200,1200,1000000,160,HMI_PARAM_COMMUNICATION,0},
    {NAME("ТАЙМ-АУТ"),UNIT("мс"),1000,1,60000,228,HMI_PARAM_COMMUNICATION,0},
    {NAME("КОЭФФИЦИЕНТ A"),UNIT(""),1,0.001f,1000,92,HMI_PARAM_CALIBRATION,3},
    {NAME("КОЭФФИЦИЕНТ B"),UNIT(""),1,0.001f,1000,126,HMI_PARAM_CALIBRATION,3},
    {NAME("КОЭФФИЦИЕНТ C"),UNIT(""),1,0.001f,1000,160,HMI_PARAM_CALIBRATION,3},
    {NAME("КОЭФФИЦИЕНТ DC"),UNIT(""),1,0.001f,1000,194,HMI_PARAM_CALIBRATION,3},
    {NAME("СМЕЩЕНИЕ ДАТЧИКА"),UNIT("А"),0,-1000,1000,228,HMI_PARAM_CALIBRATION,3},
    {NAME("ТОК: МИНИМУМ"),UNIT("А"),-6,-10000,10000,0,SPECIAL,2},
    {NAME("ТОК: МАКСИМУМ"),UNIT("А"),6,-10000,10000,0,SPECIAL,2},
    {NAME("НАПРЯЖЕНИЕ: МИНИМУМ"),UNIT("В"),0,-10000,10000,0,SPECIAL,2},
    {NAME("НАПРЯЖЕНИЕ: МАКСИМУМ"),UNIT("В"),360,-10000,10000,0,SPECIAL,2},
    {NAME("МОДУЛЯЦИЯ: МИНИМУМ"),UNIT("%"),0,0,100,0,SPECIAL,1},
    {NAME("МОДУЛЯЦИЯ: МАКСИМУМ"),UNIT("%"),100,0,100,0,SPECIAL,1},
    {NAME("ВРАЩЕНИЕ: МИНИМУМ"),UNIT("Гц"),0,0,1000,0,SPECIAL,2},
    {NAME("ВРАЩЕНИЕ: МАКСИМУМ"),UNIT("Гц"),100,0,1000,0,SPECIAL,2},
    {NAME("ТОК: МИНИМУМ"),UNIT("%"),10,0,300,0,SPECIAL,1},
    {NAME("ТОК: МАКСИМУМ"),UNIT("%"),300,0,300,0,SPECIAL,1},
    {NAME("УДАЛИТЬ ДО ДАТЫ ГГММДД"),UNIT("0 = всё"),260101,0,991231,0,SPECIAL,0},
    {NAME("RS — СТАТОР"),UNIT("Ом"),0.84f,0.001f,1000,144,HMI_PARAM_EQUIVALENT,3},
    {NAME("RR — РОТОР"),UNIT("Ом"),0.71f,0.001f,1000,188,HMI_PARAM_EQUIVALENT,3},
    {NAME("LLS — РАССЕЯНИЕ"),UNIT("мГн"),4.3f,0.001f,10000,232,HMI_PARAM_EQUIVALENT,3},
    {NAME("LLR — РАССЕЯНИЕ"),UNIT("мГн"),4.3f,0.001f,10000,276,HMI_PARAM_EQUIVALENT,3},
    {NAME("LM — НАМАГНИЧИВАНИЕ"),UNIT("мГн"),142,0.001f,10000,320,HMI_PARAM_EQUIVALENT,3},
    {NAME("СЕТЬ U: ПРЕДУПР. НИЖЕ"),UNIT("В"),198,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ U: ПРЕДУПР. ВЫШЕ"),UNIT("В"),242,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ U: АВАРИЯ НИЖЕ"),UNIT("В"),180,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ U: АВАРИЯ ВЫШЕ"),UNIT("В"),260,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ F: ПРЕДУПР. НИЖЕ"),UNIT("Гц"),49,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ F: ПРЕДУПР. ВЫШЕ"),UNIT("Гц"),51,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ F: АВАРИЯ НИЖЕ"),UNIT("Гц"),45,0,400,0,HMI_PARAM_NETWORK,1},
    {NAME("СЕТЬ F: АВАРИЯ ВЫШЕ"),UNIT("Гц"),55,0,400,0,HMI_PARAM_NETWORK,1}
};

#if HMI_EXTERNAL_ASSETS
#include "hmi_value_flash.h"
static const char *value_text(unsigned id,unsigned field){
    static char text[2][96];hmi_read_copy(text[field],(const void *)(uintptr_t)(HMI_ASSET_BASE+value_offsets[id][field]),value_lengths[id][field]);return text[field];
}
#else
static const char *value_text(unsigned id,unsigned field){return field?values[id].unit:values[id].name;}
#endif

static int inside(HmiRect r,int x,int y){return x>=r.x&&y>=r.y&&x<(int)r.x+r.width&&y<(int)r.y+r.height;}
static int value_visible(const HmiUi *ui,unsigned id){
    if(id==HMI_VALUE_BRIGHTNESS)return 0;
    if(ui->state.param_section==HMI_PARAM_AUTO)return id>=HMI_VALUE_MOTOR_POWER_KW&&id<=HMI_VALUE_POWER_FACTOR;
    return values[id].section==ui->state.param_section;
}
static HmiRect value_rect(const HmiUi *ui,unsigned id){
    unsigned row=0;for(unsigned i=3;i<id;i++)if(value_visible(ui,i))row++;
    unsigned section=ui->state.param_section;
    unsigned top=(section==HMI_PARAM_MOTOR||section==HMI_PARAM_AUTO||section==HMI_PARAM_INVERTER||section==HMI_PARAM_COMMUNICATION)?127:96;
    return (HmiRect){112,(uint16_t)(top+row*30),200,28};
}

static int hit(HmiHit *h,int x,int y,int rx,int ry,int w,int height,HmiAction a,int arg){
    HmiRect r={(uint16_t)rx,(uint16_t)ry,(uint16_t)w,(uint16_t)height};
    if(!inside(r,x,y))return 0;
    h->rect=r;h->action=a;h->argument=(int16_t)arg;return 1;
}
#define HIT(rx,ry,w,h,a,arg) if(hit(out,x,y,rx,ry,w,h,a,arg))return 1

static int settings_group(HmiDialog d){
    switch(d){
    case HMI_DIALOG_PANEL_CONTROL:return 0;
    case HMI_DIALOG_PANEL_OUTPUT:return 1;
    case HMI_DIALOG_PANEL_MOTOR:return 2;
    case HMI_DIALOG_SENSOR_DISPLAY:return 3;
    case HMI_DIALOG_FILTER:return 4;
    case HMI_DIALOG_AXIS:return 5;
    default:return -1;
    }
}

int hmi_ui_hit_test(const HmiUi *ui,int16_t x,int16_t y,HmiHit *out){
    unsigned i;HmiDialog d;HmiSceneId scene;
    if(!ui||!out||x<0||x>=320||y<0||y>=480)return 0;
    scene=hmi_scene_for_state(&ui->state);
    for(i=0;i<ui->region_count;i++)if(ui->regions[i].scene==scene||ui->regions[i].scene==HMI_SCENE_COUNT){
        if(inside(ui->regions[i].hit.rect,x,y)){*out=ui->regions[i].hit;return 1;}
    }
    if(ui->state.journal_warning==2)return 0;
    d=ui->state.dialog;
    /* Modal dialogs consume the background, including the bottom navigation. */
    if(d!=HMI_DIALOG_NONE){
        if(d==HMI_DIALOG_KEYPAD){
            static const char keys[]="789456123-0.";
            for(i=0;i<12;i++){HIT(21+(int)(i%3)*95,154+(int)(i/3)*44,90,40,HMI_ACTION_KEY,keys[i]);}
            HIT(21,330,137,38,HMI_ACTION_KEY,'C');HIT(163,330,137,38,HMI_ACTION_KEY,'B');
            HIT(21,373,137,35,HMI_ACTION_CANCEL,0);HIT(163,373,137,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_CONFIRM){
            if(ui->parameters[HMI_VALUE_JOURNAL_DATE]<0){
                HIT(20,192,278,31,HMI_ACTION_CUSTOM,611);
                HIT(20,228,278,31,HMI_ACTION_CUSTOM,612);
                HIT(20,264,278,31,HMI_ACTION_CANCEL,0);return 0;
            }
            HIT(20,234,136,35,HMI_ACTION_CANCEL,0);HIT(162,234,136,35,HMI_ACTION_JOURNAL_CLEAR,0);
        }else if(d==HMI_DIALOG_FILTER){
            for(i=0;i<3;i++){HIT(20,171+(int)i*40,270,33,HMI_ACTION_FILTER,i);}
            HIT(20,286,136,35,HMI_ACTION_CANCEL,0);HIT(162,286,136,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_TIME){
            static const char keys[]="123456789B0C";
            HIT(51,122,103,43,HMI_ACTION_TIME_PART,0);HIT(166,122,103,43,HMI_ACTION_TIME_PART,1);
            for(i=0;i<12;i++){HIT(21+(int)(i%3)*94,173+(int)(i/3)*40,90,36,HMI_ACTION_KEY,keys[i]);}
            HIT(21,349,137,35,HMI_ACTION_CANCEL,0);HIT(163,349,137,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_ROM){
            static const char keys[]="789456123";
            for(i=0;i<9;i++){HIT(20+(int)(i%3)*56,178+(int)(i/3)*35,52,31,HMI_ACTION_KEY,keys[i]);}
            HIT(20,284,164,32,HMI_ACTION_KEY,'0');
            for(i=0;i<6;i++){HIT(190+(int)(i%2)*55,178+(int)(i/2)*47,50,43,HMI_ACTION_KEY,'A'+i);}
            HIT(20,322,136,32,HMI_ACTION_KEY,'X');HIT(162,322,136,32,HMI_ACTION_KEY,'<');
            HIT(20,358,136,35,HMI_ACTION_CANCEL,0);HIT(162,358,136,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_SENSOR_SCHEMES){
            HIT(20,69,140,36,HMI_ACTION_DIALOG,HMI_DIALOG_SENSOR_DISPLAY);
            for(i=0;i<5;i++){HIT(110,132+(int)i*47,185,34,HMI_ACTION_ROM,i);}
            HIT(20,385,136,33,HMI_ACTION_CLOSE,0);HIT(162,385,136,33,HMI_ACTION_CLOSE,0);
        }else if(d==HMI_DIALOG_SENSOR_DISPLAY){
            HIT(160,69,140,36,HMI_ACTION_DIALOG,HMI_DIALOG_SENSOR_SCHEMES);
            for(i=0;i<8;i++){HIT(247,114+(int)i*33,52,29,HMI_ACTION_TOGGLE,i);}
            HIT(21,385,137,33,HMI_ACTION_CANCEL,0);HIT(163,385,137,33,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_AXIS){
            HIT(205,145,86,32,HMI_ACTION_CUSTOM,100);HIT(205,241,86,32,HMI_ACTION_CUSTOM,101);
            HIT(61,184,95,32,HMI_ACTION_EDIT,HMI_VALUE_AXIS_I_MIN);HIT(196,184,95,32,HMI_ACTION_EDIT,HMI_VALUE_AXIS_I_MAX);
            HIT(61,280,95,32,HMI_ACTION_EDIT,HMI_VALUE_AXIS_U_MIN);HIT(196,280,95,32,HMI_ACTION_EDIT,HMI_VALUE_AXIS_U_MAX);
            HIT(21,348,137,35,HMI_ACTION_CANCEL,0);HIT(163,348,137,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_PANEL_CONTROL){
            for(i=0;i<3;i++){
                HIT(146,111+(int)i*79,43,31,HMI_ACTION_TOGGLE,i);
                HIT(193,111+(int)i*79,106,31,HMI_ACTION_CUSTOM,110+i);
                HIT(53,151+(int)i*79,103,31,HMI_ACTION_EDIT,HMI_VALUE_MOD_MIN+i*2);
                HIT(196,151+(int)i*79,103,31,HMI_ACTION_EDIT,HMI_VALUE_MOD_MAX+i*2);
            }
            HIT(21,362,137,35,HMI_ACTION_CANCEL,0);HIT(163,362,137,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_PANEL_OUTPUT||d==HMI_DIALOG_PANEL_MOTOR){
            for(i=0;i<4;i++){
                HIT(146,128+(int)i*54,43,31,HMI_ACTION_TOGGLE,i);
                HIT(193,128+(int)i*54,106,31,HMI_ACTION_CUSTOM,120+i);
            }
            HIT(21,352,137,35,HMI_ACTION_CANCEL,0);HIT(163,352,137,35,HMI_ACTION_APPLY,0);
        }else if(d==HMI_DIALOG_HELP){
            HIT(16,402,62,32,HMI_ACTION_CUSTOM,500);
            HIT(242,402,62,32,HMI_ACTION_CUSTOM,501);
            HIT(91,402,138,32,HMI_ACTION_CLOSE,0);
        }
        return 0;
    }
    for(i=0;i<4;i++){HIT(7+(int)i*78,434,74,41,HMI_ACTION_PAGE,i);}
    HIT(258,3,54,29,HMI_ACTION_DIALOG,HMI_DIALOG_TIME);
    HIT(112,1,138,32,HMI_ACTION_DIALOG,HMI_DIALOG_HELP);
    if(ui->state.page!=HMI_PAGE_HOME)for(i=0;i<3;i++){HIT(1+(int)i*106,35,106,29,HMI_ACTION_SELECT_CONTROL,i);}
    if(ui->state.page==HMI_PAGE_HOME){
        if(ui->state.journal_warning) {HIT(8,140,304,25,HMI_ACTION_CUSTOM,610);}
        HIT(8,35,304,105,HMI_ACTION_DIALOG,HMI_DIALOG_SENSOR_DISPLAY);
        for(i=0;i<3;i++){HIT(139,175+(int)i*27,80,26,HMI_ACTION_EDIT,i);}
        HIT(8,149,304,112,HMI_ACTION_DIALOG,HMI_DIALOG_PANEL_CONTROL);
        HIT(8,265,148,163,HMI_ACTION_DIALOG,HMI_DIALOG_PANEL_OUTPUT);
        HIT(164,265,148,163,HMI_ACTION_DIALOG,HMI_DIALOG_PANEL_MOTOR);
    }else if(ui->state.page==HMI_PAGE_PARAMETERS){
        static const HmiParamSection sections[]={HMI_PARAM_SYSTEM,HMI_PARAM_INVERTER,HMI_PARAM_MOTOR,HMI_PARAM_PROTECTIONS,HMI_PARAM_COMMUNICATION,HMI_PARAM_CALIBRATION,HMI_PARAM_AUTO};
        for(i=0;i<7;i++){HIT(8,66+(int)i*42,92,38,HMI_ACTION_SECTION,sections[i]);}
        if(ui->state.param_section==HMI_PARAM_MOTOR){HIT(112,394,200,34,HMI_ACTION_SECTION,HMI_PARAM_EQUIVALENT);}
        if(ui->state.param_section==HMI_PARAM_EQUIVALENT){HIT(112,394,200,34,HMI_ACTION_SECTION,HMI_PARAM_MOTOR);}
        if(ui->state.param_section==HMI_PARAM_PROTECTIONS){HIT(112,394,200,34,HMI_ACTION_SECTION,HMI_PARAM_TEMPERATURE);}
        if(ui->state.param_section==HMI_PARAM_TEMPERATURE){HIT(112,394,200,34,HMI_ACTION_SECTION,HMI_PARAM_NETWORK);}
        if(ui->state.param_section==HMI_PARAM_NETWORK){HIT(112,394,200,34,HMI_ACTION_SECTION,HMI_PARAM_PROTECTIONS);}
        if(ui->state.param_section==HMI_PARAM_SYSTEM){
            for(i=0;i<3;i++){HIT(115+(int)i*65,184,64,31,HMI_ACTION_MODE,i);}

        }else if(ui->state.param_section==HMI_PARAM_INVERTER){
            for(i=0;i<3;i++){HIT(115+(int)i*65,93,65,31,HMI_ACTION_CUSTOM,10+i);}
        }else if(ui->state.param_section==HMI_PARAM_MOTOR||ui->state.param_section==HMI_PARAM_AUTO){
            if(ui->state.param_section==HMI_PARAM_AUTO){HIT(112,394,200,34,HMI_ACTION_AUTO_START,0);}
            HIT(115,93,97,31,HMI_ACTION_CUSTOM,20);HIT(213,93,97,31,HMI_ACTION_CUSTOM,21);
        }else if(ui->state.param_section==HMI_PARAM_COMMUNICATION){
            HIT(112,93,200,31,HMI_ACTION_CUSTOM,30);
        }else if(ui->state.param_section==HMI_PARAM_AUTO||ui->state.param_section==HMI_PARAM_AUTO_RUNNING||ui->state.param_section==HMI_PARAM_AUTO_DONE){
            if(ui->state.param_section==HMI_PARAM_AUTO_DONE){
                HIT(113,275,200,31,HMI_ACTION_CUSTOM,300);
                HIT(113,283,200,31,HMI_ACTION_AUTO_START,0);
                HIT(113,318,200,31,HMI_ACTION_SECTION,HMI_PARAM_MOTOR);
            }else if(ui->state.param_section==HMI_PARAM_AUTO){HIT(112,394,200,34,HMI_ACTION_AUTO_START,0);}
            else {HIT(113,275,200,31,HMI_ACTION_AUTO_CANCEL,0);}
        }
        for(i=3;i<HMI_VALUE_COUNT;i++)if(value_visible(ui,i)){
            HmiRect r=value_rect(ui,i);HIT(r.x,r.y,r.width,r.height,HMI_ACTION_EDIT,i);
        }
    }else if(ui->state.page==HMI_PAGE_GRAPHS){
        for(i=0;i<3;i++){HIT(7+(int)i*102,67,101,31,HMI_ACTION_GRAPH,i);}
        HIT(7,299,42,35,HMI_ACTION_GRAPH_ZOOM,-1);HIT(117,299,43,35,HMI_ACTION_GRAPH_ZOOM,1);
        HIT(230,299,82,35,HMI_ACTION_GRAPH_RUN,0);
        HIT(162,299,30,35,HMI_ACTION_CUSTOM,620);HIT(195,299,30,35,HMI_ACTION_CUSTOM,621);
        HIT(7,101,305,191,HMI_ACTION_DIALOG,HMI_DIALOG_AXIS);
        for(i=0;i<4;i++){HIT(7+(int)i*77,339,73,88,HMI_ACTION_CUSTOM,200+i);}
    }else if(ui->state.page==HMI_PAGE_JOURNAL){
        HIT(7,67,305,32,HMI_ACTION_DIALOG,HMI_DIALOG_FILTER);
        HIT(7,394,76,35,HMI_ACTION_JOURNAL_EXPORT,0);
        HIT(87,394,45,35,HMI_ACTION_JOURNAL_PAGE,-1);HIT(185,394,45,35,HMI_ACTION_JOURNAL_PAGE,1);
        HIT(234,394,78,35,HMI_ACTION_CUSTOM,610);
    }
    return 0;
}
#undef HIT

static void event(HmiUi *ui,HmiEventType type,unsigned id,float value,uint32_t flags,const uint8_t *data){
    HmiEvent e;memset(&e,0,sizeof(e));e.sequence=ui->last_event.sequence+1u;
    e.type=type;e.id=(uint16_t)id;e.value=value;e.flags=flags;if(data)memcpy(e.data,data,8);
    ui->last_event=e;
    if(ui->event_count==HMI_UI_EVENT_CAPACITY){
        ui->event_head=(uint8_t)((ui->event_head+1u)%HMI_UI_EVENT_CAPACITY);ui->event_count--;ui->event_overflows++;
    }
    ui->events[(ui->event_head+ui->event_count)%HMI_UI_EVENT_CAPACITY]=e;ui->event_count++;
}

static void open_dialog(HmiUi *ui,HmiDialog d){
    int group=settings_group(d);ui->state.dialog=d;ui->return_dialog=HMI_DIALOG_NONE;
    ui->edit_error=0;if(group>=0)ui->draft_flags=ui->settings[group];
    memcpy(ui->draft_limits,ui->parameters+HMI_VALUE_AXIS_I_MIN,sizeof(ui->draft_limits));
    if(group>=0&&group<3)memcpy(ui->draft_units,ui->units[group],4);
    if(d==HMI_DIALOG_TIME){memcpy(ui->edit,ui->clock,6);ui->edit_length=0;ui->edit_time_part=0;}
}

static float current_value(const HmiUi *ui,unsigned id){
    if(id==HMI_VALUE_MODULATION)return ui->state.modulation_set;
    if(id==HMI_VALUE_ROTATION_HZ)return ui->state.rotation_set;
    if(id==HMI_VALUE_CURRENT_LIMIT)return ui->state.current_limit_set;
    if(id>=HMI_VALUE_AXIS_I_MIN&&id<HMI_VALUE_JOURNAL_DATE)return ui->draft_limits[id-HMI_VALUE_AXIS_I_MIN];
    return ui->parameters[id];
}

static void edit_value(HmiUi *ui,unsigned id){
    if(id>=HMI_VALUE_COUNT)return;
    ui->return_dialog=ui->state.dialog;ui->state.dialog=HMI_DIALOG_KEYPAD;ui->edit_parameter=(uint16_t)id;
    (void)snprintf(ui->edit,sizeof(ui->edit),"%.*f",values[id].decimals,(double)current_value(ui,id));
    ui->edit_length=(uint8_t)strlen(ui->edit);ui->edit_fresh=1;ui->edit_error=0;
}

static int parse_number(const char *s,float *result){
    float n=0,scale=1;int negative=0,dot=0,digits=0;
    if(*s=='-'){negative=1;s++;}
    for(;*s;s++){
        if(*s=='.'&&!dot){dot=1;continue;}
        if(*s<'0'||*s>'9')return 0;
        digits++;if(dot){scale*=0.1f;n+=(*s-'0')*scale;}else n=n*10+(*s-'0');
    }
    *result=negative?-n:n;return digits>0;
}

static void key(HmiUi *ui,int ch){
    HmiDialog d=ui->state.dialog;ui->edit_error=0;
    if(d==HMI_DIALOG_TIME){
        unsigned at=ui->edit_time_part?3u:0u;
        if(ch=='C'){ui->edit[at]='0';ui->edit[at+1]='0';ui->edit_length=0;}
        else if(ch=='B'){ui->edit[at+1]=ui->edit[at];ui->edit[at]='0';ui->edit_length=0;}
        else if(ch>='0'&&ch<='9'){
            if(!ui->edit_length){ui->edit[at]='0';ui->edit[at+1]=(char)ch;ui->edit_length=1;}
            else {ui->edit[at]=ui->edit[at+1];ui->edit[at+1]=(char)ch;ui->edit_length=0;}
        }
        return;
    }
    if(d!=HMI_DIALOG_KEYPAD&&d!=HMI_DIALOG_ROM)return;
    if(ch=='X'||(ch=='C'&&d==HMI_DIALOG_KEYPAD)){ui->edit[0]=0;ui->edit_length=0;ui->edit_fresh=0;return;}
    if(ch=='<'||(ch=='B'&&d==HMI_DIALOG_KEYPAD)){
        ui->edit_fresh=0;if(ui->edit_length)ui->edit[--ui->edit_length]=0;return;
    }
    if(d==HMI_DIALOG_ROM){if(!((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F')))return;}
    else if(!((ch>='0'&&ch<='9')||ch=='.'||ch=='-'))return;
    if(ui->edit_fresh){ui->edit[0]=0;ui->edit_length=0;ui->edit_fresh=0;}
    if(ch=='-'){
        if(ui->edit[0]=='-'){memmove(ui->edit,ui->edit+1,ui->edit_length);ui->edit_length--;}
        else if(ui->edit_length<sizeof(ui->edit)-1u){memmove(ui->edit+1,ui->edit,ui->edit_length+1u);ui->edit[0]='-';ui->edit_length++;}
        return;
    }
    if(ch=='.'&&strchr(ui->edit,'.'))return;
    if(ui->edit_length<(d==HMI_DIALOG_ROM?16u:sizeof(ui->edit)-1u)){
        ui->edit[ui->edit_length++]=(char)ch;ui->edit[ui->edit_length]=0;
    }
}

static void apply(HmiUi *ui){
    HmiDialog d=ui->state.dialog;int group=settings_group(d);
    if(d==HMI_DIALOG_KEYPAD){
        float v;unsigned id=ui->edit_parameter;const ValueDef *def=&values[id];
        if(!parse_number(ui->edit,&v)||v<def->minimum||v>def->maximum||
           (def->decimals==0&&v!=(float)(int32_t)v)){ui->edit_error=1;return;}
        if(id<3&&(v<ui->parameters[HMI_VALUE_MOD_MIN+id*2]||v>ui->parameters[HMI_VALUE_MOD_MAX+id*2])){ui->edit_error=1;return;}
        if(id>=HMI_VALUE_AXIS_I_MIN&&id<HMI_VALUE_JOURNAL_DATE){unsigned peer=id^1u;
            if(((id&1u)==0u&&v>=current_value(ui,peer))||((id&1u)!=0u&&v<=current_value(ui,peer))){ui->edit_error=1;return;}
            if(ui->return_dialog==HMI_DIALOG_AXIS||ui->return_dialog==HMI_DIALOG_PANEL_CONTROL){
                ui->draft_limits[id-HMI_VALUE_AXIS_I_MIN]=v;ui->state.dialog=ui->return_dialog;return;
            }
        }
        if(id==HMI_VALUE_JOURNAL_DATE){ui->parameters[id]=v;ui->state.dialog=HMI_DIALOG_CONFIRM;return;}
        ui->parameters[id]=v;
        if(id==0)ui->state.modulation_set=v;
        if(id==1)ui->state.rotation_set=v;
        if(id==2)ui->state.current_limit_set=v;
        event(ui,HMI_EVENT_VALUE_CHANGED,id,v,0,NULL);ui->state.dialog=ui->return_dialog;
    }else if(d==HMI_DIALOG_TIME){
        int hour=(ui->edit[0]-'0')*10+ui->edit[1]-'0',minute=(ui->edit[3]-'0')*10+ui->edit[4]-'0';
        if(hour>23||minute>59){ui->edit_error=1;return;}
        memcpy(ui->clock,ui->edit,6);event(ui,HMI_EVENT_TIME_CHANGED,0,(float)(hour*60+minute),0,NULL);ui->state.dialog=HMI_DIALOG_NONE;
    }else if(d==HMI_DIALOG_ROM){
        uint8_t bytes[8];unsigned i;
        if(ui->edit_length!=16){ui->edit_error=1;return;}
        for(i=0;i<8;i++){
            int a=ui->edit[i*2],b=ui->edit[i*2+1];a=a<='9'?a-'0':a-'A'+10;b=b<='9'?b-'0':b-'A'+10;
            bytes[i]=(uint8_t)((a<<4)|b);
        }
        memcpy(ui->rom[ui->edit_rom],bytes,8);event(ui,HMI_EVENT_ROM_CHANGED,ui->edit_rom,0,0,bytes);
        ui->state.dialog=HMI_DIALOG_SENSOR_SCHEMES;
    }else if(group>=0){
        if(d==HMI_DIALOG_AXIS||d==HMI_DIALOG_PANEL_CONTROL){
            unsigned i,first=d==HMI_DIALOG_AXIS?HMI_VALUE_AXIS_I_MIN:HMI_VALUE_MOD_MIN;
            unsigned end=d==HMI_DIALOG_AXIS?HMI_VALUE_MOD_MIN:HMI_VALUE_JOURNAL_DATE;
            for(i=first;i<end;i++)if(ui->parameters[i]!=ui->draft_limits[i-HMI_VALUE_AXIS_I_MIN]){
                ui->parameters[i]=ui->draft_limits[i-HMI_VALUE_AXIS_I_MIN];event(ui,HMI_EVENT_VALUE_CHANGED,i,ui->parameters[i],0,NULL);
            }
        }
        if(group<3)memcpy(ui->units[group],ui->draft_units,4);
        {uint8_t payload[8]={0};if(group<3)memcpy(payload,ui->draft_units,4);
         ui->settings[group]=ui->draft_flags;event(ui,HMI_EVENT_SETTINGS_CHANGED,(unsigned)group,0,ui->draft_flags,payload);}
        ui->state.dialog=HMI_DIALOG_NONE;
    }else ui->state.dialog=HMI_DIALOG_NONE;
}

/* Local controller changes: editor text, choices and draft settings. */
static void invalidate_overlay(const HmiUi *ui,HmiAction action,int arg){
    HmiDialog d=ui->state.dialog;
    HmiRect r={0,0,0,0};int group=settings_group(d);
    if(action==HMI_ACTION_KEY||action==HMI_ACTION_APPLY||action==HMI_ACTION_TIME_PART){
        if(d==HMI_DIALOG_KEYPAD)r=(HmiRect){23,96,273,34};
        if(d==HMI_DIALOG_TIME)r=(HmiRect){21,98,275,65};
        if(d==HMI_DIALOG_ROM)r=(HmiRect){21,109,277,30};
    }else if(action==HMI_ACTION_FILTER||action==HMI_ACTION_TOGGLE){
        if(group==0&&arg<3)r=(HmiRect){145,(uint16_t)(110+79*arg),45,33};
        if((group==1||group==2)&&arg<4)r=(HmiRect){145,(uint16_t)(127+54*arg),45,33};
        if(group==3&&arg<8)r=(HmiRect){247,(uint16_t)(114+33*arg),52,29};
        if(group==4&&arg<3)r=(HmiRect){24,(uint16_t)(173+40*arg),20,20};
    }else if(action==HMI_ACTION_CUSTOM){
        if(d==HMI_DIALOG_AXIS&&(arg==100||arg==101))r=(HmiRect){207,(uint16_t)(147+96*(arg-100)),82,28};
        if(group==0&&arg>=110&&arg<=112)r=(HmiRect){195,(uint16_t)(113+79*(arg-110)),102,27};
        if((group==1||group==2)&&arg>=120&&arg<=123)r=(HmiRect){195,(uint16_t)(130+54*(arg-120)),102,27};
        if(d==HMI_DIALOG_NONE&&ui->state.page==HMI_PAGE_PARAMETERS){
            if(ui->state.param_section==HMI_PARAM_INVERTER&&arg>=10&&arg<=12)r=(HmiRect){116,94,193,29};
            if(ui->state.param_section==HMI_PARAM_MOTOR&&(arg==20||arg==21))r=(HmiRect){116,94,193,29};
            if(ui->state.param_section==HMI_PARAM_COMMUNICATION&&arg==30)r=(HmiRect){112,94,200,29};
        }
        if(d==HMI_DIALOG_NONE&&ui->state.page==HMI_PAGE_GRAPHS&&arg>=200&&arg<=203){
            hmi_invalidate((HmiRect){7,101,306,191});r=(HmiRect){7,337,306,91};
        }
    }else if(d==HMI_DIALOG_NONE){
        if(action==HMI_ACTION_MODE&&ui->state.page==HMI_PAGE_PARAMETERS&&ui->state.param_section==HMI_PARAM_SYSTEM)
            r=(HmiRect){116,184,193,29};
        if(ui->state.page==HMI_PAGE_GRAPHS){
            if(action==HMI_ACTION_GRAPH_ZOOM){hmi_invalidate((HmiRect){1,284,318,17});r=(HmiRect){50,303,66,24};}
            if(action==HMI_ACTION_GRAPH_RUN)r=(HmiRect){162,299,150,35};
        }
        if(action==HMI_ACTION_JOURNAL_PAGE&&ui->state.page==HMI_PAGE_JOURNAL)r=(HmiRect){140,398,40,24};
    }
    hmi_invalidate(r);
}

void hmi_ui_dispatch(HmiUi *ui,HmiAction action,int16_t arg){
    HmiSceneId before;uint8_t old_control,old_error,old_part;char old_edit[24];
    if(!ui)return;
    before=hmi_scene_for_state(&ui->state);old_control=ui->state.selected_control;
    old_error=ui->edit_error;old_part=ui->edit_time_part;
    memcpy(old_edit,ui->edit,sizeof(old_edit));
    ui->state.scene_override=HMI_SCENE_COUNT;
    switch(action){
    case HMI_ACTION_PAGE:
        if(arg<0||arg>HMI_PAGE_JOURNAL)return;
        ui->state.page=(HmiPage)arg;ui->state.dialog=HMI_DIALOG_NONE;event(ui,HMI_EVENT_NAVIGATED,(unsigned)arg,0,0,NULL);break;
    case HMI_ACTION_SECTION:
        if(arg<0||arg>HMI_PARAM_NETWORK)return;
        ui->state.param_section=(HmiParamSection)arg;hmi_invalidate_all();event(ui,HMI_EVENT_NAVIGATED,(unsigned)arg,0,1,NULL);break;
    case HMI_ACTION_DIALOG:
        if(arg<=HMI_DIALOG_NONE||arg>HMI_DIALOG_HELP)return;
        if(arg==HMI_DIALOG_HELP)ui->help_scroll=0;
        open_dialog(ui,(HmiDialog)arg);break;
    case HMI_ACTION_CLOSE:ui->state.dialog=HMI_DIALOG_NONE;break;
    case HMI_ACTION_CANCEL:
        if(ui->state.dialog==HMI_DIALOG_KEYPAD&&ui->edit_parameter==HMI_VALUE_JOURNAL_DATE)ui->parameters[HMI_VALUE_JOURNAL_DATE]=-1;
        ui->state.dialog=ui->state.dialog==HMI_DIALOG_KEYPAD?ui->return_dialog:
                         (ui->state.dialog==HMI_DIALOG_ROM?HMI_DIALOG_SENSOR_SCHEMES:HMI_DIALOG_NONE);break;
    case HMI_ACTION_SELECT_CONTROL:
        if(arg<0||arg>2)return;
        ui->state.selected_control=(uint8_t)arg;
        break;
    case HMI_ACTION_EDIT:if(arg<0||arg>=HMI_VALUE_COUNT)return;edit_value(ui,(unsigned)arg);break;
    case HMI_ACTION_KEY:key(ui,arg);break;
    case HMI_ACTION_APPLY:apply(ui);break;
    case HMI_ACTION_GRAPH:
        if(arg<0||arg>HMI_GRAPH_Q)return;
        ui->state.graph_page=(HmiGraphPage)arg;event(ui,HMI_EVENT_GRAPH_CHANGED,0,(float)arg,0,NULL);break;
    case HMI_ACTION_GRAPH_ZOOM:{
        uint32_t previous=ui->graph_time_ms;
        static const uint32_t divisions[]={2,5,10,20,50,100,200,500,1000,2000,5000};
        unsigned k=0;while(k<10&&divisions[k]<previous)k++;
        if(arg>0&&k)k--;else if(arg<0&&k<10)k++;
        ui->graph_time_ms=ui->graph_running&&divisions[k]<HMI_GRAPH_LIVE_MIN_MS?HMI_GRAPH_LIVE_MIN_MS:divisions[k];
        event(ui,HMI_EVENT_GRAPH_CHANGED,1,(float)ui->graph_time_ms,0,NULL);
        if(previous==ui->graph_time_ms)action=HMI_ACTION_NONE;
        break;
    }
    case HMI_ACTION_GRAPH_RUN:
        if(ui->state.graph[0].sample_count)hmi_invalidate((HmiRect){7,101,306,191});
        ui->graph_running^=1;
        if(ui->graph_running&&ui->graph_time_ms<HMI_GRAPH_LIVE_MIN_MS){
            ui->graph_time_ms=HMI_GRAPH_LIVE_MIN_MS;
            event(ui,HMI_EVENT_GRAPH_CHANGED,1,HMI_GRAPH_LIVE_MIN_MS,0,NULL);
            hmi_invalidate((HmiRect){1,284,318,50});
        }
        if(ui->graph_running){unsigned c;ui->state.graph_cursor=ui->state.graph_valid_count=0;for(c=0;c<4;c++)ui->state.graph[c].sample_count=0;}
        else ui->state.graph_valid_count=ui->state.graph_cursor;
        event(ui,HMI_EVENT_GRAPH_CHANGED,2,ui->graph_running,0,NULL);break;
    case HMI_ACTION_JOURNAL_PAGE:{
        uint16_t previous=ui->state.journal_page;
        uint16_t pages=ui->state.journal?(uint16_t)(((uint32_t)ui->state.journal_count+6u)/7u):2u;
        if(!pages)pages=1;
        if(ui->state.journal_page<1)ui->state.journal_page=1;
        if(ui->state.journal_page>pages)ui->state.journal_page=pages;
        if(arg<0&&ui->state.journal_page>1)ui->state.journal_page--;
        if(arg>0&&ui->state.journal_page<pages)ui->state.journal_page++;
        event(ui,HMI_EVENT_NAVIGATED,HMI_PAGE_JOURNAL,ui->state.journal_page,0,NULL);
        if(previous==ui->state.journal_page)action=HMI_ACTION_NONE;
        break;
    }
    case HMI_ACTION_JOURNAL_EXPORT:event(ui,HMI_EVENT_JOURNAL_EXPORT,0,0,0,NULL);break;
    case HMI_ACTION_JOURNAL_CLEAR:
        if(ui->state.dialog!=HMI_DIALOG_CONFIRM)return;
        event(ui,HMI_EVENT_JOURNAL_CLEAR,0,ui->parameters[HMI_VALUE_JOURNAL_DATE],0,NULL);ui->state.dialog=HMI_DIALOG_NONE;break;
    case HMI_ACTION_FILTER:case HMI_ACTION_TOGGLE:
        if(arg<0||arg>31||settings_group(ui->state.dialog)<0)return;
        ui->draft_flags^=1u<<(unsigned)arg;break;
    case HMI_ACTION_MODE:
        if(arg<0||arg>HMI_DRIVE_VF)return;
        if(ui->state.drive_mode==(HmiDriveMode)arg)action=HMI_ACTION_NONE;
        ui->state.drive_mode=(HmiDriveMode)arg;event(ui,HMI_EVENT_SETTINGS_CHANGED,10,(float)arg,0,NULL);break;
    case HMI_ACTION_TIME_PART:
        if(ui->edit_time_part==(uint8_t)(arg!=0))action=HMI_ACTION_NONE;
        ui->edit_time_part=(uint8_t)(arg!=0);ui->edit_length=0;break;
    case HMI_ACTION_ROM:{
        unsigned i;if(arg<0||arg>=5)return;
        ui->edit_rom=(uint8_t)arg;ui->state.dialog=HMI_DIALOG_ROM;ui->edit_fresh=1;ui->edit_error=0;
        for(i=0;i<8;i++)(void)snprintf(ui->edit+i*2,3,"%02X",ui->rom[arg][i]);
        ui->edit_length=16;break;
    }
    case HMI_ACTION_AUTO_START:if(ui->state.param_section==HMI_PARAM_AUTO){ui->state.param_section=HMI_PARAM_AUTO_RUNNING;ui->auto_progress=0;}event(ui,HMI_EVENT_AUTO_START,0,0,0,NULL);break;
    case HMI_ACTION_AUTO_CANCEL:if(ui->state.param_section==HMI_PARAM_AUTO_RUNNING)ui->state.param_section=HMI_PARAM_AUTO;event(ui,HMI_EVENT_AUTO_CANCEL,0,0,0,NULL);break;
    case HMI_ACTION_CUSTOM:
        if(arg==300&&ui->state.param_section==HMI_PARAM_AUTO_DONE){ui->state.param_section=HMI_PARAM_EQUIVALENT;event(ui,HMI_EVENT_CUSTOM,300,0,0,NULL);break;}
        if(arg==620||arg==621){if(!ui->graph_running)event(ui,HMI_EVENT_GRAPH_CHANGED,3,arg==620?-1:1,0,NULL);break;}
        if(arg==610){ui->parameters[HMI_VALUE_JOURNAL_DATE]=-1;open_dialog(ui,HMI_DIALOG_CONFIRM);break;}
        if(arg==611){ui->parameters[HMI_VALUE_JOURNAL_DATE]=0;hmi_invalidate_all();break;}
        if(arg==612){ui->parameters[HMI_VALUE_JOURNAL_DATE]=260101;edit_value(ui,HMI_VALUE_JOURNAL_DATE);break;}
        if(ui->state.dialog==HMI_DIALOG_HELP&&(arg==500||arg==501)){
            unsigned first,last,pages=help_bounds(ui,(unsigned)ui->help_scroll,&first,&last);
            if(arg==500&&ui->help_scroll>0)ui->help_scroll--;
            if(arg==501&&(unsigned)ui->help_scroll+1<pages)ui->help_scroll++;
            hmi_invalidate((HmiRect){12,40,296,397});break;
        }
        if((arg==100||arg==101)&&ui->state.dialog==HMI_DIALOG_AXIS){ui->draft_flags^=1u<<(unsigned)(arg-100);break;}
        if(arg>=110&&arg<=113&&ui->state.dialog==HMI_DIALOG_PANEL_CONTROL){ui->draft_units[arg-110]^=1;break;}
        if(arg>=120&&arg<=123&&(ui->state.dialog==HMI_DIALOG_PANEL_OUTPUT||ui->state.dialog==HMI_DIALOG_PANEL_MOTOR)){ui->draft_units[arg-120]^=1;break;}
        if(arg>=10&&arg<=12){
            if(ui->choices[0]==arg-10)action=HMI_ACTION_NONE;
            ui->choices[0]=(uint8_t)(arg-10);
        }else if(arg==20||arg==21){
            if(ui->choices[1]==arg-20)action=HMI_ACTION_NONE;
            ui->choices[1]=(uint8_t)(arg-20);
        }
        else if(arg==30)ui->choices[2]=(uint8_t)((ui->choices[2]+1u)%3u);
        else if(arg==1)ui->choices[3]^=1;
        else if(arg>=200&&arg<=203){ui->state.graph[arg-200].visible^=1;event(ui,HMI_EVENT_GRAPH_CHANGED,(unsigned)arg,ui->state.graph[arg-200].visible,0,NULL);break;}
        event(ui,HMI_EVENT_CUSTOM,(unsigned)(uint16_t)arg,
              arg==30?ui->choices[2]:(arg==1?ui->choices[3]:(arg>=10&&arg<=12?ui->choices[0]:(arg==20||arg==21?ui->choices[1]:0))),0,NULL);break;
    default:return;
    }
    if(action==HMI_ACTION_SELECT_CONTROL&&ui->state.dialog==HMI_DIALOG_NONE){
        if(ui->state.page!=HMI_PAGE_HOME)hmi_invalidate((HmiRect){1,35,318,29});
        else {
            hmi_invalidate((HmiRect){10,(uint16_t)(173+27*old_control),300,32});
            hmi_invalidate((HmiRect){10,(uint16_t)(173+27*ui->state.selected_control),300,32});
        }
    }else if(before!=hmi_scene_for_state(&ui->state))hmi_invalidate_all();
    else {
        if((action==HMI_ACTION_KEY||action==HMI_ACTION_APPLY||action==HMI_ACTION_TIME_PART)&&
           old_error==ui->edit_error&&old_part==ui->edit_time_part&&!strcmp(old_edit,ui->edit))return;
        invalidate_overlay(ui,action,arg);
    }
}

void hmi_ui_touch(HmiUi *ui,int16_t x,int16_t y,uint8_t pressed,uint32_t now){
    HmiHit h;if(!ui)return;
    if(pressed){
        if(!ui->touching){
            ui->touching=1;ui->touch_started=now;ui->touch_x=x;ui->touch_y=y;
            ui->pressed_scene=hmi_scene_for_state(&ui->state);
            ui->touch_cancelled=(uint8_t)!hmi_ui_hit_test(ui,x,y,&ui->pressed);
        }else if(x-ui->touch_x>12||ui->touch_x-x>12||y-ui->touch_y>12||ui->touch_y-y>12)ui->touch_cancelled=1;
        if(x<0||x>=320||y<0||y>=480)ui->touch_cancelled=1;
    }else if(ui->touching){
        ui->touching=0;
        /* Some controllers have no coordinates on pen-up; use last valid hit. */
        if(!ui->touch_cancelled&&(uint32_t)(now-ui->touch_started)>=HMI_UI_DEBOUNCE_MS&&
           ui->pressed_scene==hmi_scene_for_state(&ui->state)&&
           hmi_ui_hit_test(ui,ui->touch_x,ui->touch_y,&h)&&
           h.action==ui->pressed.action&&h.argument==ui->pressed.argument){
            int card=touch_card(ui,ui->touch_x,ui->touch_y);
            if(card){
                int dx=ui->touch_x-ui->last_tap_x,dy=ui->touch_y-ui->last_tap_y;
                if(ui->last_tap_card==card&&(uint32_t)(now-ui->last_tap_ms)<380u&&dx*dx+dy*dy<28*28){
                    ui->last_tap_card=0;hmi_ui_dispatch(ui,HMI_ACTION_DIALOG,(int16_t)card);
                }else{
                    ui->last_tap_card=(uint8_t)card;ui->last_tap_ms=now;
                    ui->last_tap_x=ui->touch_x;ui->last_tap_y=ui->touch_y;
                    if(card==HMI_DIALOG_PANEL_CONTROL&&ui->touch_y>=175)
                        hmi_ui_dispatch(ui,HMI_ACTION_SELECT_CONTROL,(int16_t)((ui->touch_y-175)/27));
                }
            }else{ui->last_tap_card=0;hmi_ui_dispatch(ui,h.action,h.argument);}
        }else ui->last_tap_card=0;
    }
}

static int collect_labels;
static void text_label(int x,int y,int w,int h,const char *text,int px,uint16_t color,int inset,int baseline){
    if(collect_labels){ui_text_replace(x-3,y+h/2,w+3,h-h/2+2);return;}
    if(!ui_rect_visible(x,y,w,h))return;
    ui_set_clip(x,y,w,h);
    ui_text_cstr(x+inset,y+baseline,px,color,text,0);
}

static void label(int x,int y,int w,int h,const char *text,int px,uint16_t color){
    text_label(x,y,w,h,text,px,color,3,(h+px)/2-2);
}
static void value_label(int x,int y,int w,int h,const char *text,int px,uint16_t color){
    text_label(x,y,w,h,text,px,color,0,h-2);
}
static void choice_label(int x,int y,int w,int h,const char *text,int selected){
    if(!collect_labels&&ui_rect_visible(x,y,w,h)){
        ui_set_clip(x,y,w,h);ui_fill_round_rect(x,y,w,h,3,selected?2211u:4357u);
        ui_round_rect(x,y,w,h,3,selected?36454u:12841u);
    }
    label(x,y,w,h,text,9,selected?36454u:61342u);
}

static void home_values(HmiUi *ui){
    HmiState *s=&ui->state;char text[48];unsigned i,j;
    float control[3][2]={{s->modulation_set,s->modulation_actual},{s->rotation_set,s->rotation_actual},{s->current_limit_set,s->current_limit_actual}};
    float output[]={s->output_voltage,s->output_current,s->output_power,s->dc_bus_voltage};
    float motor[]={s->rotor_frequency,s->stator_frequency,s->slip,s->motor_load};
    static const int output_y[]={304,342,380,407},motor_y[]={304,336,369,401};
    if(!collect_labels&&(s->telemetry_flags&128u)){
        for(i=0;i<3;i++){
            int y=175+(int)i*27;
            ui_set_clip(10,y,300,29);
            ui_replace_color_rect(10,y,300,29,36454u,4357u);
            ui_replace_color_rect(10,y,300,29,34276u,4357u);
            if(i==s->selected_control&&(ui->settings[0]&(1u<<i)))ui_rect(10,y,300,26,36454u);
        }
    }
    for(i=0;i<3;i++){
        if(!collect_labels&&!ui_rect_visible(11,175+(int)i*27,298,25))continue;
        if(!(ui->settings[0]&(1u<<i))){ui_set_clip(11,175+(int)i*27,298,24);ui_fill_rect(11,175+(int)i*27,298,24,4357u);continue;}
        if(!collect_labels&&(s->telemetry_flags&128u)){
            ui_set_clip(11,175+(int)i*27,298,25);
            uint16_t bg=(s->control_warning_mask&(1u<<i))?12610u:4357u;
            ui_replace_color_rect(11,175+(int)i*27,298,25,4357u,bg);
            ui_replace_color_rect(11,175+(int)i*27,298,25,4324u,bg);
            ui_replace_color_rect(11,175+(int)i*27,298,25,12610u,bg);
        }
        for(j=0;j<2;j++){
            float v=control[i][j];const char *unit=i==1?"Гц":"%";
            if(ui->units[0][i]){
                if(i==0){v*=0.01f;unit="о.е.";}
                else if(i==1){v*=60;unit="об/мин";}
                else {v*=ui->parameters[HMI_VALUE_MOTOR_CURRENT]*0.01f;unit="А";}
            }
            (void)snprintf(text,sizeof(text),"%g %s",(double)v,unit);
            if(!collect_labels&&!j&&(s->pending_mask&(1u<<i))){
                /* Keep the baked-text exclusion identical for all states. */
                int y=177+(int)i*27;
                ui_set_clip(139,y,79,25);
                ui_text_cstr(142,y+23,10,36454u,text,0);
                v=s->pending_setpoints[i];
                if(ui->units[0][i]){
                    if(i==0)v*=0.01f;
                    else if(i==1)v*=60;
                    else v*=ui->parameters[HMI_VALUE_MOTOR_CURRENT]*0.01f;
                }
                (void)snprintf(text,sizeof(text),"%g %s",(double)v,unit);
                ui_text_cstr(142,y+11,11,15709u,text,0);
            }else label(j?226:139,179+(int)i*27,j?82:79,21,text,11,j&&(s->control_warning_mask&(1u<<i))?62946u:36454u);
        }
    }
    if(!collect_labels){
        if(ui->settings[1]!=15){ui_set_clip(14,292,138,132);ui_fill_rect(14,292,138,132,4324u);}
        if(ui->settings[2]!=15){ui_set_clip(169,292,137,132);ui_fill_rect(169,292,137,132,4324u);}
    }
    for(i=0;i<4;i++){
        int oy=output_y[i],my=motor_y[i];unsigned n,oc=0,mc=0;
        for(n=0;n<4;n++){if(ui->settings[1]&(1u<<n))oc++;if(ui->settings[2]&(1u<<n))mc++;}
        if(ui->settings[1]!=15){oy=304;for(n=0;n<i;n++)if(ui->settings[1]&(1u<<n))oy+=132/(oc?oc:1);}
        if(ui->settings[2]!=15){my=304;for(n=0;n<i;n++)if(ui->settings[2]&(1u<<n))my+=132/(mc?mc:1);}
        static const char *const output_names[]={"ЛИН. НАПРЯЖЕНИЕ RMS","ФАЗНЫЙ ТОК RMS","АКТИВНАЯ МОЩНОСТЬ","DC ШИНА"};
        static const char *const motor_names[]={"ЧАСТОТА РОТОРА","ЧАСТОТА СТАТОРА","СКОЛЬЖЕНИЕ","НАГРУЗКА"};
        if(!collect_labels){
            ui_set_clip(10,289,145,139);
            if(ui->settings[1]&(1u<<i))ui_text_cstr((i==3&&ui->settings[1]==15)?15:15,(i==3&&ui->settings[1]==15)?418:oy-2,9,44503u,output_names[i],0);
            ui_set_clip(164,289,146,139);
            if(ui->settings[2]&(1u<<i))ui_text_cstr(170,my-2,9,44503u,motor_names[i],0);
        }
        const char *unit=i==0||i==3?"В":(i==1?"А":"кВт");float v=output[i];
        if(!collect_labels&&(ui->settings[1]&(1u<<i))&&ui_rect_visible((i==3&&ui->settings[1]==15)?64:15,oy,(i==3&&ui->settings[1]==15)?87:137,(i==3&&ui->settings[1]==15)?17:(oc<3?38:21))){
            if(ui->units[1][i]){
                if(i==2){v*=1000;unit="Вт";}
                else {float base=i==1?ui->parameters[HMI_VALUE_MOTOR_CURRENT]:ui->parameters[HMI_VALUE_MOTOR_VOLTAGE];v=base>0?v/base:0;unit="о.е.";}
            }
            (void)snprintf(text,sizeof(text),"%g %s",(double)v,unit);
            value_label((i==3&&ui->settings[1]==15)?64:15,oy,(i==3&&ui->settings[1]==15)?87:137,(i==3&&ui->settings[1]==15)?15:(oc<3?38:21),(ui->settings[1]&(1u<<i))?text:"",(i==3&&ui->settings[1]==15)?9:(oc<3?24:16),36454u);
        }
        if(collect_labels||!(ui->settings[2]&(1u<<i))||!ui_rect_visible(170,my,135,mc<3?38:21))continue;
        unit=i<2?"Гц":"%";v=motor[i];
        if(ui->units[2][i]){
            if(i<2){v*=60;unit="об/мин";}
            else if(i==2){v*=s->stator_frequency*0.01f;unit="Гц";}
            else {v*=0.01f;unit="о.е.";}
        }
        if(i==3&&motor[i]<0)(void)snprintf(text,sizeof(text),"— %s",unit);
        else (void)snprintf(text,sizeof(text),"%g %s",(double)v,unit);
        value_label(170,my,135,mc<3?38:21,(ui->settings[2]&(1u<<i))?text:"",mc<3?24:16,15709u);
    }
    if(!collect_labels&&ui_rect_visible(140,274,14,12)){
        static const int8_t wave[]={0,-2,-4,-4,-2,0,2,4,4,2,0};
        ui_set_clip(140,274,14,12);
        for(i=1;i<11;i++)ui_line_aa(141+(int)i-1,280+wave[i-1],141+(int)i,280+wave[i],36454u);
    }
    if(!collect_labels&&(s->journal_warning||(s->telemetry_flags&64u))){
        ui_set_clip(8,140,304,24);ui_fill_rect(8,140,304,24,(s->telemetry_flags&64u)?16547u:12610u);
        if(s->telemetry_flags&64u){ui_round_rect(8,140,304,24,3,62154u);ui_round_rect(9,141,302,22,3,62154u);ui_text_cstr(14,159,16,62154u,"!",1);ui_text_fit_cstr(29,158,14,62154u,"НЕТ СВЯЗИ С КОНТРОЛЛЕРОМ",1,275);return;}
        ui_text_cstr(12,156,10,62946u,(s->telemetry_flags&64u)?"Нет связи с контроллером":s->journal_warning==2?"Упаковка журнала…":s->journal_warning==3?"Ошибка записи журнала во Flash":"Журнал заполнен >90% — упаковать",1);
    }

}

static void paint(void *user){
    HmiUi *ui=(HmiUi *)user;HmiDialog d=ui->state.dialog;char text[128];unsigned i;
        if(d==HMI_DIALOG_NONE&&ui->state.page!=HMI_PAGE_HOME&&!collect_labels&&(ui->state.telemetry_flags&128u))for(i=0;i<3;i++){
            int x=(int)i*106+1;float v=i==0?ui->state.modulation_set:i==1?ui->state.rotation_set:ui->state.current_limit_set;
            uint16_t color=(ui->state.control_warning_mask&(1u<<i))?62946u:36454u;
            static const char *const names[]={"Kmod =","Frot =","Ilim ="};
            ui_set_clip(x,35,106,29);
            if(ui->state.selected_control==i)ui_fill_gradient_v(x,35,106,29,21326u,14987u);
            else ui_fill_gradient_v(x,35,106,29,15052u,10793u);
            (void)snprintf(text,sizeof(text),i==1?"%.1f Гц":"%.0f %%",(double)v);
            int nw=ui_measure_cstr(9,names[i]),vw=ui_measure_cstr(11,text),tx;
            if(ui->state.pending_mask&(1u<<i)){char pending[24];(void)snprintf(pending,sizeof(pending),i==1?"%.1f Гц":"%.0f %%",(double)ui->state.pending_setpoints[i]);int pw=ui_measure_cstr(10,pending);if(pw>vw)vw=pw;}
            tx=x+(106-nw-6-vw)/2;ui_text_cstr(tx,54,9,52890u,names[i],1);tx+=nw+6;
            if(ui->state.pending_mask&(1u<<i)){
                ui_text_cstr(tx,61,9,color,text,0);
                (void)snprintf(text,sizeof(text),i==1?"%.1f Гц":"%.0f %%",(double)ui->state.pending_setpoints[i]);
                ui_text_cstr(tx,47,10,15709u,text,1);
            }else ui_text_cstr(tx,54,11,color,text,1);
        }
    if(!collect_labels&&ui_rect_visible(102,1,152,33)){
        static const char *const titles[]={"ПЧ-1000","ГРАФИКИ","ПАРАМЕТРЫ","ЖУРНАЛ"};
        const char *title=titles[(unsigned)ui->state.page<4?ui->state.page:0];
        int w=ui_measure_cstr(15,title),x=(320-w)/2,q=x+w+12;
        ui_set_clip(102,1,152,33);ui_fill_gradient_v(1,1,319,34,15052u,10793u);
        ui_text_cstr(x,24,15,61342u,title,1);ui_circle(q,19,7,44503u,0);ui_text_cstr(q-3,24,12,61342u,"?",0);
    }
    if(d==HMI_DIALOG_HELP){
        unsigned page=(unsigned)ui->state.page,first,last,pages;int y=97;
        if(collect_labels)return;
        if(page>=4)page=0;
        pages=help_bounds(ui,(unsigned)ui->help_scroll,&first,&last);
        ui_set_clip(12,40,296,397);ui_fill_rect(12,40,296,397,4357u);
        (void)snprintf(text,sizeof(text),"СПРАВКА  %u / %u",(unsigned)ui->help_scroll+1,pages);
        ui_text_cstr(23,56,12,61342u,text,1);
        ui_fill_rect(23,65,6,6,36454u);ui_text_cstr(33,72,10,36454u,"норма",0);
        ui_fill_rect(96,65,6,6,62946u);ui_text_cstr(106,72,10,62946u,"переход / предупреждение",0);
        ui_fill_rect(23,79,6,6,62154u);ui_text_cstr(33,86,10,62154u,"авария",0);
        ui_fill_rect(96,79,6,6,15709u);ui_text_cstr(106,86,10,15709u,"информация",0);
        for(i=first;i<last;){
            unsigned end=i;int h;while(end<last&&help_line(page,end)[0])end++;
            h=(int)(end-i)*15+7;
            ui_set_clip(20,y,280,h);ui_fill_rect(20,y,280,h,2211u);ui_rect(20,y,280,h,12841u);
            for(unsigned line=i;line<end;line++){
                const char *body=help_line(page,line);
                int baseline=y+14+(int)(line-i)*15;
                ui_text_cstr(27,baseline,line==i?11:10,line==i?61342u:52890u,body,line==i);
                if(line!=i){
                    static const char *const terms[]={"ЗАРЯД / РАЗРЯД","ПУСК / СТОП","Энкодер","(U/F)","(SF)","(VF)","Синий","Жёлтый","зелёный"};
                    for(unsigned k=0;k<9;k++){const char *at=strstr(body,terms[k]);if(at){char prefix[128];size_t n=(size_t)(at-body);memcpy(prefix,body,n);prefix[n]=0;
                        ui_text_cstr(27+ui_measure_cstr(10,prefix),baseline,10,k==7?62946u:k==8?36454u:15709u,terms[k],0);}}
                }
            }
            y+=h+5;i=end<last?end+1:end;
        }
        ui_set_clip(16,402,288,32);
        ui_round_rect(16,402,62,32,3,36454u);ui_text_cstr(42,424,14,36454u,"<",1);
        ui_round_rect(242,402,62,32,3,36454u);ui_text_cstr(266,424,14,36454u,">",1);
        ui_round_rect(91,402,138,32,3,36454u);ui_text_cstr(127,423,11,36454u,"ЗАКРЫТЬ",0);
    }else if(d==HMI_DIALOG_CONFIRM){
        if(collect_labels)return;
        ui->state.dialog=HMI_DIALOG_NONE;paint(ui);ui->state.dialog=HMI_DIALOG_CONFIRM;
        ui_dim_content();
        ui_set_clip(1,35,318,394);ui_fill_round_rect(12,150,296,156,4,4357u);
        ui_round_rect(12,150,296,156,4,44503u);
        ui_text_cstr(25,177,13,61342u,"ОЧИСТКА ЖУРНАЛА",1);
        if(ui->parameters[HMI_VALUE_JOURNAL_DATE]<0){
            for(i=0;i<3;i++){
                static const char *const options[]={"ОЧИСТИТЬ ВСЁ","ОЧИСТИТЬ ПО ДАТУ","ОТМЕНА"};
                ui_round_rect(20,192+(int)i*36,278,31,3,i==2?12841u:36454u);
                ui_text_cstr(47,213+(int)i*36,11,61342u,options[i],1);
            }
        }else{
            if(ui->parameters[HMI_VALUE_JOURNAL_DATE]==0)strcpy(text,"Удалить все записи?");
            else (void)snprintf(text,sizeof(text),"Удалить по дату %.0f включительно?",(double)ui->parameters[HMI_VALUE_JOURNAL_DATE]);
            ui_text_cstr(20,210,10,62946u,text,0);
            ui_rect(20,234,136,35,12841u);ui_rect(162,234,136,35,62154u);
            ui_text_cstr(55,257,11,61342u,"ОТМЕНА",0);ui_text_cstr(191,257,11,62154u,"ОЧИСТИТЬ",0);
        }
    }else if(d==HMI_DIALOG_KEYPAD){
        const ValueDef *v=&values[ui->edit_parameter];
        label(21,54,277,21,value_text(ui->edit_parameter,0),12,61342u);
        (void)snprintf(text,sizeof(text),"%g ... %g %s",(double)v->minimum,(double)v->maximum,value_text(ui->edit_parameter,1));
        label(21,76,277,17,text,9,44503u);
        label(23,96,273,34,ui->edit,20,ui->edit_error?62154u:61342u);
    }else if(d==HMI_DIALOG_TIME){
        char part[3]={ui->edit[0],ui->edit[1],0};
        label(53,124,99,39,part,20,ui->edit_time_part?61342u:36454u);
        part[0]=ui->edit[3];part[1]=ui->edit[4];label(168,124,99,39,part,20,ui->edit_time_part?36454u:61342u);
        label(21,98,275,18,ui->edit_error?"Неверное время":"Выберите часы или минуты и введите две цифры.",10,ui->edit_error?62154u:44503u);
    }else if(d==HMI_DIALOG_ROM){
        label(21,109,277,30,ui->edit,13,ui->edit_error?62154u:61342u);
    }else if(d==HMI_DIALOG_SENSOR_SCHEMES){
        static const char *const names[]={"Выпрямитель","Предзаряд","DC-шина","Инвертор","Двигатель"};
        if(collect_labels)return;
        ui_set_clip(1,108,318,321);ui_fill_rect(1,108,318,321,4357u);
        ui_set_clip(1,35,318,394);ui_round_rect(10,40,300,384,4,12841u);
        ui_text_cstr(21,122,9,44503u,"ROM: 8 байт, 16 HEX-символов",0);
        for(i=0;i<5;i++){
            unsigned j;int y=132+(int)i*47;
            for(j=0;j<8;j++)(void)snprintf(text+j*2,3,"%02X",ui->rom[i][j]);
            ui_text_cstr(21,y+21,10,61342u,names[i],0);
            ui_rect(110,y,185,34,12841u);ui_text_cstr(116,y+22,10,61342u,text,0);
        }
        ui_rect(20,385,136,33,12841u);ui_rect(162,385,136,33,36454u);
        ui_text_cstr(54,407,10,61342u,"ЗАКРЫТЬ",0);ui_text_cstr(189,407,10,36454u,"СОХРАНИТЬ",0);
    }else if(d==HMI_DIALOG_AXIS){
        for(i=0;i<4;i++){
            (void)snprintf(text,sizeof(text),"%g",(double)ui->draft_limits[i]);
            label(63+(int)(i%2)*135,186+(int)(i/2)*96,91,28,text,11,61342u);
        }
        for(i=0;i<2;i++)label(207,147+(int)i*96,82,28,(ui->draft_flags&(1u<<i))?"АВТО":"РУЧН",10,36454u);
    }else if(d==HMI_DIALOG_SENSOR_DISPLAY){
        static const char *const names[]={"Напряжение сети","Напряжение DC-шины","Ток предзаряда","Темп. выпрямителя","Темп. предзаряда","Темп. DC-шины","Темп. инвертора","Темп. двигателя"};
        ui_set_clip(20,110,280,312);ui_fill_rect(20,110,280,312,4357u);
        for(i=0;i<8;i++){
            label(21,114+(int)i*33,221,29,names[i],11,61342u);
            label(247,114+(int)i*33,52,29,(ui->draft_flags&(1u<<i))?"ВКЛ":"ВЫКЛ",10,(ui->draft_flags&(1u<<i))?36454u:44503u);
            ui_set_clip(20,110,280,312);ui_rect(247,114+(int)i*33,52,29,(ui->draft_flags&(1u<<i))?36454u:12841u);
        }
        label(21,385,137,33,"ОТМЕНА",10,61342u);label(163,385,137,33,"СОХРАНИТЬ",10,36454u);
        ui_set_clip(20,110,280,312);ui_rect(21,385,137,33,44503u);ui_rect(163,385,137,33,36454u);
    }else if(settings_group(d)>=0){
        int group=settings_group(d),count=(group==0||group==4)?3:4;
        for(i=0;i<(unsigned)count;i++){
            int x=146,y=128+(int)i*54,w=43;
            if(group==0){x=146;y=111+(int)i*79;w=43;}
            if(group==4){
                ui_set_clip(24,173+(int)i*40,20,20);ui_fill_rect(24,173+(int)i*40,20,20,4357u);
                ui_rect(25,174+(int)i*40,17,17,36454u);
                if(ui->draft_flags&(1u<<i))ui_line(27,181+(int)i*40,34,188+(int)i*40,36454u);
                if(ui->draft_flags&(1u<<i))ui_line(34,188+(int)i*40,40,176+(int)i*40,36454u);
                continue;
            }
            if(!collect_labels){ui_set_clip(x-1,y-1,w+2,33);ui_fill_rect(x-1,y-1,w+2,33,4357u);}
            choice_label(x,y,w,29,(ui->draft_flags&(1u<<i))?"ВКЛ":"ВЫКЛ",(ui->draft_flags&(1u<<i))!=0);
        }
        if(group==0)for(i=0;i<6;i++){
            (void)snprintf(text,sizeof(text),"%g",(double)ui->draft_limits[4+i]);
            label(55+(int)(i%2)*143,153+(int)(i/2)*79,99,27,text,10,61342u);
        }
        if(group<3)for(i=0;i<(unsigned)count;i++){
            static const char *const base[3][4]={{"%","Гц","% Iном",""},{"В","А","кВт","В"},{"Гц","Гц","%","%"}};
            static const char *const alternate[3][4]={{"о.е.","об/мин","А",""},{"о.е.","о.е.","Вт","о.е."},{"об/мин","об/мин","Гц","о.е."}};
            label(195,group==0?113+(int)i*79:130+(int)i*54,102,27,ui->draft_units[i]?alternate[group][i]:base[group][i],10,61342u);
        }
    }else if(d==HMI_DIALOG_NONE&&ui->state.page==HMI_PAGE_HOME){
        home_values(ui);
    }else if(d==HMI_DIALOG_NONE&&ui->state.page==HMI_PAGE_PARAMETERS){
        unsigned section=ui->state.param_section;
        int grouped=1;
        if(!collect_labels){
            if(grouped){ui_set_clip(110,65,205,364);ui_fill_rect(110,65,205,364,4357u);
                const char *title=section==HMI_PARAM_NETWORK?"ЗАЩИТЫ СЕТИ":section==HMI_PARAM_SYSTEM?"СИСТЕМА":section==HMI_PARAM_INVERTER?"ИНВЕРТОР":section==HMI_PARAM_COMMUNICATION?"СВЯЗЬ":section==HMI_PARAM_CALIBRATION?"КАЛИБРОВКА":section==HMI_PARAM_EQUIVALENT?"СХЕМА ЗАМЕЩЕНИЯ":section==HMI_PARAM_TEMPERATURE?"ТЕМПЕРАТУРНЫЕ":section==HMI_PARAM_PROTECTIONS?"СИЛОВЫЕ ЗАЩИТЫ":"ДАННЫЕ ШИЛЬДА";
                ui_text_cstr(116,82,11,61342u,title,1);
            }

            if(section==HMI_PARAM_AUTO_RUNNING||section==HMI_PARAM_AUTO_DONE){
                ui_set_clip(110,65,205,364);ui_fill_rect(110,65,205,364,4357u);
                ui_text_cstr(117,90,11,61342u,"АВТООПРЕДЕЛЕНИЕ",1);
                (void)snprintf(text,sizeof(text),"%u %%",ui->auto_progress);ui_text_cstr(175,160,18,36454u,text,1);
                ui_rect(118,181,186,16,12841u);ui_fill_rect(121,184,180*ui->auto_progress/100,10,36454u);
                ui_text_cstr(118,225,10,44503u,section==HMI_PARAM_AUTO_DONE?"Готово":ui->auto_progress?"Измерение параметров":"Ожидание контроллера",0);
                choice_label(113,275,200,31,section==HMI_PARAM_AUTO_DONE?"СОХРАНИТЬ":"ОТМЕНА",0);return;
            }
        }
        for(i=3;i<HMI_VALUE_COUNT;i++)if(value_visible(ui,i)){
            HmiRect r=value_rect(ui,i);
            if(!collect_labels&&!ui_rect_visible(r.x,r.y,r.width,r.height))continue;
            (void)snprintf(text,sizeof(text),"%g %s",(double)ui->parameters[i],value_text(i,1));
            if(grouped){
                if(!collect_labels){ui_set_clip(r.x,r.y,r.width,r.height);ui_fill_rect(r.x,r.y,r.width,r.height,2211u);ui_rect(r.x,r.y,r.width,r.height,12841u);ui_text_cstr(r.x+4,r.y+10,8,44503u,value_text(i,0),0);ui_text_cstr(r.x+100,r.y+25,11,61342u,text,1);
                if(i>=HMI_VALUE_MAX_CURRENT_PU&&i<=HMI_VALUE_MOTOR_TEMP_PU){
                    float base=1;const char *unit="";
                    switch(i){case HMI_VALUE_MAX_CURRENT_PU:base=ui->parameters[HMI_VALUE_MOTOR_CURRENT];unit="А";break;
                    case HMI_VALUE_DC_HIGH_PU:case HMI_VALUE_DC_LOW_PU:base=ui->parameters[HMI_VALUE_MAINS_VOLTAGE]*1.41421356f;unit="В";break;
                    case HMI_VALUE_MAX_SPEED_PU:base=ui->parameters[HMI_VALUE_MOTOR_RPM];unit="об/мин";break;
                    case HMI_VALUE_MAX_POWER_PU:base=ui->parameters[HMI_VALUE_MOTOR_POWER_KW];unit="кВт";break;
                    case HMI_VALUE_INVERTER_TEMP_PU:base=80;unit="°C";break;default:base=100;unit="°C";break;}
                    (void)snprintf(text,sizeof(text),i==HMI_VALUE_MAX_POWER_PU?"%.1f %s":"%.0f %s",(double)(ui->parameters[i]*base),unit);ui_text_cstr(r.x+4,r.y+25,9,15709u,text,0);
                }}
            }else label(r.x+1,r.y+1,r.width-2,r.height-2,text,9,61342u);
        }
        if(section==HMI_PARAM_MOTOR||section==HMI_PARAM_EQUIVALENT||section==HMI_PARAM_PROTECTIONS||section==HMI_PARAM_TEMPERATURE||section==HMI_PARAM_NETWORK||section==HMI_PARAM_AUTO){
            const char *next=section==HMI_PARAM_MOTOR?"СХЕМА ЗАМЕЩЕНИЯ >":section==HMI_PARAM_EQUIVALENT?"< ДАННЫЕ ШИЛЬДА":section==HMI_PARAM_PROTECTIONS?"ТЕМПЕРАТУРНЫЕ >":section==HMI_PARAM_TEMPERATURE?"ЗАЩИТЫ СЕТИ >":section==HMI_PARAM_NETWORK?"< СИЛОВЫЕ ЗАЩИТЫ":"НАЧАТЬ";
            choice_label(112,394,200,34,next,0);
        }
        if(ui->state.param_section==HMI_PARAM_SYSTEM){
            static const char *const names[]={"U/F CONST","СКАЛЯР","ВЕКТОР"};
            label(116,229,193,29,"ВЕРСИЯ ПО: 1.2.4",11,61342u);
            label(116,157,193,26,"АЛГОРИТМ УПРАВЛЕНИЯ",10,44503u);
            for(i=0;i<3;i++)choice_label(116+(int)i*65,184,61,29,names[i],i==(unsigned)ui->state.drive_mode);
        }else if(ui->state.param_section==HMI_PARAM_INVERTER){
            static const char *const names[]={"СИНУС","ВЕКТОР","ДЕЛЬТА"};
            for(i=0;i<3;i++){choice_label(116+(int)i*65,94,63,29,names[i],i==ui->choices[0]);label(119+(int)i*65,114,57,10,i==2?"МОДУЛЯЦИЯ":"ШИМ",7,44503u);}
        }else if(ui->state.param_section==HMI_PARAM_MOTOR||ui->state.param_section==HMI_PARAM_AUTO){
            choice_label(116,94,95,29,"Y ЗВЕЗДА",!ui->choices[1]);
            choice_label(214,94,95,29,"",ui->choices[1]);
            label(228,94,79,29,"ТРЕУГОЛЬНИК",9,ui->choices[1]?36454u:61342u);
            if(!collect_labels){uint16_t c=ui->choices[1]?36454u:61342u;ui_set_clip(216,95,12,27);ui_line(217,113,222,103,c);ui_line(222,103,227,113,c);ui_line(217,113,227,113,c);}
        }else if(ui->state.param_section==HMI_PARAM_COMMUNICATION){
            static const char *const names[]={"Нет","Чётная","Нечётная"};
            choice_label(112,94,200,29,"",0);
            label(116,94,100,29,"ЧЁТНОСТЬ",9,44503u);
            label(220,94,88,29,names[ui->choices[2]],10,61342u);
        }
    }else if(d==HMI_DIALOG_NONE&&ui->state.page==HMI_PAGE_GRAPHS){
        static const int xs[]={7,50,117,162,195,230}, widths[]={42,66,43,30,30,82};
        if(collect_labels)return;
        ui_set_clip(7,299,305,35);ui_fill_rect(7,299,305,35,4357u);
        for(i=0;i<6;i++)ui_rect(xs[i],299,widths[i],35,12841u);
        ui_text_cstr(23,322,13,61342u,"-",0);ui_text_cstr(133,322,13,61342u,"+",0);
        for(i=0;i<2;i++){int x=177+(int)i*33,d=i?1:-1;ui_line(x-d*3,312,x+d*3,317,61342u);ui_line(x+d*3,317,x-d*3,322,61342u);}
        (void)snprintf(text,sizeof(text),ui->graph_time_ms>=1000?"%lu с/дел":"%lu мс/дел",(unsigned long)(ui->graph_time_ms>=1000?ui->graph_time_ms/1000:ui->graph_time_ms));
        ui_text_cstr(83-ui_measure_cstr(9,text)/2,321,9,36454u,text,0);
        ui_text_cstr(271-ui_measure_cstr(10,ui->graph_running?"СТОП":"ПУСК")/2,322,10,36454u,ui->graph_running?"СТОП":"ПУСК",0);
    }else if(d==HMI_DIALOG_NONE&&ui->state.page==HMI_PAGE_JOURNAL){
        (void)snprintf(text,sizeof(text),"%u",ui->state.journal_page);label(140,398,40,24,text,10,36454u);
    }
    if(!collect_labels&&ui->state.journal_warning==2){
        ui_set_clip(20,180,280,120);ui_fill_round_rect(20,180,280,120,5,4357u);ui_round_rect(20,180,280,120,5,62946u);
        ui_text_cstr(34,207,12,61342u,"УПАКОВКА ЖУРНАЛА",1);
        (void)snprintf(text,sizeof(text),"%u %%",ui->state.journal_progress);
        ui_text_cstr(136,233,14,62946u,text,1);ui_rect(35,249,250,16,12841u);
        ui_fill_rect(38,252,244*ui->state.journal_progress/100,10,36454u);
        ui_text_cstr(43,285,9,44503u,"Стирание / перенос / проверка",0);
    }

}

void hmi_ui_init(HmiUi *ui,HmiDisplay display){
    unsigned i;if(!ui)return;memset(ui,0,sizeof(*ui));hmi_state_defaults(&ui->state);
    ui->display=display;memcpy(ui->clock,"17:32",6);ui->state.clock=ui->clock;
    ui->state.journal_page=1;ui->state.dynamic_values=1;ui->graph_time_ms=200;
    for(i=0;i<HMI_VALUE_COUNT;i++)ui->parameters[i]=values[i].initial;
    ui->settings[0]=7;ui->settings[1]=15;ui->settings[2]=15;ui->settings[3]=255;ui->settings[4]=7;ui->settings[5]=3;
    ui->choices[0]=1;
    hmi_init();hmi_invalidate_all();
}

void hmi_ui_prepare_text_layers(HmiUi *ui){
    ui->state.sensor_mask=(uint8_t)ui->settings[3];
    ui->state.output_mask=(uint8_t)ui->settings[1];ui->state.motor_mask=(uint8_t)ui->settings[2];
    ui->state.graph_division_ms=ui->graph_time_ms;
    ui_set_live_graph_background((ui->state.telemetry_flags&128u)&&ui->state.page==HMI_PAGE_GRAPHS);
    ui_text_layers_begin(ui->state.page==HMI_PAGE_HOME&&ui->state.dialog==HMI_DIALOG_NONE);
    collect_labels=1;paint(ui);collect_labels=0;
    if(ui_text_layers_home()){
        ui_text_replace(10,289,145,139);ui_text_replace(164,289,146,139);ui_text_replace(139,269,17,21);
        static const HmiRect fields[]={{20,51,43,24},{118,51,37,12},{170,51,36,12},
            {66,119,39,15},{117,119,39,15},{169,119,39,15},{217,119,39,15},{268,107,39,15}};
        unsigned i;for(i=0;i<8;i++){HmiRect r=fields[i];ui_text_replace(r.x,r.y,r.width,r.height);}
    }
}

void hmi_ui_render(HmiUi *ui){
    HmiState visible;if(!ui||!ui->display.write_rect)return;
    if(!hmi_dirty_pending())return;
    hmi_ui_prepare_text_layers(ui);
    visible=ui->state;
    /* Header telemetry stays live; the dynamic layer clips page content at modals. */
    hmi_render_dirty_ex(&visible,ui->display.write_rect,ui->display.user,paint,ui);
    ui_text_layers_end();
}

int hmi_ui_poll_event(HmiUi *ui,HmiEvent *out){
    if(!ui||!out||!ui->event_count)return 0;
    *out=ui->events[ui->event_head];ui->event_head=(uint8_t)((ui->event_head+1u)%HMI_UI_EVENT_CAPACITY);ui->event_count--;return 1;
}

void hmi_ui_set_regions(HmiUi *ui,const HmiTouchRegion *regions,uint16_t count){
    if(!ui)return;
    ui->regions=regions;ui->region_count=regions?count:0;
}
