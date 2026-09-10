#include "storage_fixture.h"
#include "hmi_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t actual[320u*480u],expected[320u*480u];
static unsigned pixels,checks;
static HmiUi *switch_during_flush;
static void collect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    unsigned row;uint16_t *frame=user;
    pixels+=(unsigned)w*h;
    for(row=0;row<h;row++)memcpy(frame+(y+row)*320u+x,p+row*stride,w*2u);
    if(switch_during_flush){HmiUi *ui=switch_during_flush;switch_during_flush=0;
        hmi_ui_dispatch(ui,HMI_ACTION_PAGE,HMI_PAGE_PARAMETERS);}

}
static unsigned verify(HmiUi *ui,unsigned limit){
    unsigned sent;
    pixels=0;ui->display.user=actual;hmi_ui_render(ui);sent=pixels;
    ui->display.user=expected;hmi_invalidate_all();hmi_ui_render(ui);
    if(memcmp(actual,expected,sizeof(actual))){
        unsigned i;for(i=0;i<320u*480u;i++)if(actual[i]!=expected[i])break;
        fprintf(stderr,"check %u scene %u: mismatch at %u,%u (%04x != %04x)\n",checks,
                (unsigned)hmi_scene_for_state(&ui->state),i%320,i/320,actual[i],expected[i]);
        assert(0);
    }
    if(sent>limit)fprintf(stderr,"check %u pixels %u limit %u\n",checks,sent,limit);
    assert(sent<=limit);
    if(getenv("HMI_UI_HASHES")){uint32_t hash=2166136261u;
        for(unsigned at=0;at<320u*480u;at++){hash^=actual[at];hash*=16777619u;}
        printf("frame %u %08lx scene %u dialog %u\n",checks,(unsigned long)hash,(unsigned)hmi_scene_for_state(&ui->state),(unsigned)ui->state.dialog);}
    checks++;
    ui->display.user=actual;pixels=0;hmi_ui_render(ui);assert(!pixels);
    return sent;
}
static void step(HmiUi *ui,HmiAction action,int arg,unsigned limit){
    hmi_ui_dispatch(ui,action,(int16_t)arg);verify(ui,limit);
}
static void open(HmiUi *ui,HmiDialog d){step(ui,HMI_ACTION_DIALOG,d,153600);}
int main(void){
    HmiUi ui;unsigned i;
    fixture_init();hmi_ui_init(&ui,(HmiDisplay){collect,actual});verify(&ui,153600);
    {HmiState before=ui.state;ui.state.output_voltage=12345;
     hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,153600);
     before=ui.state;ui.state.output_voltage=1;
     hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,153600);}
    step(&ui,HMI_ACTION_PAGE,HMI_PAGE_HOME,0);
    open(&ui,HMI_DIALOG_HELP);
    hmi_ui_touch(&ui,150,300,1,1000);hmi_ui_touch(&ui,150,80,1,1050);
    hmi_ui_touch(&ui,150,80,0,1100);assert(ui.help_scroll==0);verify(&ui,0);
    step(&ui,HMI_ACTION_CUSTOM,501,117512);assert(ui.help_scroll==1);
    step(&ui,HMI_ACTION_CUSTOM,500,117512);assert(ui.help_scroll==0);
    step(&ui,HMI_ACTION_CLOSE,0,153600);
    /* Pending arrival/change/apply restores both lines without a full frame. */
    for(i=0;i<3;i++){
        HmiState before=ui.state;
        ui.state.pending_setpoints[i]=123.5f;ui.state.pending_mask|=(uint8_t)(1u<<i);
        hmi_diff_and_invalidate(&before,&ui.state);assert(verify(&ui,1975)>0);
        before=ui.state;ui.state.pending_setpoints[i]=2;
        hmi_diff_and_invalidate(&before,&ui.state);assert(verify(&ui,1975)>0);
        before=ui.state;ui.state.pending_mask=0;
        hmi_diff_and_invalidate(&before,&ui.state);assert(verify(&ui,1975)>0);
    }
    step(&ui,HMI_ACTION_SELECT_CONTROL,1,30000);
    step(&ui,HMI_ACTION_SELECT_CONTROL,2,30000);
    open(&ui,HMI_DIALOG_PANEL_CONTROL);
    for(i=0;i<3;i++){step(&ui,HMI_ACTION_TOGGLE,i,1500);step(&ui,HMI_ACTION_CUSTOM,110+i,3000);}
    /* Several non-overlapping changes must survive one queued render. */
    hmi_ui_dispatch(&ui,HMI_ACTION_TOGGLE,0);hmi_ui_dispatch(&ui,HMI_ACTION_TOGGLE,2);
    hmi_ui_dispatch(&ui,HMI_ACTION_CUSTOM,111);verify(&ui,6000);
    step(&ui,HMI_ACTION_EDIT,HMI_VALUE_MOD_MIN,153600);
    step(&ui,HMI_ACTION_KEY,'5',10000);step(&ui,HMI_ACTION_APPLY,0,153600);
    step(&ui,HMI_ACTION_APPLY,0,153600);
    for(i=HMI_DIALOG_PANEL_OUTPUT;i<=HMI_DIALOG_PANEL_MOTOR;i++){
        open(&ui,(HmiDialog)i);
        for(unsigned j=0;j<4;j++){step(&ui,HMI_ACTION_TOGGLE,j,1500);step(&ui,HMI_ACTION_CUSTOM,120+j,3000);}
        step(&ui,HMI_ACTION_APPLY,0,153600);
    }
    open(&ui,HMI_DIALOG_SENSOR_DISPLAY);
    for(i=0;i<8;i++)step(&ui,HMI_ACTION_TOGGLE,i,1600);
    step(&ui,HMI_ACTION_CANCEL,0,153600);
    open(&ui,HMI_DIALOG_FILTER);
    for(i=0;i<3;i++)step(&ui,HMI_ACTION_FILTER,i,400);
    step(&ui,HMI_ACTION_CANCEL,0,153600);
    open(&ui,HMI_DIALOG_AXIS);
    step(&ui,HMI_ACTION_CUSTOM,100,2300);step(&ui,HMI_ACTION_CUSTOM,101,2300);
    step(&ui,HMI_ACTION_CANCEL,0,153600);
    open(&ui,HMI_DIALOG_TIME);
    step(&ui,HMI_ACTION_TIME_PART,0,0);
    step(&ui,HMI_ACTION_KEY,'9',18000);step(&ui,HMI_ACTION_KEY,'9',18000);
    step(&ui,HMI_ACTION_APPLY,0,18000);assert(ui.edit_error);
    step(&ui,HMI_ACTION_KEY,'C',18000);assert(!ui.edit_error);
    step(&ui,HMI_ACTION_TIME_PART,1,18000);step(&ui,HMI_ACTION_APPLY,0,153600);
    step(&ui,HMI_ACTION_EDIT,HMI_VALUE_MAINS_VOLTAGE,153600);
    step(&ui,HMI_ACTION_KEY,'9',10000);step(&ui,HMI_ACTION_APPLY,0,10000);assert(ui.edit_error);
    step(&ui,HMI_ACTION_KEY,'2',10000);assert(!ui.edit_error);
    step(&ui,HMI_ACTION_KEY,'.',10000);step(&ui,HMI_ACTION_KEY,'.',0);
    step(&ui,HMI_ACTION_CANCEL,0,153600);
    step(&ui,HMI_ACTION_ROM,0,153600);
    step(&ui,HMI_ACTION_KEY,'A',9000);step(&ui,HMI_ACTION_APPLY,0,9000);
    step(&ui,HMI_ACTION_KEY,'<',9000);step(&ui,HMI_ACTION_CANCEL,0,153600);
    step(&ui,HMI_ACTION_CLOSE,0,153600);
    step(&ui,HMI_ACTION_PAGE,HMI_PAGE_PARAMETERS,153600);
    step(&ui,HMI_ACTION_MODE,HMI_DRIVE_VF,6000);
    step(&ui,HMI_ACTION_MODE,HMI_DRIVE_VF,0);
    step(&ui,HMI_ACTION_SECTION,HMI_PARAM_INVERTER,153600);
    step(&ui,HMI_ACTION_CUSTOM,12,6000);
    step(&ui,HMI_ACTION_CUSTOM,12,0);
    step(&ui,HMI_ACTION_SECTION,HMI_PARAM_MOTOR,153600);step(&ui,HMI_ACTION_CUSTOM,21,6000);
    step(&ui,HMI_ACTION_SECTION,HMI_PARAM_COMMUNICATION,153600);step(&ui,HMI_ACTION_CUSTOM,30,5800);
    step(&ui,HMI_ACTION_AUTO_START,0,0);step(&ui,HMI_ACTION_AUTO_CANCEL,0,0);
    step(&ui,HMI_ACTION_PAGE,HMI_PAGE_GRAPHS,153600);
    step(&ui,HMI_ACTION_GRAPH_ZOOM,1,1600);step(&ui,HMI_ACTION_GRAPH_RUN,0,6000);
    for(i=0;i<4;i++)step(&ui,HMI_ACTION_CUSTOM,200+i,90000);
    step(&ui,HMI_ACTION_PAGE,HMI_PAGE_JOURNAL,153600);
    step(&ui,HMI_ACTION_JOURNAL_EXPORT,0,0);
    step(&ui,HMI_ACTION_JOURNAL_PAGE,-1,0);
    step(&ui,HMI_ACTION_JOURNAL_PAGE,1,153600);step(&ui,HMI_ACTION_JOURNAL_PAGE,1,1000);
    /* A page change during flush must stop the obsolete frame after one
     * tile and preserve the new invalidation for the next render. */
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_HOME);
    pixels=0;switch_during_flush=&ui;hmi_ui_render(&ui);
    assert(pixels>0&&pixels<=2560&&hmi_dirty_pending());
    verify(&ui,153600);
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_HOME);
    ui.state.telemetry_flags=129;ui.state.power_state=HMI_POWER_RUN;
    hmi_invalidate_all();verify(&ui,153600);
    for(i=0;i<3;i++){
        HmiState before=ui.state;ui.state.selected_control=(uint8_t)i;ui.state.control_warning_mask=(uint8_t)(1u<<i);
        hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,30000);
        if(i!=0)for(unsigned x=12;x<307;x++)assert(actual[201*320+x]!=36454u&&actual[201*320+x]!=34276u);
    }
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_GRAPHS);verify(&ui,153600);
    {HmiState before=ui.state;ui.state.rotation_set=77;ui.state.pending_setpoints[1]=88;
     ui.state.pending_mask=2;hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,30000);}
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_JOURNAL);verify(&ui,153600);
    step(&ui,HMI_ACTION_EDIT,HMI_VALUE_JOURNAL_DATE,153600);
    assert(strcmp(ui.edit,"260101")==0);
    strcpy(ui.edit,"260831");step(&ui,HMI_ACTION_APPLY,0,153600);
    assert(ui.state.dialog==HMI_DIALOG_CONFIRM);
    step(&ui,HMI_ACTION_JOURNAL_CLEAR,0,153600);
    assert(ui.last_event.type==HMI_EVENT_JOURNAL_CLEAR&&ui.last_event.value==260831);
    /* Selection is blue-gray; regulation changes text only on secondary tabs. */
    for(i=HMI_PAGE_GRAPHS;i<=HMI_PAGE_JOURNAL;i++){
        HmiHit hit;HmiState before;
        step(&ui,HMI_ACTION_PAGE,(int)i,153600);
        assert(hmi_ui_hit_test(&ui,150,45,&hit)&&hit.action==HMI_ACTION_SELECT_CONTROL&&hit.argument==1);
        before=ui.state;ui.state.control_warning_mask=2;ui.state.rotation_set+=1;
        hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,40000);
        assert(actual[37*320+110]!=12610u);
        {unsigned amber=0;for(unsigned y=35;y<64;y++)for(unsigned x=103;x<205;x++){unsigned c=actual[y*320+x];amber+=(c>>11)>20&&((c>>5)&63)>25&&(c&31)<8;}assert(amber>0);}
        step(&ui,HMI_ACTION_SELECT_CONTROL,1,20000);
        assert(actual[35*320+110]==21326u);
        step(&ui,HMI_ACTION_SELECT_CONTROL,2,20000);
        before=ui.state;ui.state.control_warning_mask=0;
        hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,40000);
        assert(actual[37*320+110]!=12610u);
    }
    /* Updating journal entries must preserve every footer button pixel. */
    {static uint16_t footer[320*35];static const HmiJournalItem entries[]={{"08.09.2026 12:00","info","Start"}};
     memcpy(footer,actual+394*320,sizeof(footer));ui.state.journal=entries;ui.state.journal_count=1;
     hmi_invalidate_all();verify(&ui,153600);assert(!memcmp(footer,actual+394*320,sizeof(footer)));}
    step(&ui,HMI_ACTION_CUSTOM,610,153600);
    assert(ui.state.dialog==HMI_DIALOG_CONFIRM&&ui.parameters[HMI_VALUE_JOURNAL_DATE]<0);
    step(&ui,HMI_ACTION_CUSTOM,611,153600);
    step(&ui,HMI_ACTION_JOURNAL_CLEAR,0,153600);
    assert(ui.last_event.type==HMI_EVENT_JOURNAL_CLEAR&&ui.last_event.value==0);
    step(&ui,HMI_ACTION_CUSTOM,610,153600);step(&ui,HMI_ACTION_CUSTOM,612,153600);
    assert(ui.state.dialog==HMI_DIALOG_KEYPAD&&ui.edit_parameter==HMI_VALUE_JOURNAL_DATE);
    step(&ui,HMI_ACTION_CLOSE,0,153600);step(&ui,HMI_ACTION_PAGE,HMI_PAGE_HOME,153600);
    open(&ui,HMI_DIALOG_TIME);
    {static uint16_t dialog[320*480];HmiState before=ui.state;
     memcpy(dialog,actual,sizeof(dialog));ui.state.precharge_seconds=9.8f;ui.state.dc_bus_voltage=100;
     hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,20000);
     assert(!memcmp(dialog,actual,sizeof(dialog)));}
    step(&ui,HMI_ACTION_CLOSE,0,153600);
    step(&ui,HMI_ACTION_PAGE,HMI_PAGE_PARAMETERS,153600);
    {const int sections[]={HMI_PARAM_MOTOR,HMI_PARAM_EQUIVALENT,HMI_PARAM_MOTOR,HMI_PARAM_PROTECTIONS,HMI_PARAM_TEMPERATURE,HMI_PARAM_NETWORK,HMI_PARAM_PROTECTIONS,HMI_PARAM_AUTO};
     for(unsigned j=0;j<sizeof(sections)/sizeof(sections[0]);j++)step(&ui,HMI_ACTION_SECTION,sections[j],153600);}
    step(&ui,HMI_ACTION_PAGE,HMI_PAGE_HOME,153600);
    {HmiState before=ui.state;ui.state.power_state=HMI_POWER_RUN;ui.state.dynamic_values=1;
     hmi_diff_and_invalidate(&before,&ui.state);verify(&ui,153600);}
    open(&ui,HMI_DIALOG_HELP);step(&ui,HMI_ACTION_CLOSE,0,153600);
    printf("ui dirty: %u transitions pixel-identical to full frames, bounded writes and idle PASS\n",checks);
    return 0;
}
