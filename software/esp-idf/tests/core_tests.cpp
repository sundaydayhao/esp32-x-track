#include "navigation.hpp"
#include "gpx_writer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <fstream>
#include <filesystem>
#include <iostream>

static int checks=0;
static void check(bool condition,const char* message){++checks;if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
static std::string nmea(const std::string& payload){unsigned c=0;for(char b:payload)c^=uint8_t(b);char tail[8];std::snprintf(tail,sizeof(tail),"*%02X\r\n",c);return "$"+payload+tail;}
static void feed(xtrack::NmeaParser& parser,const std::string& text,uint64_t now){for(char c:text)parser.feed(c,now);}
static void pair(xtrack::NmeaParser& p,const std::string& time,const std::string& date,uint64_t now){
    feed(p,nmea("GNGGA,"+time+",3112.0000,N,12130.0000,E,1,12,0.9,32.5,M,0.0,M,,"),now);
    feed(p,nmea("GNRMC,"+time+",A,3112.0000,N,12130.0000,E,5.0,90.0,"+date+",,,A"),now);
}
int main(int argc,char** argv){
    using namespace xtrack;
    check(argc==2,"output directory required");std::filesystem::create_directories(argv[1]);
    NmeaParser parser;check(!parser.snapshot(0).valid,"startup is invalid");
    pair(parser,"020304.00","120926",1000);auto f=parser.snapshot(1000);
    check(f.valid,"GN multi-constellation RMC+GGA");
    check(std::abs(f.latitude-31.2)<1e-9 && std::abs(f.longitude-121.5)<1e-9,"coordinates parsed");
    check(f.year==2026 && f.month==9 && f.day==12 && f.hour==2,"UTC date parsed");
    check(std::abs(f.speed_kmh-9.26)<1e-9,"knots to km/h");
    pair(parser,"020304.00","120926",1200);
    check(parser.snapshot(1200).sequence==f.sequence,"duplicate UTC epoch does not add a point");
    pair(parser,"020304.00","120926",3600);
    check(!parser.snapshot(3600).valid,"frozen GPS timestamp expires despite repeated input");
    check(!parser.snapshot(3501).valid && parser.snapshot(3501).satellites==0,"disconnect times out");
    feed(parser,nmea("GNRMC,020305.00,V,,,,,,,120926,,,N"),1100);
    check(!parser.snapshot(1100).valid,"void status invalidates immediately");
    pair(parser,"020306.00","120926",2000);
    feed(parser,nmea("GNGGA,020306.00,3112.0000,N,12130.0000,E,0,00,,,,,,"),2001);
    check(!parser.snapshot(2001).valid,"GGA quality zero invalidates");
    pair(parser,"020307.00","120926",3000);
    feed(parser,nmea("GPRMC,020308.00,A,3112.0000,N,12130.0000,E,5,90,120926,,,A"),3100);
    check(parser.snapshot(3100).valid && parser.snapshot(3100).second==7,"incomplete epoch keeps last coherent pair");
    feed(parser,nmea("GPGGA,020308.00,3112.0000,N,12130.0000,E,1,12,0.9,35,M,0,M,,"),3400);
    check(parser.snapshot(3400).valid && parser.snapshot(3400).second==8 && parser.snapshot(3400).altitude==35,"delayed GGA completes new epoch atomically");
    NmeaParser mismatch;
    feed(mismatch,nmea("GNRMC,020304,A,3112.000,N,12130.000,E,5,90,120926,,,A"),1000);
    feed(mismatch,nmea("GNGGA,020305,3112.000,N,12130.000,E,1,12,1,2,M,0,M,,"),1000);
    check(!mismatch.snapshot(1000).valid,"mismatched epochs cannot create the first fix");
    feed(mismatch,nmea("GNGGA,020304,3212.000,N,12130.000,E,1,12,1,2,M,0,M,,"),1000);
    check(!mismatch.snapshot(1000).valid,"inconsistent coordinates in a pair are rejected");
    NmeaParser bad;auto damaged=nmea("GNRMC,020304,A,3112.0000,N,12130.0000,E,5,90,120926,,,A");damaged[8]='9';feed(bad,damaged,1000);
    check(bad.rejected()==1 && !bad.snapshot(1000).valid,"bad checksum rejected");
    feed(bad,"$"+std::string(300,'X')+"\r\n",1000);pair(bad,"020304","120926",1001);
    check(bad.snapshot(1001).valid,"oversized input resynchronizes");
    NmeaParser negative;feed(negative,nmea("GPGGA,020304,3351.000,S,15112.000,W,1,8,1,2,M,0,M,,"),1000);
    feed(negative,nmea("GPRMC,020304,A,3351.000,S,15112.000,W,0,0,120926,,,A"),1000);
    check(negative.snapshot(1000).valid && negative.snapshot(1000).latitude<0 && negative.snapshot(1000).longitude<0,"south/west signs");
    feed(negative,nmea("GPRMC,020305,A,1e300,S,15112.000,W,0,0,120926,,,A"),1100);
    check(!negative.snapshot(1100).valid,"oversized numeric coordinate rejected before integer conversion");
    NmeaParser poor;pair(poor,"020304","120926",1000);
    feed(poor,nmea("GNGGA,020304,3112.000,N,12130.000,E,1,12,20,32.5,M,0,M,,"),1000);
    check(!poor.snapshot(1000).valid,"poor HDOP rejected");
    feed(poor,nmea("GNGGA,020304,3112.000,N,12130.000,E,1,12,1,1e300,M,0,M,,"),1000);
    check(!poor.snapshot(1000).valid,"out of range altitude rejected");
    check(!valid_date(2026,2,29) && valid_date(2028,2,29),"calendar leap years");
    check(std::abs(distance_m(0,0,0,1)-111195.08)<1,"haversine reference");
    Trip trip;f.valid=true;f.sequence=1;f.speed_kmh=20;trip.update(f,1000);
    f.latitude+=0.00005;f.sequence=2;trip.update(f,2000);
    check(trip.meters>5 && trip.meters<6,"real position accumulates meters");
    double meters=trip.meters;trip.update(f,2100);check(trip.meters==meters && trip.moving_seconds==1,"duplicate fix not integrated twice");
    f.valid=false;trip.update(f,2500);f.valid=true;f.latitude+=0.01;f.sequence=3;trip.update(f,3500);
    check(trip.meters==meters,"no bridge across GPS outage");
    f.latitude+=1;f.sequence=4;trip.update(f,4500);check(trip.meters==meters,"impossible jump discarded");
    std::string root=argv[1];auto output=root+"/normal.gpx";
    std::filesystem::remove(output);std::filesystem::remove(output+".part");
    f.latitude=31.2;f.longitude=121.5;
    GpxWriter writer;check(writer.start(output),"create GPX journal");
    check(!writer.start(output),"duplicate start rejected");
    check(writer.point(f),"write valid UTC point");
    check(writer.pause() && !writer.point(f),"pause suppresses points");
    check(writer.resume() && writer.point(f),"resume creates segment");
    check(writer.stop() && writer.stop(),"stop is safe twice");
    check(std::filesystem::exists(output) && !std::filesystem::exists(output+".part"),"closed GPX renamed");
    check(!writer.start(output) && writer.stop(),"existing finalized track is safe to acknowledge");
    auto recover=root+"/interrupted.gpx";std::filesystem::remove(recover);std::filesystem::remove(recover+".part");
    {GpxWriter cut;check(cut.start(recover) && cut.point(f),"prepare interrupted track");}
    {std::ofstream append(recover+".part",std::ios::app);append<<"<trkpt lat=\"31.2";}
    check(GpxWriter::recover(recover+".part"),"recover incomplete final line");
    auto alien=root+"/unrelated.part";{std::ofstream stream(alien);stream<<"user file\n";}
    check(!GpxWriter::recover(alien) && std::filesystem::exists(alien),"do not modify unrelated files");
    auto failure=root+"/absent/track.gpx";GpxWriter missing;check(!missing.start(failure) && !missing.error().empty(),"missing storage is an error");
    std::cout<<"PASS "<<checks<<" core checks\n";
}
