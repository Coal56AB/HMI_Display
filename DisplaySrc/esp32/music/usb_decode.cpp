#include "music.h"
namespace music {
bool decode_usb(const uint8_t p[4],uint64_t t,Event &e) {
    uint8_t cin=p[0]&15,status=p[1]>>4;
    if(cin==15 && p[1]==0xff) {e={Type::Reset,0,0,0,t};return true;}
    if((cin!=8 && cin!=9 && cin!=11) || cin!=status || p[2]>127 || p[3]>127) return false;
    e={cin==8?Type::Off:cin==9?Type::On:Type::Control,uint8_t(p[1]&15),p[2],p[3],t,uint8_t(p[0]>>4)};
    if(e.type==Type::On && !e.velocity) e.type=Type::Off;
    return true;
}
bool find_endpoint(const uint8_t *d,size_t n,Endpoint &out) {
    if(n<9 || d[0]<9 || d[1]!=2) return false;
    size_t total=d[2]|(unsigned(d[3])<<8);
    if(total>n || total<9) return false;
    bool midi=false,version=false;uint8_t iface=0,alt=0;
    for(size_t i=0;i+2<=total;) {
        size_t len=d[i];if(len<2 || i+len>total) return false;
        if(d[i+1]==4) {
            if(len<9) return false;
            iface=d[i+2];alt=d[i+3];midi=d[i+5]==1 && d[i+6]==3 && d[i+7]==0;version=false;
        } else if(midi && d[i+1]==0x24 && len>=5 && d[i+2]==1) {
            version=d[i+3]==0 && d[i+4]==1;
        } else if(midi && version && d[i+1]==5 && len>=7 && (d[i+2]&0x80) && (d[i+3]&3)==2) {
            uint16_t size=d[i+4]|(uint16_t(d[i+5])<<8);
            if(!size || size>64 || size%4) return false;
            out={iface,alt,d[i+2],size};return true;
        }
        i+=len;
    }
    return false;
}
}
