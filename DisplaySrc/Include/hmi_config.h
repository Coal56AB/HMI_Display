#ifndef HMI_CONFIG_H
#define HMI_CONFIG_H

#ifndef HMI_EXTERNAL_ASSETS
#define HMI_EXTERNAL_ASSETS 0
#endif
#ifndef HMI_LITE
#define HMI_LITE 0
#endif
#if HMI_EXTERNAL_ASSETS && HMI_LITE
#error "Choose either full external assets or lite internal assets"
#endif
#define HMI_STORAGE_VIRTUAL (HMI_EXTERNAL_ASSETS || HMI_LITE)
#endif
