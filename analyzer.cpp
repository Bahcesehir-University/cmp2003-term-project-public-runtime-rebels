#include "analyzer.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <queue>

using namespace std;

void TripAnalyzer::ingestFile(const string& csvPath) {
    //optimize io operations
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    ifstream file(csvPath);
    if (!file.is_open()) return;

    stats.clear();
    stats.reserve(600000); 

    string line;
    line.reserve(128); 

    //lambda for checking digits
    auto isdig = [](char ch) { return ch >= '0' && ch <= '9'; };

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        //skip leading spaces
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        if (start == line.size()) continue;

        //check header
        if (line.compare(start, 6, "TripID") == 0) continue;

        //manual single-pass comma scanning
        size_t c1 = string::npos, c2 = string::npos, c3 = string::npos, c4 = string::npos, c5 = string::npos;
        int found = 0;
        for (size_t i = start; i < line.size(); ++i) {
            if (line[i] == ',') {
                if (found == 0) c1 = i;
                else if (found == 1) c2 = i;
                else if (found == 2) c3 = i;
                else if (found == 3) c4 = i;
                else { c5 = i; break; }
                ++found;
            }
        }

        //require at least 5 commas (6 columns)
        if (found < 5) continue; 

        //validate tripid is numeric
        if (c1 <= start) continue;
        bool id_ok = true;
        for (size_t i = start; i < c1; ++i) {
            if (!isdig(line[i])) { id_ok = false; break; }
        }
        if (!id_ok) continue;

        //extract pzone (between c1 and c2)
        if (c2 <= c1 + 1) continue; 
        size_t pL = c1 + 1, pR = c2;
        //trim spaces
        while (pL < pR && (line[pL] == ' ' || line[pL] == '\t')) ++pL;
        while (pR > pL && (line[pR - 1] == ' ' || line[pR - 1] == '\t')) --pR;
        if (pR <= pL) continue;

        string pzone = line.substr(pL, pR - pL);
        //NO TOUPPER - case sensitivity required

        //extract time (between c3 and c4)
        if (c4 <= c3 + 1) continue; 
        size_t tL = c3 + 1, tR = c4;
        //trim spaces
        while (tL < tR && (line[tL] == ' ' || line[tL] == '\t')) ++tL;
        while (tR > tL && (line[tR - 1] == ' ' || line[tR - 1] == '\t')) --tR;
        if (tR <= tL) continue;
        
        //timestamp validation
        size_t len = tR - tL;
        if (len < 15) continue; 

        //check separators
        if (line[tL+4] != '-' || line[tL+7] != '-' || line[tL+10] != ' ') continue;
        
        //check digits
        if (!isdig(line[tL]) || !isdig(line[tL+1]) || !isdig(line[tL+2]) || !isdig(line[tL+3])) continue;
        if (!isdig(line[tL+5]) || !isdig(line[tL+6])) continue;
        if (!isdig(line[tL+8]) || !isdig(line[tL+9])) continue;

        int hour = 0;
        //check colon position (handle H:MM vs HH:MM)
        if (len >= 16 && line[tL+13] == ':') {
             if (!isdig(line[tL+11]) || !isdig(line[tL+12])) continue;
             hour = (line[tL+11] - '0') * 10 + (line[tL+12] - '0');
        } else if (len >= 15 && line[tL+12] == ':') {
             if (!isdig(line[tL+11])) continue;
             hour = (line[tL+11] - '0');
        } else {
            continue;
        }

        if (hour > 23) continue;

        //update map
        stats[pzone].total_trips++;
        stats[pzone].hourly_trips[hour]++;
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

    //sort count desc zone asc
    auto comp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    };

    size_t k_sz = min(static_cast<size_t>(k), result.size());
    if (k_sz < result.size()) {
        nth_element(result.begin(), result.begin() + k_sz, result.end(), comp);
        result.resize(k_sz);
    }
    sort(result.begin(), result.end(), comp);
    return result;
}

vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    if (k <= 0) return {};

    //min heap comparator
    auto comp_greater = [](const SlotCount& a, const SlotCount& b) {
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
                } else if (comp_greater(sc, pq.top())) {
                    pq.pop();
                    pq.push(sc);
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
    
    sort(result.begin(), result.end(), comp_greater);
    return result;
}
