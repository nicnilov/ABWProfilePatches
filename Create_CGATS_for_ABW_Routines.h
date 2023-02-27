/*
Copyright (c) <2020> <doug gray>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <array>
#include <string>
#include <vector>
#include <map>
#include "color_conversions.h"
#include "PatchFilter.h"
#include "cgats.h"

using std::vector;
using std::string;

using V3 = array<double, 3>;  // typically used to hold RGB, LAB or XYZ values

struct LabStats {
    // synthesized RGBLAB values for use in profiling application
    vector<V6> rgblab_neutral;
    vector<V6> rgblab_tint;
    
    // All vectors the same size (percents.size()) containing fraction
    // of samples > corresponding percents
    array<double,6> percents = { 50, 75, 90, 95, 98, 100 };
    vector<double> distributionp_ab0_15;
    vector<double> distributionp_ab0_5;
    vector<double> distributionp_15;
    vector<double> distributionp_5;
    // These used only if repeats > 1 since they are individual sample stats
    vector<double> distributionp_std_L;
    vector<double> distributionp_std_a;
    vector<double> distributionp_std_b;

    V3 white_point;
    V3 black_point;
    V3 lab_average;
    int repeats;

    vector<V3> lab;
    vector<double> rgb;
    PatchFilter patch_filter;
};

LabStats process_cgats_measurement_file(const string& filename);
void make_RGB_for_ABW(const string& filename, int count, int randomize_and_repeat=0);

vector<V6> make_rgb_synth(PatchFilter& pf, bool color = false);
void replace_icc1_A2B1_with_icc2_A2B1(string iccpath1, string iccpath2);
string replace_suffix(string name, string suffix, string replacement);
bool is_suffix_icm(string fname);
bool is_suffix_txt(string fname);
string remove_suffix(string fname);
void print_stats(const LabStats& stats, bool extended);
void print_argyll_batch_command_file(const char* batch_file_name, const char* pc);

std::string to_lower(const std::string& arg);
bool is_suffix_icm(std::string fname);
bool is_suffix_txt(std::string fname);
std::string remove_suffix(std::string fname);

class MapRGB {
public:
    struct RGBLabAndLoc {
        V3 lab;
        int loc;
    };
    std::map<V3, vector<RGBLabAndLoc>> rgb_lab_loc;
    void print_stats()
    {
        printf("%i unique patches\n         R   G   B        L*     a*     b*      Diff from ave   Patch#\n",
            int(rgb_lab_loc.size()));
        for (const auto& x : rgb_lab_loc)
        {
            Statistics stat[3];
            for (size_t iv = 0; iv < x.second.size(); iv++)
                for (int i = 0; i < 3; i++)
                    stat[i].clk(x.second[iv].lab[i]);
            printf("Patch: %3.0f %3.0f %3.0f   %6.1f %6.1f %6.1f\n",
                x.first[0], x.first[1], x.first[2],
                stat[0].ave(), stat[1].ave(), stat[2].ave());
            if (x.second.size() > 1)
                for (const auto& xx : x.second)
                    printf("                       %6.1f %6.1f %6.1f    %4.1f %4.1f %4.1f     %i\n",
                        xx.lab[0], xx.lab[1], xx.lab[2],
                        xx.lab[0] - stat[0].ave(), xx.lab[1] - stat[1].ave(), xx.lab[2] - stat[2].ave(),
                        xx.loc);
        }
    }
    MapRGB(const vector<V6>& rgblab)
    {
        auto rgb_lab = cgats_utilities::separate_rgb_lab(rgblab);
        for (size_t i = 0; i < rgblab.size(); i++)
            if (!rgb_lab_loc.contains(rgb_lab.first[i]))
                rgb_lab_loc[rgb_lab.first[i]] = { vector<RGBLabAndLoc>() };
        for (size_t i = 0; i < rgblab.size(); i++)
        {
            RGBLabAndLoc x{ rgb_lab.second[i], static_cast<int>(i + 1) };
            rgb_lab_loc[rgb_lab.first[i]].push_back(x);
        }
    }
};

