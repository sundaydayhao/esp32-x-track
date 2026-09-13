#include "gpx_writer.hpp"
#include <cstring>
#include <cerrno>
#include <cmath>
#include <unistd.h>
namespace xtrack {
static constexpr const char* footer="</trkseg></trk></gpx>\n";
GpxWriter::~GpxWriter(){if(file_) std::fclose(file_);}
bool GpxWriter::write(const char* text) {
    if(!file_) return false;
    size_t length=std::strlen(text);
    if(std::fwrite(text,1,length,file_)!=length || std::fflush(file_) || fsync(fileno(file_))) {
        error_=std::strerror(errno);std::fclose(file_);file_=nullptr;return false;
    }
    return true;
}
bool GpxWriter::start(const std::string& path) {
    if(file_) {error_="Recording already open";return false;}
    error_.clear();path_=path;paused_=false;
    if(access(path.c_str(),F_OK)==0 || access((path+".part").c_str(),F_OK)==0) {
        error_="Track already exists";return false;
    }
    file_=std::fopen((path+".part").c_str(),"wb");
    if(!file_) {error_=std::strerror(errno);return false;}
    return write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"X-Track ESP-IDF\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n<trk><name>X-Track ride</name><trkseg>\n");
}
bool GpxWriter::point(const Fix& f) {
    if(!active() || !f.valid || !valid_date(f.year,f.month,f.day) || !std::isfinite(f.latitude)
       || !std::isfinite(f.longitude) || !std::isfinite(f.altitude) || std::abs(f.latitude)>90
       || std::abs(f.longitude)>180 || f.hour<0 || f.hour>23 || f.minute<0 || f.minute>59
       || f.second<0 || f.second>59) return false;
    char line[320];
    int n=std::snprintf(line,sizeof(line),"<trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.2f</ele><time>%04d-%02d-%02dT%02d:%02d:%02dZ</time></trkpt>\n",
        f.latitude,f.longitude,f.altitude,f.year,f.month,f.day,f.hour,f.minute,f.second);
    if(n<0 || size_t(n)>=sizeof(line)){error_="Invalid track point size";return false;}
    return write(line);
}
bool GpxWriter::pause(){if(!file_) return false;paused_=true;return true;}
bool GpxWriter::resume(){if(!file_) return false;if(paused_ && !write("</trkseg><trkseg>\n")) return false;paused_=false;return true;}
bool GpxWriter::stop() {
    if(!file_) {
        // A later SD remount may already have recovered this failed journal.
        // Acknowledge only a finalized file; retain errors for unsaved data.
        if(!path_.empty() && access(path_.c_str(),F_OK)==0 && access((path_+".part").c_str(),F_OK)!=0)
            error_.clear();
        return error_.empty();
    }
    if(!write(footer)) return false;
    if(std::fclose(file_)!=0){file_=nullptr;error_=std::strerror(errno);return false;}
    file_=nullptr;
    if(std::rename((path_+".part").c_str(),path_.c_str())!=0){error_=std::strerror(errno);return false;}
    error_.clear();
    return true;
}
bool GpxWriter::recover(const std::string& part) {
    if(part.size()<5 || part.substr(part.size()-5)!=".part") return false;
    const std::string final=part.substr(0,part.size()-5);
    if(access(final.c_str(),F_OK)==0) return false;
    FILE* f=std::fopen(part.c_str(),"r+b");if(!f)return false;
    // Only salvage our journal, not arbitrary user files. Discard the incomplete last line.
    char head[256]{};size_t read=std::fread(head,1,sizeof(head)-1,f);
    if(!read || !std::strstr(head,"creator=\"X-Track ESP-IDF\"")) {std::fclose(f);return false;}
    std::fseek(f,0,SEEK_SET); long complete=0;bool started=false,closed=false;
    char line[512];
    while(std::fgets(line,sizeof(line),f)) {
        size_t n=std::strlen(line);
        if(!n || line[n-1]!='\n') break;
        if(std::strstr(line,"<trkseg>"))started=true;
        if(std::strstr(line,"</gpx>"))closed=true;
        complete=std::ftell(f);
    }
    bool ok=started && complete>0 && ftruncate(fileno(f),complete)==0 && std::fseek(f,complete,SEEK_SET)==0;
    if(ok && !closed)ok=std::fwrite(footer,1,std::strlen(footer),f)==std::strlen(footer);
    if(ok)ok=std::fflush(f)==0 && fsync(fileno(f))==0;
    if(std::fclose(f)!=0)ok=false;
    return ok && std::rename(part.c_str(),final.c_str())==0;
}
}
