#include "hmi_ui.h"
#include "hmi_gfx.h"
#include <stdio.h>
int main(void){
    printf("static assets: %lu bytes; renderer RAM estimate: %lu; HmiUi: %lu; pixel buffer: %lu\n",
        (unsigned long)hmi_static_data_bytes(),(unsigned long)hmi_working_ram_bytes(),
        (unsigned long)sizeof(HmiUi),(unsigned long)HMI_RENDER_BUFFER_BYTES);
    return 0;
}
