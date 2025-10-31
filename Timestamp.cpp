#include "Timestamp.h"
#include <ostream>
#include <time.h>
#include <iostream>

Timestamp::Timestamp() : microSecondsSinceEpoch_(0) {}
Timestamp::Timestamp(int64_t microSecondsSinceEpoch) 
                    : microSecondsSinceEpoch_(microSecondsSinceEpoch) 
                    {}
Timestamp Timestamp::now() {
    time_t ti = time(NULL);
    return Timestamp(ti);
}
std::string Timestamp::toString() const {
    char buf[128] = {0};
    tm *tm_time = localtime(&microSecondsSinceEpoch_);
    snprintf(buf, 128, "%4d-%2d-%2d %2d-%2d-%2d",
            tm_time->tm_year + 1900,
            tm_time->tm_mon + 1,
            tm_time->tm_mday,
            tm_time->tm_hour,
            tm_time->tm_min,
            tm_time->tm_sec);
    return buf;
}

int main() {
    Timestamp tms = Timestamp::now();
    std::cout << tms.toString() << std::endl;
    return 0;
}
