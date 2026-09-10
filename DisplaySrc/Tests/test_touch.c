#include "storage_fixture.h"
#include "hmi_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned pixels;
static void flush(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p,uint16_t stride,void *user){
    (void)user;assert(p&&stride>=w&&x+w<=320&&y+h<=480);pixels+=(unsigned)w*h;
}
static void tap(HmiUi *ui,int x,int y,uint32_t time){hmi_ui_touch(ui,(int16_t)x,(int16_t)y,1,time);hmi_ui_touch(ui,-1,-1,0,time+30u);}
static void type(HmiUi *ui,const char *text){for(;*text;text++)hmi_ui_dispatch(ui,HMI_ACTION_KEY,*text);}
int main(void){
    fixture_init();
    HmiUi ui;HmiEvent e;HmiHit h;unsigned i;
    const HmiTouchRegion region={HMI_SCENE_COUNT,{{0,0,20,20},HMI_ACTION_CUSTOM,42}};
    hmi_ui_init(&ui,(HmiDisplay){flush,NULL});hmi_ui_render(&ui);assert(pixels==153600);
    pixels=0;hmi_ui_render(&ui);assert(!pixels);
    tap(&ui,200,455,100);assert(ui.state.page==HMI_PAGE_PARAMETERS);
    assert(hmi_ui_poll_event(&ui,&e)&&e.type==HMI_EVENT_NAVIGATED&&e.id==HMI_PAGE_PARAMETERS);
    tap(&ui,270,109,200);assert(ui.state.dialog==HMI_DIALOG_KEYPAD);
    type(&ui,"230");tap(&ui,230,390,300);
    assert(ui.state.dialog==HMI_DIALOG_NONE&&ui.parameters[HMI_VALUE_MAINS_VOLTAGE]==230);
    assert(hmi_ui_poll_event(&ui,&e)&&e.type==HMI_EVENT_VALUE_CHANGED&&e.value==230);
    hmi_ui_dispatch(&ui,HMI_ACTION_EDIT,HMI_VALUE_MAINS_VOLTAGE);type(&ui,"999");
    hmi_ui_dispatch(&ui,HMI_ACTION_APPLY,0);assert(ui.edit_error&&ui.state.dialog==HMI_DIALOG_KEYPAD);
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);assert(ui.parameters[HMI_VALUE_MAINS_VOLTAGE]==230);
    hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,HMI_DIALOG_FILTER);
    tap(&ui,30,190,400);assert(ui.draft_flags==6);
    tap(&ui,40,455,500);assert(ui.state.dialog==HMI_DIALOG_FILTER); /* modal consumes nav */
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);assert(ui.settings[4]==7);
    hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,HMI_DIALOG_FILTER);
    hmi_ui_dispatch(&ui,HMI_ACTION_FILTER,1);hmi_ui_dispatch(&ui,HMI_ACTION_APPLY,0);assert(ui.settings[4]==5);
    hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,HMI_DIALOG_TIME);type(&ui,"23");
    hmi_ui_dispatch(&ui,HMI_ACTION_TIME_PART,1);type(&ui,"59");hmi_ui_dispatch(&ui,HMI_ACTION_APPLY,0);
    assert(strcmp(ui.clock,"23:59")==0);
    hmi_ui_dispatch(&ui,HMI_ACTION_ROM,3);type(&ui,"28FF641D2B16037C");
    hmi_ui_dispatch(&ui,HMI_ACTION_APPLY,0);assert(ui.rom[3][0]==0x28&&ui.rom[3][7]==0x7c);
    hmi_ui_dispatch(&ui,HMI_ACTION_CLOSE,0);
    hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,HMI_DIALOG_PANEL_CONTROL);
    tap(&ui,100,165,540);assert(ui.state.dialog==HMI_DIALOG_KEYPAD);
    type(&ui,"5");hmi_ui_dispatch(&ui,HMI_ACTION_APPLY,0);
    assert(ui.state.dialog==HMI_DIALOG_PANEL_CONTROL&&ui.parameters[HMI_VALUE_MOD_MIN]==0);
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);assert(ui.parameters[HMI_VALUE_MOD_MIN]==0);
    hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,HMI_DIALOG_PANEL_CONTROL);
    tap(&ui,100,165,580);type(&ui,"5");hmi_ui_dispatch(&ui,HMI_ACTION_APPLY,0);
    tap(&ui,220,380,620);assert(ui.state.dialog==HMI_DIALOG_NONE&&ui.parameters[HMI_VALUE_MOD_MIN]==5);
    hmi_ui_touch(&ui,40,455,1,600);hmi_ui_touch(&ui,40,455,0,601);assert(ui.state.page==HMI_PAGE_PARAMETERS);
    hmi_ui_touch(&ui,40,455,1,700);hmi_ui_touch(&ui,80,455,1,725);hmi_ui_touch(&ui,40,455,0,760);
    assert(ui.state.page==HMI_PAGE_PARAMETERS); /* drag cannot activate */
    tap(&ui,40,455,UINT32_MAX-10u);assert(ui.state.page==HMI_PAGE_HOME); /* timer wrap */
    assert(!hmi_ui_hit_test(&ui,-1,20,&h)&&!hmi_ui_hit_test(&ui,320,20,&h));
    hmi_ui_set_regions(&ui,&region,1);tap(&ui,10,10,800);assert(ui.last_event.type==HMI_EVENT_CUSTOM&&ui.last_event.id==42);
    hmi_ui_set_regions(&ui,NULL,0);
    while(hmi_ui_poll_event(&ui,&e)){}
    for(i=0;i<HMI_UI_EVENT_CAPACITY+3u;i++)hmi_ui_dispatch(&ui,HMI_ACTION_CUSTOM,(int16_t)i);
    assert(ui.event_count==HMI_UI_EVENT_CAPACITY&&ui.event_overflows==3);
    assert(hmi_ui_poll_event(&ui,&e)&&e.id==3);
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_GRAPHS);
    tap(&ui,150,200,2200);assert(ui.state.dialog==HMI_DIALOG_NONE);
    tap(&ui,150,200,2400);assert(ui.state.dialog==HMI_DIALOG_AXIS);
    hmi_ui_dispatch(&ui,HMI_ACTION_CLOSE,0);
    /* Double taps work across the entire card body. */
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_HOME);
    {static const int points[][3]={{20,310,HMI_DIALOG_PANEL_OUTPUT},{140,410,HMI_DIALOG_PANEL_OUTPUT},
        {180,320,HMI_DIALOG_PANEL_MOTOR},{300,410,HMI_DIALOG_PANEL_MOTOR},
        {20,195,HMI_DIALOG_PANEL_CONTROL},{290,245,HMI_DIALOG_PANEL_CONTROL}};
     for(i=0;i<sizeof(points)/sizeof(points[0]);i++){
        assert(hmi_ui_hit_test(&ui,(int16_t)points[i][0],(int16_t)points[i][1],&h));
        assert(h.action==HMI_ACTION_DIALOG&&h.argument==points[i][2]);
        tap(&ui,points[i][0],points[i][1],3000+i*1000);
        assert(ui.state.dialog==HMI_DIALOG_NONE);
        tap(&ui,points[i][0],points[i][1],3200+i*1000);
        assert(ui.state.dialog==(HmiDialog)points[i][2]);hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);
     }}
    tap(&ui,180,190,10000);assert(ui.state.dialog==HMI_DIALOG_NONE&&ui.state.selected_control==0);
    tap(&ui,20,310,12000);tap(&ui,20,310,12400);assert(ui.state.dialog==HMI_DIALOG_NONE);
    tap(&ui,120,310,12500);assert(ui.state.dialog==HMI_DIALOG_NONE); /* too far */
    tap(&ui,120,310,12700);assert(ui.state.dialog==HMI_DIALOG_PANEL_OUTPUT);
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);
    tap(&ui,20,310,UINT32_MAX-100u);tap(&ui,20,310,30);
    assert(ui.state.dialog==HMI_DIALOG_PANEL_OUTPUT); /* wrap-safe interval */
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);
    tap(&ui,241,17,1800);assert(ui.state.dialog==HMI_DIALOG_HELP);
    hmi_ui_touch(&ui,150,300,1,11000);hmi_ui_touch(&ui,150,80,1,11050);
    hmi_ui_touch(&ui,150,80,0,11100);
    assert(ui.state.dialog==HMI_DIALOG_HELP&&ui.help_scroll==0);
    tap(&ui,272,419,11120);assert(ui.help_scroll==1);
    tap(&ui,45,419,11160);assert(ui.help_scroll==0);
    tap(&ui,150,420,11200);assert(ui.state.dialog==HMI_DIALOG_NONE);
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_PARAMETERS);
    hmi_ui_dispatch(&ui,HMI_ACTION_SECTION,HMI_PARAM_SYSTEM);
    tap(&ui,205,199,1900);assert(ui.state.drive_mode==HMI_DRIVE_SF&&ui.state.dialog==HMI_DIALOG_NONE);
    tap(&ui,275,199,2000);assert(ui.state.drive_mode==HMI_DRIVE_VF);
    tap(&ui,145,199,2100);assert(ui.state.drive_mode==HMI_DRIVE_UF);
    hmi_ui_dispatch(&ui,HMI_ACTION_SECTION,HMI_PARAM_INVERTER);
    tap(&ui,140,109,2200);assert(ui.choices[0]==0&&ui.state.dialog==HMI_DIALOG_NONE);
    tap(&ui,275,109,2300);assert(ui.choices[0]==2);
    hmi_ui_dispatch(&ui,HMI_ACTION_SECTION,HMI_PARAM_MOTOR);
    tap(&ui,260,109,2400);assert(ui.choices[1]==1&&ui.state.dialog==HMI_DIALOG_NONE);
    tap(&ui,160,109,2500);assert(ui.choices[1]==0);
    hmi_ui_dispatch(&ui,HMI_ACTION_PAGE,HMI_PAGE_JOURNAL);
    hmi_ui_dispatch(&ui,HMI_ACTION_CUSTOM,610);
    hmi_ui_dispatch(&ui,HMI_ACTION_CUSTOM,612);
    assert(ui.state.dialog==HMI_DIALOG_KEYPAD);
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);
    assert(ui.state.dialog==HMI_DIALOG_CONFIRM&&ui.parameters[HMI_VALUE_JOURNAL_DATE]<0);
    hmi_ui_dispatch(&ui,HMI_ACTION_CANCEL,0);
    assert(ui.state.dialog==HMI_DIALOG_NONE);
    {HmiUi journal_ui;static const HmiJournalItem entries[15]={{0}};
     hmi_ui_init(&journal_ui,(HmiDisplay){flush,0});
     hmi_ui_dispatch(&journal_ui,HMI_ACTION_PAGE,HMI_PAGE_JOURNAL);
     for(i=0;i<100;i++)hmi_ui_dispatch(&journal_ui,HMI_ACTION_JOURNAL_PAGE,1);
     assert(journal_ui.state.journal_page==2);
     journal_ui.state.journal=entries;journal_ui.state.journal_count=15;
     for(i=0;i<100;i++)hmi_ui_dispatch(&journal_ui,HMI_ACTION_JOURNAL_PAGE,1);
     assert(journal_ui.state.journal_page==3);
     journal_ui.state.journal_count=0;
     hmi_ui_dispatch(&journal_ui,HMI_ACTION_JOURNAL_PAGE,1);
     assert(journal_ui.state.journal_page==1);
     hmi_ui_dispatch(&journal_ui,HMI_ACTION_JOURNAL_PAGE,-1);
     assert(journal_ui.state.journal_page==1);}
    /* Every modal can be rendered through the same transport callback. */
    for(i=HMI_DIALOG_KEYPAD;i<=HMI_DIALOG_HELP;i++){
        hmi_ui_dispatch(&ui,HMI_ACTION_DIALOG,(int16_t)i);hmi_ui_render(&ui);
    }
    puts("touch: navigation, editing, validation, cancel, modal, time, ROM, debounce, drag, overflow, render PASS");
    return 0;
}
