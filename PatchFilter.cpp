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


#include <numeric>
#include <stdexcept>
#include <algorithm>
#include "PatchFilter.h"
#include "color_conversions.h"
using namespace color_conversions;

static vector<double> convolve(vector<double> v1, vector<double> v2)
{
    vector<double> ret(v1.size() + v2.size() - 1);
    for (int i = 0; i < int(v1.size()); i++)
        for (int ii = 0; ii < int(v2.size()); ii++)
            ret[i + ii] += v1[i] * v2[ii];
    return ret;
}

// create low pass filter of size n=1,3,5,..  with optional, center value near zero
// to remove center from filter to measure tracking noise/lumpiness
static vector<double> make_lowpass(size_t n, bool xcenter)
{
    vector<double> ret{ 1 };
    vector<double> f1{ 1, 1 };
    for (size_t i = 1; i < n; i++)
        ret = convolve(ret, f1);
    if (xcenter)
        ret[n / 2] = .001;
    auto s = std::accumulate(ret.begin(), ret.end(), 0.0);
    std::transform(ret.begin(), ret.end(), ret.begin(), [s](double x) {return x / s; });    // normalize filter
    return ret;
}

// Low pass filter to reduce anomalous variations. Used to filter Lab values
static vector<V3> smooth(const vector<V3>& v3, int n, bool xcenter)
{
    vector<double> ret1(v3.size());
    vector<V3> ret(v3.size());
    auto f = make_lowpass(n, xcenter);
    for (size_t ii = 0; ii < 3; ii++)
    {
        vector<double> v;
        for (auto x : v3)
            v.push_back(x[ii]);
        for (size_t i = 0; i < v.size() - n + 1; i++)
            ret[i + n / 2][ii] = std::inner_product(f.begin(), f.begin() + n, v.begin() + i, 0.0);

        for (size_t i  = 1; i < n / 2u + 1; i++) // use decreasing low pass filters to average near end points
        {
            auto f = make_lowpass(i * 2 - 1, xcenter);
            ret[i - 1][ii] = std::inner_product(f.begin(), f.begin() + f.size(), v.begin(), 0.0);
            ret[ret1.size() - i][ii] = std::inner_product(f.begin(), f.begin() + f.size(), v.end() - f.size(), 0.0);
        }
    }
    return ret;
}




// Populate averages of Lab values for step sizes of either 1 or 5
// lab: average Lab of all same RGB samples
// labf: Smoothed (low pass filter) of lab;
// labx: Smoothed (low pass filter) of lab but excluding sample
PatchFilter::PatchFilter(const vector<V6>& vin) :
    ND{ 255 / (int)(vin.size() - 1) }
{
    // create L* table for sRGB in steps of 5
    L_sRGB.resize(52);
    sRGB_xyz.resize(52);
    L_projected.resize(52);

    for (int i = 0; i < 52; i++)
    {
        V3 rgb = sRGB_to_Lab(V3{ i * 5.,i * 5.,i * 5. });
        L_sRGB[i] = rgb[0];
        sRGB_xyz[i] = Lab_to_XYZ(V3{ L_sRGB[i],0,0 });
    }

    for (auto const& x : vin)
        lab.push_back(*((V3*)&x+1));
    labf = smooth(lab, ND > 1 ? 3 : 9, false);
    labfx = smooth(lab, ND > 1 ? 3 : 9, true);
    if (labf.size() == 52)
        lab5 = labf;
    else
        for (int i = 0; i < 256; i += 5)
            lab5.push_back(labf[i]);

    const vector<V3> xyz5 = Lab_to_XYZ(lab5);
    const V3 white = xyz5[51];
    const V3 black = xyz5[0];

    // Check for BPC which extends black a white to L0:100
    // and produces small, increasing, changes in L* in the early RGB segments
    double L = lab5[0][0];
    if (L < 5 && lab5[1][0] - lab5[0][0] > .3 || 
        L >= 5 && L < 10 && lab5[2][0] - lab5[0][0] > .3 ||
        L >= 10 && lab5[3][0] - lab5[0][0] > .3)
    {
        // scale Y 0 to 1
        intent = Intent::RELBPC;
        for (int i = 0; i < 52; i++)
        {
            V3 xyz = black + (white[1] - black[1]) *sRGB_xyz[i];
            V3 lab = XYZ_to_Lab(xyz);
            L_projected[i] = lab[0];
        }
    }
    // Rel. Col. mode extends only the paper white point to L:100
    // If the paper's white point is > 98.5 an Abs. Col. print may be
    // deemed Rel. Col.
    else if (lab5[51][0] - lab5[50][0] > .3)    // REL
    {
        intent = Intent::REL;
        for (int i = 0; i < 52; i++)
        {
            V3 xyz;
            if (white[1]*sRGB_xyz[i][1] < black[1])
                xyz = black;
            else
                xyz = white[1] * sRGB_xyz[i];
            V3 lab = XYZ_to_Lab(xyz);
            L_projected[i] = lab[0];
        }
    }
    // Abs. Col. is actual L* with plateaus below black ink and above paper white
    else   // ABS
    {
        intent = Intent::ABS;
        for (int i = 0; i < 52; i++)
        {
            V3 xyz;
            if (sRGB_xyz[i][1] < black[1])
                xyz = black;
            else if (sRGB_xyz[i][1] > white[1])
                xyz = white;
            else
                xyz = sRGB_xyz[i];
            V3 lab = XYZ_to_Lab(xyz);
            L_projected[i] = lab[0];
        }
    }
}

// Operates on potentially averaged values. Returns dE00 of sample vs. smoothed excluding sample
// Useful for evaluating printer smoothness
vector<double> PatchFilter::get_dE00_vals()
{
    vector<double> ret;
    for (int i = 0; i < int(labf.size()); i++)
        ret.push_back(deltaE2000(lab[i], labfx[i]));
    return ret;
}

// Returns dE00 of Lab for sample v ave(sample-spread, sample+spread)  w option to zero a* and b*
vector<double> PatchFilter::get_dE00_split(int spread, bool zero_ab)  // spread must be 5 or 15
{
    auto labfq = labf;
    if (zero_ab)
        for (auto &x : labfq)
            x[2] = x[1] = 0;
    if (lab.size() != 52 && lab.size() != 256)
        throw std::invalid_argument("B&W Must be either 52 or 256 evenly spaced RGB values from 0:255");
    vector<double> ret;
    if (lab.size() == 52)
        spread /= 5;
    for (int i = spread; i < int(lab.size()) - spread; i++)
    {
        double de = deltaE2000((labfq[i - spread] + labfq[i + spread])*.5, labfq[i]);
        ret.push_back(de);
    }
    return ret;
}

// Get rgblab with spacing of 5 rgb units, Optionally clear a* and b*
vector<V6> PatchFilter::get_rgblab5(bool zero_ab)
{
    vector<V6> ret;
    for (int i = 0; i < int(labf.size()); i++)
    {
        double i_d = i*ND; // make all values in V6 doubles
        if (zero_ab)
            ret.push_back(V6{ i_d, i_d, i_d, labf[i][0], 0., 0. });
        else
            ret.push_back(V6{ i_d, i_d, i_d, labf[i][0], labf[i][1], labf[i][2] });
    }
    return ret;
}


vector<int> histogram(vector<double> v, double step, double last)
{
    vector<int> counts;
    int i = 0;
    for (; i * step < last; i++)
    {
        auto x = std::accumulate(v.begin(), v.end(), 0, [i, step](const auto& arg1, const auto& arg2)
            {return arg2 >= i * step && arg2 < (i + 1) * step ? arg1 + 1 : arg1; });
        counts.push_back(x);
    }
    auto x = std::accumulate(v.begin(), v.end(), 0, [i, step](const auto& arg1, const auto& arg2)
        {return arg2 >= i * step ? arg1 + 1 : arg1; });
    counts.push_back(x);
    for (size_t i = counts.size() - 1; counts[i] == 0; i--)
        counts.erase(counts.end() - 1);
    return counts;
}

vector<double> distribution(vector<int>v, bool accumulate)
{
    int count = std::accumulate(v.begin(), v.end(), 0);
    if (accumulate)
        std::partial_sum(v.begin(), v.end(), v.begin());
    vector<double> ret(v.size());
    std::transform(v.begin(), v.end(), ret.begin(), [count](auto x) {return 1.0 * x / count; });
    return ret;
}


// Interpolate to specific neutral RGB element and return Lab value
// v is RGBLAB vector of (3 for RGB, 3 for Lab)
V3 find_lab_interpolation(const vector<V6>& v, int x)
{
    V3 ret;
    if (x == 0)
        ret= *((V3*)&v[0] + 1);     // hack to get Lab component of V6
    else
    {
        auto low = std::find_if(v.begin(), v.end(), [x](V6 arg) {return arg[0] >= x; }) - 1;
        auto deltaLab = *((V3*)&low[1]+1) - *((V3*)&low[0]+1);
        auto deltaR = low[1][0] - low[0][0];        // Difference in RGB
        auto deltaLAdj = deltaLab * ((x - low[0][0])/deltaR);// Adjustment
        ret = *((V3*)&low[0] + 1) + deltaLAdj;
    }
    return ret;
}

