#pragma once
#include "navigation.hpp"
#include <cstdio>
#include <string>
namespace xtrack {
class GpxWriter {
public:
    ~GpxWriter();
    bool start(const std::string& final_path);
    bool point(const Fix& fix);
    bool pause();
    bool resume();
    bool stop();
    bool active() const {return file_ && !paused_;}
    bool opened() const {return file_!=nullptr;}
    const std::string& error() const {return error_;}
    static bool recover(const std::string& part_path);
private:
    bool write(const char* text);
    FILE* file_=nullptr;
    bool paused_=false;
    std::string path_,error_;
};
}
