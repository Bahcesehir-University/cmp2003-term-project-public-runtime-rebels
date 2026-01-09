#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <array>

struct ZoneCount {
    std::string zone;
    long long count;
};

struct SlotCount {
    std::string zone;
    int hour;              //must be 0-23
    long long count;
};

class TripAnalyzer {
public:
    //reads the big csv file and parses it strictly
    void ingestFile(const std::string& csvPath);

    //returns top k zones. sorts by count (desc) then zone (asc)
    std::vector<ZoneCount> topZones(int k = 10) const;

    //returns top k busy slots. sorts by count, then zone, then hour
    std::vector<SlotCount> topBusySlots(int k = 10) const;

    private:
        struct ZoneStats {
            long long total_trips = 0;
            std::array<long long, 24> hourly_trips{};
        };
        std::unordered_map<std::string, ZoneStats> stats;
};
