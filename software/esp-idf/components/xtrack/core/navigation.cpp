#include "navigation.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace xtrack {
namespace {
bool number(const char* s, double& out) {
    if (!s || !*s) return false;
    char* end=nullptr;
    out=std::strtod(s,&end);
    return end && !*end && std::isfinite(out);
}
bool integer(const char* s, int& out) {
    double value;
    if (!number(s,value) || value<0 || value>1000000 || std::floor(value)!=value) return false;
    out=int(value); return true;
}
bool coordinate(const char* s, const char* hemisphere, bool lat, double& out) {
    double raw;
    if (!number(s,raw) || raw<0 || raw>18000 || std::strlen(hemisphere)!=1) return false;
    const int degrees=int(raw/100);
    const double minutes=raw-degrees*100;
    if (minutes>=60 || degrees>(lat?90:180)) return false;
    out=degrees+minutes/60;
    if (out>(lat?90:180)) return false;
    if (*hemisphere==(lat?'S':'W')) out=-out;
    else if (*hemisphere!=(lat?'N':'E')) return false;
    return true;
}
int epoch(const char* s) {
    double value;
    if (!number(s,value) || value<0 || value>=240000) return -1;
    int v=int(value), h=v/10000, m=v/100%100, sec=v%100;
    if(h>23 || m>59 || sec>59) return -1;
    return h*3600+m*60+sec;
}
int hex(char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='A'&&c<='F') return c-'A'+10;
    if(c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
}
bool valid_date(int y,int m,int d) {
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<2020 || y>2099 || m<1 || m>12 || d<1) return false;
    int max=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
    return d<=max;
}
bool NmeaParser::feed(char c,uint64_t now) {
    if(c=='$') { size_=0; line_[size_++]=c; return false; }
    if(!size_) return false;
    if(c=='\r') return false;
    if(c=='\n') {
        line_[size_]=0; size_=0;
        bool ok=sentence(now); if(ok) ++accepted_; else ++rejected_;
        return ok;
    }
    if(size_+1>=sizeof(line_)) { size_=0; ++rejected_; return false; }
    line_[size_++]=c; return false;
}
bool NmeaParser::sentence(uint64_t now) {
    char* star=std::strchr(line_,'*');
    if(!star || std::strlen(star)!=3 || hex(star[1])<0 || hex(star[2])<0) return false;
    unsigned crc=0; for(char* p=line_+1;p<star;++p) crc^=uint8_t(*p);
    if(crc!=unsigned(hex(star[1])*16+hex(star[2]))) return false;
    *star=0;
    char* field[24]{}; int count=0;
    field[count++]=line_+1;
    for(char* p=line_+1;*p && count<24;++p) if(*p==',') {*p=0;field[count++]=p+1;}
    if(std::strlen(field[0])!=5) return false;
    const char* type=field[0]+2;
    auto invalid=[this](){complete_.valid=false;return false;};
    if(std::strcmp(type,"RMC")==0) {
        rmc_valid_=false;
        if(count<10) return invalid();
        rmc_epoch_=epoch(field[1]);
        if(std::strcmp(field[2],"A") || rmc_epoch_<0){complete_.valid=false;return true;}
        double lat,lon,speed=0,course=0;
        if(!coordinate(field[3],field[4],true,lat) || !coordinate(field[5],field[6],false,lon)) return invalid();
        if(!number(field[7],speed) || speed<0 || speed>300) return invalid();
        if(*field[8] && (!number(field[8],course) || course<0 || course>360)) return invalid();
        int date;
        if(std::strlen(field[9])!=6 || !integer(field[9],date)) return invalid();
        int year=2000+date%100,month=date/100%100,day=date/10000;
        if(!valid_date(year,month,day)) return invalid();
        const bool new_epoch=fix_.sequence==0 || fix_.year!=year || fix_.month!=month || fix_.day!=day
            || fix_.hour*3600+fix_.minute*60+fix_.second!=rmc_epoch_;
        if(new_epoch){++fix_.sequence;rmc_ms_=now;}
        fix_.latitude=lat;fix_.longitude=lon;fix_.speed_kmh=speed*1.852;fix_.course=course;
        fix_.year=year;fix_.month=month;fix_.day=day;
        fix_.hour=rmc_epoch_/3600;fix_.minute=rmc_epoch_/60%60;fix_.second=rmc_epoch_%60;
        rmc_valid_=true;commit(now);return true;
    }
    if(std::strcmp(type,"GGA")==0) {
        gga_valid_=false;
        if(count<11) return invalid();
        gga_epoch_=epoch(field[1]);gga_ms_=now;
        int quality=0,sats=0; double hdop=99,alt=0;
        if(!integer(field[6],quality) || !integer(field[7],sats) || sats>99) return invalid();
        fix_.satellites=sats;
        if(!quality){complete_.valid=false;complete_.satellites=sats;return true;}
        double lat,lon;
        if(!coordinate(field[2],field[3],true,lat) || !coordinate(field[4],field[5],false,lon)) return invalid();
        if(!number(field[8],hdop) || !number(field[9],alt) || alt < -12000 || alt > 100000 || std::strcmp(field[10],"M")) return invalid();
        gga_lat_=lat;gga_lon_=lon;
        fix_.altitude=alt;fix_.hdop=hdop;
        gga_valid_=gga_epoch_>=0 && sats>=3 && hdop>0 && hdop<=10 && quality<=5;
        if(!gga_valid_)complete_.valid=false;
        commit(now);
        return true;
    }
    return true; // Valid but unused NMEA sentence, e.g. GSV/VTG.
}
void NmeaParser::commit(uint64_t now) {
    // A receiver can send many GSV sentences between GGA and RMC. Keep the last
    // coherent fix while assembling the next pair, so trip integration never
    // mistakes that normal UART interval for a real loss of positioning.
    if(!rmc_valid_ || !gga_valid_ || rmc_epoch_!=gga_epoch_ || now<rmc_ms_ || now<gga_ms_
        || now-rmc_ms_>2500 || now-gga_ms_>2500 || fix_.sequence==complete_.sequence)return;
    if(distance_m(fix_.latitude,fix_.longitude,gga_lat_,gga_lon_)>100){complete_.valid=false;return;}
    complete_=fix_;complete_.valid=true;complete_ms_=std::min(rmc_ms_,gga_ms_);
}
Fix NmeaParser::snapshot(uint64_t now,uint32_t max_age) const {
    Fix out=complete_;
    out.valid=complete_.valid && now>=complete_ms_ && now-complete_ms_<=max_age;
    if(now<complete_ms_ || now-complete_ms_>max_age) out.satellites=0;
    if(!out.valid) out.speed_kmh=0;
    return out;
}
double distance_m(double a,double b,double c,double d) {
    constexpr double rad=3.14159265358979323846/180;
    double s=std::sin((c-a)*rad/2),t=std::sin((d-b)*rad/2);
    double h=std::clamp(s*s+std::cos(a*rad)*std::cos(c*rad)*t*t,0.0,1.0);
    return 6371008.8*2*std::asin(std::sqrt(h));
}
void Trip::reset(){*this=Trip{};}
void Trip::update(const Fix& fix,uint64_t now) {
    if(!fix.valid) {anchored_=false;return;}
    if(anchored_ && fix.sequence==previous_.sequence) return;
    if(anchored_ && now>previous_ms_ && now-previous_ms_<=5000) {
        double dt=(now-previous_ms_)/1000.0;
        double distance=distance_m(previous_.latitude,previous_.longitude,fix.latitude,fix.longitude);
        // Reject impossible jumps; re-anchor after an outage instead of bridging it.
        double bound=std::max(20.0,fix.speed_kmh/3.6*dt*2+10);
        if(fix.speed_kmh>=1.5 && distance<=bound && distance/dt<60) {
            meters+=distance;moving_seconds+=dt;max_kmh=std::max(max_kmh,fix.speed_kmh);
        }
    }
    previous_=fix;previous_ms_=now;anchored_=true;
}
}
