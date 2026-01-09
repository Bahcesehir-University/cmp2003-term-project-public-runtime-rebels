#include "analyzer.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>
#include <cctype>
#include <queue>

using namespace std;

void TripAnalyzer::ingestFile(const string& csvPath) {
    ifstream file(csvPath);
    if (!file.is_open()) return;

    stats.clear();
    stats.reserve(600000);

    string line;
    //pre-allocate line buffer, handled by string internal growth

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        //check for header "tripID"
        if (line.rfind("TripID", 0) == 0) continue; 

        //manual comma scanning
        //fields: 0:tripid, 1:pzone, 2:dzone, 3:time, 4:dist, 5:fare
        //delimiters: c1, c2, c3, c4, c5
        //we need fields up to time (index 3), but strict validation requires checking for 6 fields (5 commas)
        
        size_t c1 = line.find(',');
         if (c1 == std::string::npos) continue;

        //validate tripid is numeric to handle dirty data inputs
        if (c1 == 0) continue; 
        bool id_ok = true;
        for (size_t i = 0; i < c1; ++i) {
            unsigned char ch = static_cast<unsigned char>(line[i]);
            if (ch < '0' || ch > '9') { 
                id_ok = false; 
                break; 
            }
        }
        if (!id_ok) continue;
        
        size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;
        size_t c3 = line.find(',', c2 + 1);
        if (c3 == std::string::npos) continue;
        size_t c4 = line.find(',', c3 + 1);
        if (c4 == std::string::npos) continue;
        size_t c5 = line.find(',', c4 + 1);
        if (c5 == std::string::npos) continue; //require at least 6 fields (5 commas)
        
        //extract pzone (between c1 and c2)
        if (c2 <= c1 + 1) continue; //empty pzone
        std::string_view pzone(line.data() + c1 + 1, c2 - (c1 + 1));
        if (pzone.empty()) continue;

        //extract time (between c3 and c4)
        if (c4 <= c3 + 1) continue; //empty time
        std::string_view ts(line.data() + c3 + 1, c4 - (c3 + 1));
        
        //timestamp validation: "2024-01-01 00:00"
        //strict length >= 16 (yyyy-mm-dd hh:mm)
        if (ts.size() < 16) continue;
        
        //check separators
        if (ts[4] != '-' || ts[7] != '-' || ts[10] != ' ' || ts[13] != ':') continue;

        //check date digits (yyyy-mm-dd)
        if (!isdigit(static_cast<unsigned char>(ts[0])) || 
            !isdigit(static_cast<unsigned char>(ts[1])) || 
            !isdigit(static_cast<unsigned char>(ts[2])) || 
            !isdigit(static_cast<unsigned char>(ts[3]))) continue;
        if (!isdigit(static_cast<unsigned char>(ts[5])) || 
            !isdigit(static_cast<unsigned char>(ts[6]))) continue;
        if (!isdigit(static_cast<unsigned char>(ts[8])) || 
            !isdigit(static_cast<unsigned char>(ts[9]))) continue;

        //extract hour (pos 11, 12)
        char h1 = ts[11];
        char h2 = ts[12];
        if (!isdigit(static_cast<unsigned char>(h1)) || !isdigit(static_cast<unsigned char>(h2))) continue;
        int hour = (h1 - '0') * 10 + (h2 - '0');
        if (hour > 23) continue;

        //extract minute (pos 14, 15)
        char m1 = ts[14];
        char m2 = ts[15];
        if (!isdigit(static_cast<unsigned char>(m1)) || !isdigit(static_cast<unsigned char>(m2))) continue;
        int minute = (m1 - '0') * 10 + (m2 - '0');
        if (minute > 59) continue;

        //update stats
        //using try_emplace with string construction from string_view (c++17 safe)
        auto [it, inserted] = stats.try_emplace(string(pzone));
        it->second.total_trips++;
        it->second.hourly_trips[hour]++;
    }
}

vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    if (k <= 0) return {};

    vector<ZoneCount> result;
    result.reserve(stats.size());
    for (const auto& pair : stats) {
        result.push_back({pair.first, pair.second.total_trips});
    }

    if (result.empty()) return result;

    //comparator: count desc, zone asc
    auto comp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    };

    size_t k_sz = min(static_cast<size_t>(k), result.size());

    if (k_sz < result.size()) {
        nth_element(result.begin(), result.begin() + k_sz, result.end(), comp);
        result.resize(k_sz);
        sort(result.begin(), result.end(), comp);
    } else {
        sort(result.begin(), result.end(), comp);
    }
    return result;
}

vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    if (k <= 0) return {};

    //comparator for min-heap (top is worst = smallest priority)
    
    auto comp_greater = [](const SlotCount& a, const SlotCount& b) {
        //returns true if a > b (a is better/higher priority than b)
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    };
    
    priority_queue<SlotCount, vector<SlotCount>, decltype(comp_greater)> pq(comp_greater);

    for (const auto& pair : stats) {
        for (int h = 0; h < 24; ++h) {
            long long cnt = pair.second.hourly_trips[h];
            if (cnt > 0) {
                SlotCount sc{pair.first, h, cnt};
                if (pq.size() < (size_t)k) {
                    pq.push(sc);
                } else {
                    //pq.top() is the worst element
                    //if sc is better than worst, replace
                    if (comp_greater(sc, pq.top())) {
                        pq.pop();
                        pq.push(sc);
                    }
                }
            }
        }
    }

    vector<SlotCount> result;
    result.reserve(pq.size());
    while(!pq.empty()) {
        result.push_back(pq.top());
        pq.pop();
    }
    
    //sort final result: count desc, zone asc, hour asc
    sort(result.begin(), result.end(), comp_greater);
    
    return result;
}
