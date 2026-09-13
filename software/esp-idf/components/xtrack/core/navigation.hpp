#pragma once
#include <cstdint>
#include <cstddef>

namespace xtrack {
struct Fix {
    double latitude=0, longitude=0, altitude=0, speed_kmh=0, course=0, hdop=99;
    int year=0, month=0, day=0, hour=0, minute=0, second=0, satellites=0;
    bool valid=false;
    uint64_t sequence=0;
};
// A position requires matching, fresh RMC and GGA epochs, checksum and fix quality.
class NmeaParser {
public:
    bool feed(char c, uint64_t now_ms);
    Fix snapshot(uint64_t now_ms, uint32_t max_age_ms=2500) const;
    uint32_t accepted() const { return accepted_; }
    uint32_t rejected() const { return rejected_; }
private:
    bool sentence(uint64_t now);
    void commit(uint64_t now);
    char line_[160]{};
    size_t size_=0;
    Fix fix_{};
    Fix complete_{};
    uint64_t complete_ms_=0;
    double gga_lat_=0,gga_lon_=0;
    uint64_t rmc_ms_=0, gga_ms_=0;
    int rmc_epoch_=-1, gga_epoch_=-2;
    bool rmc_valid_=false, gga_valid_=false;
    uint32_t accepted_=0, rejected_=0;
};
double distance_m(double lat1, double lon1, double lat2, double lon2);
bool valid_date(int year, int month, int day);
class Trip {
public:
    double meters=0, moving_seconds=0, max_kmh=0;
    void update(const Fix& fix, uint64_t now_ms);
    void reset();
private:
    Fix previous_{};
    uint64_t previous_ms_=0;
    bool anchored_=false;
};
}
