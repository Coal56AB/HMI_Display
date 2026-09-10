/* Shared host test/export scene context. */
static void raster_context(HmiUi *ui,unsigned id){
    static const HmiDialog dialogs[]={HMI_DIALOG_AXIS,HMI_DIALOG_CONFIRM,HMI_DIALOG_FILTER,
        HMI_DIALOG_KEYPAD,HMI_DIALOG_PANEL_CONTROL,HMI_DIALOG_PANEL_MOTOR,HMI_DIALOG_PANEL_OUTPUT,
        HMI_DIALOG_ROM,HMI_DIALOG_SENSOR_DISPLAY,HMI_DIALOG_SENSOR_SCHEMES,HMI_DIALOG_TIME};
    static const HmiParamSection params[]={HMI_PARAM_AUTO_DONE,HMI_PARAM_AUTO_RUNNING,HMI_PARAM_AUTO,
        HMI_PARAM_CALIBRATION,HMI_PARAM_COMMUNICATION,HMI_PARAM_INVERTER,HMI_PARAM_MOTOR,
        HMI_PARAM_PROTECTIONS,HMI_PARAM_SYSTEM};
    hmi_ui_init(ui,(HmiDisplay){discard,0});ui->state.scene_override=(HmiSceneId)id;
    if(id<=HMI_SCENE_DIALOG_TIME)ui->state.dialog=dialogs[id];
    else if(id<=HMI_SCENE_GRAPHS_SPEED)ui->state.page=HMI_PAGE_GRAPHS;
    else if(id<=HMI_SCENE_HELP_PARAMS)ui->state.dialog=HMI_DIALOG_HELP;
    else if(id>=HMI_SCENE_PARAMS_AUTO_DONE){ui->state.page=HMI_PAGE_PARAMETERS;ui->state.param_section=params[id-HMI_SCENE_PARAMS_AUTO_DONE];}
    else if(id>=HMI_SCENE_JOURNAL_PAGE1)ui->state.page=HMI_PAGE_JOURNAL;
}
