#include "ShuntAnalysis.h"
#include "TubeCouplingCircuit.h"
#include "TubeCurveData.h"
#include <dsp/VtbTransformerCore.h>
#include <dsp/GainTube.h>
#include <dsp/AmpTube.h>
#include <PluginProcessor.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <complex>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <vector>

// Tube stage (docs/TubeStage.md).
// Step 1, task tube-fit: fit the Koren triode equation to the digitized plate
// families of the 12BH7-A and 5751.
// Step 2, task tube-coupling: the hybrid plan's tube-to-transformer coupling
// test, a 12BH7 cathode follower into the VTB1148 C calibration.
namespace
{
using namespace ModuleSpike;
using TubeCurves::Point;
using TubeCurves::Typical;

std::ofstream csv (const juce::File& root, const char* file, const char* header)
{
    std::ofstream result (root.getChildFile (file).getFullPathName().toStdString());
    result << std::setprecision (10) << header << '\n'; return result;
}
std::vector<double> nelderMead (const std::function<double (const std::vector<double>&)>& f, std::vector<double> start, const std::vector<double>& step, int evaluations)
{
    const auto n = start.size();
    std::vector<std::vector<double>> simplex { start };
    for (std::size_t i = 0; i < n; ++i) { auto x = start; x[i] += step[i]; simplex.push_back (x); }
    std::vector<double> values;
    for (const auto& x : simplex) values.push_back (f (x));
    for (int used = static_cast<int> (n + 1); used < evaluations;)
    {
        std::vector<std::size_t> order (n + 1);
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (auto a, auto b) { return values[a] < values[b]; });
        const auto best = order.front(), worst = order.back(), second = order[n - 1];
        std::vector<double> centroid (n, 0);
        for (auto i : order) if (i != worst) for (std::size_t k = 0; k < n; ++k) centroid[k] += simplex[i][k] / static_cast<double> (n);
        const auto along = [&] (double t) { std::vector<double> x (n); for (std::size_t k = 0; k < n; ++k) x[k] = centroid[k] + t * (simplex[worst][k] - centroid[k]); return x; };
        auto reflected = along (-1); auto fr = f (reflected); ++used;
        if (fr < values[best])
        {
            auto expanded = along (-2); auto fe = f (expanded); ++used;
            if (fe < fr) { simplex[worst] = expanded; values[worst] = fe; } else { simplex[worst] = reflected; values[worst] = fr; }
        }
        else if (fr < values[second]) { simplex[worst] = reflected; values[worst] = fr; }
        else
        {
            auto contracted = along (fr < values[worst] ? -0.5 : 0.5); auto fc = f (contracted); ++used;
            if (fc < std::min (fr, values[worst])) { simplex[worst] = contracted; values[worst] = fc; }
            else
                for (auto i : order)
                    if (i != best)
                    {
                        for (std::size_t k = 0; k < n; ++k) simplex[i][k] = simplex[best][k] + 0.5 * (simplex[i][k] - simplex[best][k]);
                        values[i] = f (simplex[i]); ++used;
                    }
        }
    }
    return simplex[static_cast<std::size_t> (std::min_element (values.begin(), values.end()) - values.begin())];
}

// The curves were read along current rows, so reading error is horizontal:
// compare plate voltages at the digitized current.
double plateVoltageFor (const TriodeParameters& p, double vg, double ia)
{
    double lo = 0, hi = 2000;
    if (triodeCurrent (p, hi, vg).ia < ia) return hi;
    for (int i = 0; i < 80; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (triodeCurrent (p, mid, vg).ia < ia ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}
double voltageRms (const TriodeParameters& p, std::span<const Point> points)
{
    double sum = 0;
    for (const auto& point : points) sum += std::pow (plateVoltageFor (p, point.vg, point.ma * 0.001) - point.va, 2);
    return std::sqrt (sum / static_cast<double> (points.size()));
}
TriodeParameters fromVector (const std::vector<double>& x) { return { std::exp (x[0]), std::exp (x[1]), std::exp (x[2]), std::exp (x[3]), std::exp (x[4]) }; }
struct Small { double mu, gm, rp; };
Small smallSignal (const TriodeParameters& p, double va, double vg)
{
    const auto c = triodeCurrent (p, va, vg);
    return { c.dVg / c.dVa, c.dVg, 1 / c.dVa };
}
TriodeParameters fitTube (const char* tube, std::span<const Point> points, const Typical& anchor, std::ofstream& search)
{
    const auto cost = [&] (const std::vector<double>& x)
    {
        const auto value = voltageRms (fromVector (x), points);
        search << tube << ',' << std::exp (x[0]) << ',' << std::exp (x[1]) << ',' << std::exp (x[2]) << ',' << std::exp (x[3]) << ',' << std::exp (x[4]) << ',' << value << '\n';
        return value;
    };
    // Grid over the shape terms; kg1 is set so the published typical current is met.
    std::vector<std::pair<double, std::vector<double>>> grid;
    for (double kp : { 40.0, 100.0, 300.0, 700.0 })
        for (double ex : { 1.2, 1.35, 1.5 })
            for (double kvb : { 30.0, 300.0, 3000.0 })
            {
                TriodeParameters p { anchor.mu, ex, 1, kp, kvb };
                p.kg1 = triodeCurrent (p, anchor.va, anchor.vg).ia / (anchor.ma * 0.001);
                std::vector<double> x { std::log (p.mu), std::log (ex), std::log (p.kg1), std::log (kp), std::log (kvb) };
                grid.emplace_back (cost (x), x);
            }
    std::sort (grid.begin(), grid.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<double> best;
    double bestCost = 1.0e30;
    for (std::size_t start = 0; start < 3; ++start)
    {
        auto x = grid[start].second;
        for (int restart = 0; restart < 3; ++restart)
            x = nelderMead (cost, x, { 0.1, 0.1, 0.5, 0.5, 1.0 }, 1500);
        const auto value = cost (x);
        std::cout << "tube fit " << tube << " start " << start << " rms " << value << " V" << std::endl;
        if (value < bestCost) { bestCost = value; best = x; }
    }
    search.flush();
    return fromVector (best);
}
void writeFit (const juce::File& root)
{
    auto search = csv (root, "tube_fit_search.csv", "tube,mu,ex,kg1,kp,kvb,rms_volts");
    auto parameters = csv (root, "tube_fit.csv", "tube,source,mu,ex,kg1,kp,kvb,points,rms_volts,max_volts,rms_current_percent_above_2pct_full_scale");
    auto pointsOut = csv (root, "tube_fit_points.csv", "tube,grid_volts,digitized_plate_volts,plate_ma,model_plate_volts,model_ma,volt_error");
    auto typical = csv (root, "tube_fit_typical.csv", "tube,source,plate_volts,grid_volts,published_ma,model_ma,published_mu,model_mu,published_gm_ms,model_gm_ms,published_rp_kohm,model_rp_kohm");
    struct Tube { const char* id; const char* source; std::span<const Point> points; std::span<const Typical> typicals; double fullScaleMa; };
    const std::array tubes {
        Tube { "12BH7-A", "RCA tentative data Mar 1955 PDF p4", TubeCurves::rca12bh7a, TubeCurves::rca12bh7aTypical, 50 },
        Tube { "5751", "GE ETX-245A Jun 1952 p2", TubeCurves::ge5751, TubeCurves::ge5751Typical, 6 }
    };
    for (const auto& tube : tubes)
    {
        const auto p = fitTube (tube.id, tube.points, tube.typicals.back().mu > 0 ? tube.typicals.back() : tube.typicals.front(), search);
        double sum = 0, worst = 0, currentSum = 0;
        int currentCount = 0;
        for (const auto& point : tube.points)
        {
            const auto va = plateVoltageFor (p, point.vg, point.ma * 0.001);
            const auto ma = triodeCurrent (p, point.va, point.vg).ia * 1000;
            sum += std::pow (va - point.va, 2); worst = std::max (worst, std::abs (va - point.va));
            if (point.ma > 0.02 * tube.fullScaleMa) { currentSum += std::pow ((ma - point.ma) / point.ma, 2); ++currentCount; }
            pointsOut << tube.id << ',' << point.vg << ',' << point.va << ',' << point.ma << ',' << va << ',' << ma << ',' << va - point.va << '\n';
        }
        const auto rms = std::sqrt (sum / static_cast<double> (tube.points.size()));
        parameters << tube.id << ',' << tube.source << ',' << p.mu << ',' << p.ex << ',' << p.kg1 << ',' << p.kp << ',' << p.kvb << ','
                   << tube.points.size() << ',' << rms << ',' << worst << ',' << 100 * std::sqrt (currentSum / currentCount) << '\n';
        for (const auto& t : tube.typicals)
        {
            const auto s = smallSignal (p, t.va, t.vg);
            typical << tube.id << ',' << t.source << ',' << t.va << ',' << t.vg << ',' << t.ma << ',' << triodeCurrent (p, t.va, t.vg).ia * 1000 << ','
                    << t.mu << ',' << s.mu << ',' << t.gmMs << ',' << s.gm * 1000 << ',' << t.rpKohm << ',' << s.rp / 1000 << '\n';
        }
        std::cout << std::setprecision (10) << tube.id << " { " << p.mu << ", " << p.ex << ", " << p.kg1 << ", " << p.kp << ", " << p.kvb << " } rms " << rms << " V" << std::endl;
    }
}
// ---- Gain-control tubes (docs/GainTube.md): task gain-tube-fit ----
// Transfer points were read at fixed grid voltage, so their error is in current:
// relative, floored at 0.25 mA so tail readings do not dominate.
double transferError (const std::function<double (double, double)>& current, std::span<const Point> points)
{
    double sum = 0;
    for (const auto& p : points)
        sum += std::pow ((current (p.va, p.vg) * 1000 - p.ma) / (p.ma + 0.25), 2);
    return std::sqrt (sum / static_cast<double> (points.size()));
}
VariableMuParameters variableMuFrom (const std::vector<double>& x, int count)
{
    VariableMuParameters p;
    p.count = count;
    for (int i = 0; i < count; ++i)
        p.sections[static_cast<std::size_t> (i)] = { std::exp (x[static_cast<std::size_t> (2 * i)]), std::exp (x[static_cast<std::size_t> (2 * count)]),
            std::exp (x[static_cast<std::size_t> (2 * i + 1)]), std::exp (x[static_cast<std::size_t> (2 * count + 1)]), std::exp (x[static_cast<std::size_t> (2 * count + 2)]) };
    return p;
}
// Sections share ex, kp and kvb (one cathode and plate); each has its own mu and kg1.
// Fitted to the transfer family plus the sheet's anchors: Ia and gm at the class A1
// point, and gm = 100 uS at about -16 V (100 V plate), the remote-cutoff tail.
double variableMuCost (const VariableMuParameters& p)
{
    // Keep every section a plausible triode (mu between 2 and 60); unbounded fits
    // drift to a section with no plate-voltage dependence at all.
    for (int i = 0; i < p.count; ++i)
        if (p.sections[static_cast<std::size_t> (i)].mu < 2 || p.sections[static_cast<std::size_t> (i)].mu > 60) return 1.0e6;
    const auto current = [&p] (double va, double vg) { return variableMuCurrent (p, va, vg).ia; };
    const auto curves = transferError (current, TubeCurves::ge6386Transfer);
    const auto& t = TubeCurves::ge6386Typical[0];
    const auto atTypical = variableMuCurrent (p, t.va, t.vg);
    const auto tail = variableMuCurrent (p, TubeCurves::ge6386PlateFor100uS, TubeCurves::ge6386GridFor100uS);
    const auto anchors = 3 * std::pow (atTypical.ia * 1000 / t.ma - 1, 2) + 3 * std::pow (std::log (atTypical.dVg * 1000 / t.gmMs), 2)
        + 3 * std::pow (std::log (std::max (tail.dVg, 1.0e-9) / 100.0e-6), 2);
    return std::sqrt (curves * curves + anchors);
}
VariableMuParameters fitVariableMu (int count, std::ofstream& search)
{
    const auto cost = [count, &search] (const std::vector<double>& x)
    {
        const auto value = variableMuCost (variableMuFrom (x, count));
        search << "6386," << count;
        for (auto v : x) search << ',' << std::exp (v);
        search << ',' << value << '\n';
        return value;
    };
    // Starts: sections spread from the high-mu (sharp) to the low-mu (remote) end.
    std::vector<std::pair<double, std::vector<double>>> starts;
    for (double spread : { 2.0, 4.0, 8.0 })
        for (double kp : { 50.0, 200.0, 600.0 })
            for (double ex : { 1.2, 1.4 })
            {
                std::vector<double> x;
                for (int i = 0; i < count; ++i)
                {
                    const auto mu = 20.0 / std::pow (spread, static_cast<double> (i) / std::max (1, count - 1));
                    x.push_back (std::log (mu));
                    x.push_back (std::log (1000.0 * (1 + i)));
                }
                x.push_back (std::log (ex)); x.push_back (std::log (kp)); x.push_back (std::log (300.0));
                starts.emplace_back (cost (x), x);
            }
    std::sort (starts.begin(), starts.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<double> best;
    double bestCost = 1.0e30;
    for (std::size_t start = 0; start < 4; ++start)
    {
        auto x = starts[start].second;
        for (int restart = 0; restart < 4; ++restart)
            x = nelderMead (cost, x, std::vector<double> (x.size(), 0.3), 3000);
        const auto value = cost (x);
        std::cout << "6386 sections " << count << " start " << start << " cost " << value << std::endl;
        if (value < bestCost) { bestCost = value; best = x; }
    }
    return variableMuFrom (best, count);
}
void writeGainTubeFit (const juce::File& root)
{
    auto search = csv (root, "gain_tube_fit_search.csv", "tube,sections,parameters...,cost");
    auto summary = csv (root, "gain_tube_fit.csv", "tube,model,sections,section,mu,ex,kg1,kp,kvb,transfer_rms_relative,plate_rms_volts,cost");
    auto anchors = csv (root, "gain_tube_anchors.csv", "tube,model,point,plate_volts,grid_volts,published,model");
    auto transfer = csv (root, "gain_tube_transfer.csv", "tube,model,plate_volts,grid_volts,published_ma,model_ma");
    // 5670: an ordinary medium-mu triode; Koren fitted to its plate family as before.
    const auto p5670 = fitTube ("5670", TubeCurves::ge5670, TubeCurves::ge5670Typical[0], search);
    const auto koren = [&p5670] (double va, double vg) { return triodeCurrent (p5670, va, vg).ia; };
    summary << "5670,koren,1,1," << p5670.mu << ',' << p5670.ex << ',' << p5670.kg1 << ',' << p5670.kp << ',' << p5670.kvb << ','
            << transferError (koren, TubeCurves::ge5670Transfer) << ',' << voltageRms (p5670, TubeCurves::ge5670) << ",\n";
    for (const auto& p : TubeCurves::ge5670Transfer)
        transfer << "5670,koren," << p.va << ',' << p.vg << ',' << p.ma << ',' << koren (p.va, p.vg) * 1000 << '\n';
    {
        const auto& t = TubeCurves::ge5670Typical[0];
        const auto c = triodeCurrent (p5670, t.va, t.vg);
        anchors << "5670,koren,Ia mA," << t.va << ',' << t.vg << ',' << t.ma << ',' << c.ia * 1000 << '\n'
                << "5670,koren,gm mS," << t.va << ',' << t.vg << ',' << t.gmMs << ',' << c.dVg * 1000 << '\n'
                << "5670,koren,mu," << t.va << ',' << t.vg << ',' << t.mu << ',' << c.dVg / c.dVa << '\n';
    }
    // 6386: sums of one to three sections; the best is recorded.
    for (int count : { 1, 2, 3 })
    {
        const auto p = fitVariableMu (count, search);
        const auto current = [&p] (double va, double vg) { return variableMuCurrent (p, va, vg).ia; };
        const auto model = "sections-" + std::to_string (count);
        for (int i = 0; i < count; ++i)
        {
            const auto& s = p.sections[static_cast<std::size_t> (i)];
            summary << "6386," << model << ',' << count << ',' << i + 1 << ',' << s.mu << ',' << s.ex << ',' << s.kg1 << ',' << s.kp << ',' << s.kvb << ','
                    << transferError (current, TubeCurves::ge6386Transfer) << ",," << variableMuCost (p) << '\n';
        }
        for (const auto& pt : TubeCurves::ge6386Transfer)
            transfer << "6386," << model << ',' << pt.va << ',' << pt.vg << ',' << pt.ma << ',' << current (pt.va, pt.vg) * 1000 << '\n';
        const auto& t = TubeCurves::ge6386Typical[0];
        const auto c = variableMuCurrent (p, t.va, t.vg);
        const auto tail = variableMuCurrent (p, TubeCurves::ge6386PlateFor100uS, TubeCurves::ge6386GridFor100uS);
        anchors << "6386," << model << ",Ia mA," << t.va << ',' << t.vg << ',' << t.ma << ',' << c.ia * 1000 << '\n'
                << "6386," << model << ",gm mS," << t.va << ',' << t.vg << ',' << t.gmMs << ',' << c.dVg * 1000 << '\n'
                << "6386," << model << ",mu," << t.va << ',' << t.vg << ',' << t.mu << ',' << c.dVg / c.dVa << '\n'
                << "6386," << model << ",gm uS," << TubeCurves::ge6386PlateFor100uS << ',' << TubeCurves::ge6386GridFor100uS << ",100," << tail.dVg * 1.0e6 << '\n';
        std::cout << "6386 " << model << " transfer rms " << transferError (current, TubeCurves::ge6386Transfer) << " typical " << c.ia * 1000 << " mA gm " << c.dVg * 1000 << " tail gm " << tail.dVg * 1.0e6 << " uS" << std::endl;
        std::cout << std::setprecision (10);
        for (int i = 0; i < count; ++i) { const auto& s = p.sections[static_cast<std::size_t> (i)]; std::cout << "  { " << s.mu << ", " << s.ex << ", " << s.kg1 << ", " << s.kp << ", " << s.kvb << " }" << std::endl; }
    }
    std::cout << "5670 { " << p5670.mu << ", " << p5670.ex << ", " << p5670.kg1 << ", " << p5670.kp << ", " << p5670.kvb << " } transfer rms " << transferError (koren, TubeCurves::ge5670Transfer) << std::endl;
}

// ---- Step 2: coupling test ----
using namespace TubeCoupling;
constexpr double couplingRate = 384000; // 8x 48 kHz; every path runs here, so differences are coupling only.
constexpr int perBlockSamples = 2048;   // 256 host samples at 48 kHz.
constexpr double analysisSeconds = 0.2; // Whole periods of every test frequency.
double dbuToPeakVolts (double dbu) { return std::sqrt (2.0) * 0.7745966692 * std::pow (10.0, dbu / 20); }
double voltsToDbu (double rms) { return 20 * std::log10 (std::max (rms, 1.0e-12) / 0.7745966692); }
struct ToneResult
{
    std::vector<double> output;
    std::array<std::complex<double>, 21> harmonics {};
    StageDiagnostics stage;
    std::uint64_t transformerFailures = 0;
    double minimumOhms = 0, maximumOhms = 0, quiescentOhms = 0;
};
ToneResult runTone (Path path, double hz, double gridDbu)
{
    CouplingPath chain;
    chain.prepare (path, couplingRate, perBlockSamples, StageDesign {}, vtb1148OutputC());
    const auto settle = hz <= 100 ? 1.0 : 0.3, fade = 0.1;
    const auto total = static_cast<int> ((settle + analysisSeconds) * couplingRate), start = static_cast<int> (settle * couplingRate);
    const auto amplitude = dbuToPeakVolts (gridDbu);
    ToneResult r;
    r.output.reserve (static_cast<std::size_t> (total - start));
    for (int n = 0; n < total; ++n)
    {
        const auto t = n / couplingRate;
        const auto envelope = t < fade ? 0.5 * (1 - std::cos (ShuntResearch::pi * t / fade)) : 1.0;
        const auto y = chain.process (envelope * amplitude * std::sin (2 * ShuntResearch::pi * hz * t));
        if (n >= start) r.output.push_back (y);
    }
    const auto count = static_cast<double> (r.output.size());
    for (std::size_t h = 1; h < r.harmonics.size(); ++h)
    {
        std::complex<double> sum {};
        for (std::size_t n = 0; n < r.output.size(); ++n)
            sum += r.output[n] * std::polar (1.0, -2 * ShuntResearch::pi * hz * static_cast<double> (h) * static_cast<double> (n + static_cast<std::size_t> (start)) / couplingRate);
        r.harmonics[h] = 2.0 * sum / count; // Peak amplitude and phase.
    }
    r.stage = chain.stageDiagnostics(); r.transformerFailures = chain.transformerFailures();
    r.minimumOhms = chain.minimumOhms; r.maximumOhms = chain.maximumOhms; r.quiescentOhms = chain.quiescentOhms;
    return r;
}
double thdPercent (const ToneResult& r)
{
    double sum = 0;
    for (std::size_t h = 2; h < r.harmonics.size(); ++h) sum += std::norm (r.harmonics[h]);
    return 100 * std::sqrt (sum) / std::abs (r.harmonics[1]);
}
struct Difference { double unmatchedDb, matchedDb, harmonicErrorDbc; };
Difference compare (const ToneResult& candidate, const ToneResult& reference)
{
    double error = 0, power = 0, cross = 0, self = 0;
    for (std::size_t n = 0; n < reference.output.size(); ++n)
    {
        error += std::pow (candidate.output[n] - reference.output[n], 2); power += std::pow (reference.output[n], 2);
        cross += candidate.output[n] * reference.output[n]; self += candidate.output[n] * candidate.output[n];
    }
    const auto gain = cross / std::max (self, 1.0e-30);
    double matched = 0;
    for (std::size_t n = 0; n < reference.output.size(); ++n) matched += std::pow (gain * candidate.output[n] - reference.output[n], 2);
    // Distortion error as a complex difference: level and phase of every harmonic.
    double harmonic = 0;
    for (std::size_t h = 2; h < reference.harmonics.size(); ++h) harmonic += std::norm (candidate.harmonics[h] - reference.harmonics[h]);
    return { ShuntResearch::powerDb (error / power), ShuntResearch::powerDb (matched / power),
        ShuntResearch::powerDb (harmonic / std::norm (reference.harmonics[1])) };
}
void writeCoupling (const juce::File& root)
{
    const auto design = StageDesign {};
    {
        auto operating = csv (root, "coupling_operating_point.csv", "tube,sections,supply_volts,grid_bias_volts,cathode_ohms,coupling_uf,quiescent_cathode_volts,plate_volts,grid_cathode_volts,plate_ma_total,dissipation_watts_per_section,output_ohms,primary_inductance_h,winding_ohms,load_ohms,lf_corner_hz,transformer_unit_volts");
        const auto vk = CathodeFollower<InductiveLoad>::quiescentCathode (design);
        const auto c = triodeCurrent (design.tube, design.supply - vk, design.bias - vk);
        CathodeFollower<InductiveLoad> stage;
        stage.prepare (couplingRate, design, vtb1148OutputC());
        const auto p = vtb1148OutputC();
        const auto ro = stage.outputOhms (0), winding = p.loss * TransformerCircuit::impedanceBase, l = linearInductance (p);
        const auto source = ro + winding;
        operating << "12BH7-A," << design.sections << ',' << design.supply << ',' << design.bias << ',' << design.cathodeOhms << ',' << design.couplingFarads * 1.0e6 << ','
                  << vk << ',' << design.supply - vk << ',' << design.bias - vk << ',' << design.sections * c.ia * 1000 << ',' << (design.supply - vk) * c.ia << ','
                  << ro << ',' << l << ',' << winding << ',' << p.loadOhms << ',' << source * p.loadOhms / (source + p.loadOhms) / (2 * ShuntResearch::pi * l) << ',' << transformerUnitVolts (p) << '\n';
    }
    auto sweep = csv (root, "coupling_sweep.csv", "path,frequency_hz,grid_dbu,output_dbu,thd_percent,h2_dbc,h3_dbc,h4_dbc,h5_dbc,h6_dbc,h7_dbc,unmatched_difference_db,matched_difference_db,harmonic_error_dbc,plate_ma_min,plate_ma_max,source_ohms_min,source_ohms_max,stage_iterations_max,stage_failures,transformer_failures");
    for (double hz : { 20.0, 50.0, 100.0, 1000.0 })
        for (double dbu = -10; dbu <= 30.5; dbu += 2)
        {
            std::array<std::future<ToneResult>, 4> jobs;
            for (std::size_t i = 0; i < jobs.size(); ++i) jobs[i] = std::async (std::launch::async, runTone, static_cast<Path> (i), hz, dbu);
            std::array<ToneResult, 4> results;
            for (std::size_t i = 0; i < jobs.size(); ++i) results[i] = jobs[i].get();
            for (std::size_t i = 0; i < results.size(); ++i)
            {
                const auto& r = results[i];
                const auto d = compare (r, results[0]);
                sweep << name (static_cast<Path> (i)) << ',' << hz << ',' << dbu << ',' << voltsToDbu (std::abs (r.harmonics[1]) / std::sqrt (2.0)) << ',' << thdPercent (r);
                for (std::size_t h = 2; h <= 7; ++h) sweep << ',' << ShuntResearch::db (std::abs (r.harmonics[h]) / std::abs (r.harmonics[1]));
                sweep << ',' << d.unmatchedDb << ',' << d.matchedDb << ',' << d.harmonicErrorDbc << ',' << r.stage.minimumPlateMa << ',' << r.stage.maximumPlateMa << ','
                      << r.minimumOhms << ',' << r.maximumOhms << ',' << r.stage.maximumIterations << ',' << r.stage.failures << ',' << r.transformerFailures << '\n';
            }
            std::cout << "coupling " << hz << " Hz " << dbu << " dBu: THD ref " << thdPercent (results[0]) << "% a " << thdPercent (results[1]) << "% b " << thdPercent (results[2]) << "% c " << thdPercent (results[3]) << '%' << std::endl;
        }
}

// Cost of each path per sample at the live internal rate (4x 48 kHz), on bass
// material at +12 dBu RMS. Single thread, Release; observed times on this Mac.
void writeCouplingCpu (const juce::File& root)
{
    constexpr double liveRate = 192000;
    auto out = csv (root, "coupling_cpu.csv", "path,internal_rate,ns_per_internal_sample,percent_core_stereo_48k,stage_iterations_max");
    std::vector<double> grid;
    for (int n = 0; n < static_cast<int> (4 * liveRate); ++n)
        grid.push_back (dbuToPeakVolts (12) * ShuntResearch::material (0, 0, std::fmod (n / 4.0, ShuntResearch::rate)));
    for (int i = 0; i < 4; ++i)
    {
        CouplingPath chain;
        chain.prepare (static_cast<Path> (i), liveRate, 1024, StageDesign {}, vtb1148OutputC());
        double sink = 0;
        const auto begin = std::chrono::steady_clock::now();
        for (auto x : grid) sink += chain.process (x);
        const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();
        const auto ns = 1.0e9 * seconds / static_cast<double> (grid.size());
        // Stereo at 48 kHz host: 2 channels x 4 internal samples per host sample.
        out << name (static_cast<Path> (i)) << ',' << liveRate << ',' << ns << ',' << 100 * ns * 1.0e-9 * 2 * liveRate << ',' << chain.stageDiagnostics().maximumIterations << '\n';
        std::cout << name (static_cast<Path> (i)) << " " << ns << " ns/sample (" << sink << ")" << std::endl;
    }
}

// ---- Live output stage: aliasing of the joint tube + VTB1148 solve ----
// The same method as the transformer's (shunt Step 3d): steep FIR oversampling at
// each factor around the live core, coherent tones and a two-tone case, against a
// 64x render. Task tube-aliasing.
class CoreAtFactor
{
public:
    CoreAtFactor (int factor, double extraDb, VtbTransformerCore::Mode mode)
    {
        filters.clearOversamplingStages();
        if (factor == 1) filters.addDummyOversamplingStage();
        std::size_t index = 0;
        for (int stage = 1; stage < factor; stage *= 2, ++index)
        {
            const auto up = index < MagneticAudio::upWidths.size() ? MagneticAudio::upWidths[index] : 0.08;
            const auto down = index < MagneticAudio::downWidths.size() ? MagneticAudio::downWidths[index] : 0.08;
            filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
                static_cast<float> (up), -120.0f, static_cast<float> (down), -120.0f);
        }
        filters.setUsingIntegerLatency (false); filters.initProcessing (ModuleResearch::capacity);
        core.prepare (ShuntResearch::rate * factor, vtb1148OutputC(), extraDb, {}, false,
            { {}, mode != VtbTransformerCore::Mode::transformer, mode != VtbTransformerCore::Mode::tube });
    }
    void process (juce::AudioBuffer<double>& block) noexcept
    {
        juce::dsp::AudioBlock<double> base (block);
        auto high = filters.processSamplesUp (base);
        for (std::size_t n = 0; n < high.getNumSamples(); ++n)
        {
            std::array<double, 2> frame { high.getSample (0, static_cast<int> (n)), 0.0 };
            core.processFrame (frame, 1);
            high.setSample (0, static_cast<int> (n), frame[0]);
        }
        filters.processSamplesDown (base);
    }
    double latency() const noexcept { return filters.getLatencyInSamples(); }
    VtbTransformerCore core;
private:
    juce::dsp::Oversampling<double> filters { 1 };
};
void writeTubeAliasing (const juce::File& root)
{
    using namespace ShuntResearch;
    auto out = csv (root, "live_aliasing.csv", "mode,factor,extra_drive_db,peak_dbfs,requested_hz,actual_hz,fundamental_dbfs,harmonic_dbc_audio,nonharmonic_dbc_audio,nonharmonic_dbc_worst_band,difference_to_64x_band_dbr,tube_failures,transformer_failures");
    constexpr double peakDb = -6.0;
    for (auto mode : { VtbTransformerCore::Mode::tubeAndTransformer, VtbTransformerCore::Mode::transformer })
        for (double extra : { 0.0, 12.0, 18.0 })
            for (double requested : { 55.0, 440.0, 1000.0, 7001.0, 13001.0, 19001.0, 0.0 })
            {
                // requested 0 = two-tone: 55 Hz at -6 dBFS plus 13001 Hz at -20 dBFS.
                const auto twoTone = requested < 1;
                const auto bin = static_cast<int> (2 * std::round (((twoTone ? 55.0 : requested) * sineSize / rate - 1) / 2) + 1);
                const auto probeBin = static_cast<int> (2 * std::round ((13001.0 * sineSize / rate - 1) / 2) + 1);
                const auto amplitude = std::pow (10.0, peakDb / 20);
                const auto renderAt = [&] (int factor)
                {
                    CoreAtFactor processor (factor, extra, mode);
                    constexpr int warm = 49152;
                    const auto count = warm + static_cast<int> (sineSize) + 1024;
                    std::vector<double> raw (static_cast<std::size_t> (count));
                    juce::AudioBuffer<double> block (1, ModuleResearch::capacity);
                    for (int start = 0; start < count; start += ModuleResearch::capacity)
                    {
                        for (int n = 0; n < ModuleResearch::capacity; ++n)
                        {
                            const auto t = static_cast<double> (start + n) / sineSize;
                            block.setSample (0, n, amplitude * std::sin (2 * pi * bin * t) + (twoTone ? 0.1 * std::sin (2 * pi * probeBin * t) : 0.0));
                        }
                        processor.process (block);
                        for (int n = 0; n < ModuleResearch::capacity && start + n < count; ++n) raw[static_cast<std::size_t> (start + n)] = block.getSample (0, n);
                    }
                    auto window = MagneticValidation::alignTrace (raw, warm, processor.latency(), static_cast<int> (sineSize)).samples;
                    const auto tubeFailures = mode == VtbTransformerCore::Mode::transformer ? 0 : processor.core.getTubeDiagnostics (0).failures;
                    return std::tuple { window, tubeFailures, processor.core.getDiagnostics (0).failures };
                };
                std::array<std::future<std::tuple<std::vector<double>, std::uint64_t, std::uint64_t>>, 5> jobs;
                const std::array factors { 64, 1, 2, 4, 8 };
                for (std::size_t i = 0; i < jobs.size(); ++i) jobs[i] = std::async (std::launch::async, renderAt, factors[i]);
                const auto [reference, referenceTube, referenceIron] = jobs[0].get();
                for (std::size_t i = 1; i < jobs.size(); ++i)
                {
                    const auto [output, tubeFailures, ironFailures] = jobs[i].get();
                    const auto bands = split (output, output, bin);
                    double fundamental = 0, harmonicEnergy = 0, nonHarmonic = 0, worst = 0;
                    for (const auto& b : bands) { fundamental += b.fundamental; harmonicEnergy += b.harmonic; nonHarmonic += b.nonHarmonic; worst = std::max (worst, b.nonHarmonic); }
                    out << (mode == VtbTransformerCore::Mode::transformer ? "transformer" : "tube+transformer") << ',' << factors[i] << ',' << extra << ',' << peakDb << ',' << requested << ',' << bin * rate / sineSize << ','
                        << powerDb (fundamental) << ',' << powerDb (harmonicEnergy / fundamental) << ',' << powerDb (nonHarmonic / fundamental) << ',' << powerDb (worst / fundamental) << ','
                        << bandDifference (output, reference) << ',' << tubeFailures + referenceTube << ',' << ironFailures + referenceIron << '\n';
                }
                out.flush();
                std::cout << "live alias " << static_cast<int> (mode) << ' ' << extra << ' ' << requested << std::endl;
            }
}

// ---- Gain tube report (docs/GainTube.md): task gain-tube-report ----
void writeGainTubeReport (const juce::File& root)
{
    using GainTube::Tube;
    // 1. Distortion of the cell against gain reduction and level (memoryless, so
    //    frequency-independent): one period, 1024 points.
    auto thd = csv (root, "gain_tube_thd.csv", "tube,grid_level_db,level_dbu,gain_reduction_db,gain_error_db,thd_db,thd_percent,h2_dbc,h3_dbc,h5_dbc");
    for (auto tube : { Tube::ge5670, Tube::ge6386 })
        for (double gridDb : { -12.0, 0.0, 12.0 })
            for (double dbu : { -10.0, 0.0, 4.0, 10.0, 18.0 })
                for (double reduction = 0; reduction <= 30.01; reduction += 2)
                {
                    GainTube::Cell cell;
                    cell.configure ({ tube, gridDb, 0.0 });
                    const auto peak = std::pow (10.0, (dbu - 18.0) / 20.0);
                    constexpr int count = 1024;
                    std::array<std::complex<double>, 12> h {};
                    for (int n = 0; n < count; ++n)
                    {
                        const auto phase = 2.0 * ShuntResearch::pi * n / count;
                        const auto y = cell.process (peak * std::sin (phase), reduction);
                        for (std::size_t k = 0; k < h.size(); ++k) h[k] += y * std::polar (1.0, -static_cast<double> (k) * phase) * (2.0 / count);
                    }
                    double sum = 0;
                    for (std::size_t k = 2; k < h.size(); ++k) sum += std::norm (h[k]);
                    const auto t = 10.0 * std::log10 (sum / std::norm (h[1]) + 1.0e-30);
                    const auto rel = [&h] (std::size_t k) { return 20.0 * std::log10 (std::abs (h[k]) / std::abs (h[1]) + 1.0e-30); };
                    thd << (tube == Tube::ge5670 ? "5670" : "6386") << ',' << gridDb << ',' << dbu << ',' << reduction << ','
                        << 20.0 * std::log10 (std::abs (h[1]) / peak) + reduction << ',' << t << ',' << 100.0 * std::pow (10.0, t / 20.0) << ','
                        << rel (2) << ',' << rel (3) << ',' << rel (5) << '\n';
                }
    // 2. Aliasing at a fixed GR: steep oversampling around the cell against 64x,
    //    the same method as the transformer and output stage.
    auto alias = csv (root, "gain_tube_aliasing.csv", "tube,factor,gain_reduction_db,peak_dbfs,requested_hz,nonharmonic_dbc_audio,difference_to_64x_band_dbr");
    for (auto tube : { Tube::ge5670, Tube::ge6386 })
        for (double reduction : { 0.0, 10.0, 20.0 })
            for (double requested : { 1000.0, 7001.0, 13001.0, 19001.0 })
            {
                using namespace ShuntResearch;
                const auto bin = static_cast<int> (2 * std::round ((requested * sineSize / rate - 1) / 2) + 1);
                const auto renderAt = [&] (int factor)
                {
                    juce::dsp::Oversampling<double> filters { 1 };
                    filters.clearOversamplingStages();
                    if (factor == 1) filters.addDummyOversamplingStage();
                    std::size_t index = 0;
                    for (int stage = 1; stage < factor; stage *= 2, ++index)
                    {
                        const auto up = index < MagneticAudio::upWidths.size() ? MagneticAudio::upWidths[index] : 0.08;
                        const auto down = index < MagneticAudio::downWidths.size() ? MagneticAudio::downWidths[index] : 0.08;
                        filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple, static_cast<float> (up), -120.0f, static_cast<float> (down), -120.0f);
                    }
                    filters.setUsingIntegerLatency (false); filters.initProcessing (ModuleResearch::capacity);
                    GainTube::Cell cell;
                    cell.configure ({ tube, 0.0, 0.0 });
                    constexpr int warm = 8192;
                    const auto count = warm + static_cast<int> (sineSize) + 1024;
                    std::vector<double> raw (static_cast<std::size_t> (count));
                    juce::AudioBuffer<double> block (1, ModuleResearch::capacity);
                    for (int start = 0; start < count; start += ModuleResearch::capacity)
                    {
                        for (int n = 0; n < ModuleResearch::capacity; ++n)
                            block.setSample (0, n, 0.5 * std::sin (2 * pi * bin * static_cast<double> (start + n) / sineSize));
                        juce::dsp::AudioBlock<double> base (block);
                        auto high = filters.processSamplesUp (base);
                        for (std::size_t n = 0; n < high.getNumSamples(); ++n)
                            high.setSample (0, static_cast<int> (n), cell.process (high.getSample (0, static_cast<int> (n)), reduction));
                        filters.processSamplesDown (base);
                        for (int n = 0; n < ModuleResearch::capacity && start + n < count; ++n) raw[static_cast<std::size_t> (start + n)] = block.getSample (0, n);
                    }
                    return MagneticValidation::alignTrace (raw, warm, filters.getLatencyInSamples(), static_cast<int> (sineSize)).samples;
                };
                const auto reference = renderAt (64);
                for (int factor : { 1, 2, 4, 8 })
                {
                    const auto output = renderAt (factor);
                    const auto bands = split (output, output, bin);
                    double fundamental = 0, nonHarmonic = 0;
                    for (const auto& b : bands) { fundamental += b.fundamental; nonHarmonic += b.nonHarmonic; }
                    alias << (tube == Tube::ge5670 ? "5670" : "6386") << ',' << factor << ',' << reduction << ",-6," << requested << ','
                          << powerDb (nonHarmonic / fundamental) << ',' << bandDifference (output, reference) << '\n';
                }
                alias.flush();
                std::cout << "gain tube alias " << static_cast<int> (tube) << ' ' << reduction << ' ' << requested << std::endl;
            }
    // 2b. The 5751 amplifier alone: THD against level, and aliasing against 64x.
    auto ampThd = csv (root, "amp_tube_thd.csv", "level_dbu_at_amp,gain_error_db,thd_percent,h2_dbc,h3_dbc");
    for (double dbu = -20; dbu <= 30.01; dbu += 2)
    {
        const auto peak = std::pow (10.0, (dbu - 18.0) / 20.0);
        constexpr int count = 1024;
        std::array<std::complex<double>, 12> h {};
        for (int n = 0; n < count; ++n)
        {
            const auto phase = 2.0 * ShuntResearch::pi * n / count;
            const auto y = AmpTube::stage().process (peak * std::sin (phase));
            for (std::size_t k = 0; k < h.size(); ++k) h[k] += y * std::polar (1.0, -static_cast<double> (k) * phase) * (2.0 / count);
        }
        double sum = 0;
        for (std::size_t k = 2; k < h.size(); ++k) sum += std::norm (h[k]);
        ampThd << dbu << ',' << 20.0 * std::log10 (std::abs (h[1]) / peak) << ',' << 100.0 * std::sqrt (sum) / std::abs (h[1]) << ','
               << 20.0 * std::log10 (std::abs (h[2]) / std::abs (h[1])) << ',' << 20.0 * std::log10 (std::abs (h[3]) / std::abs (h[1])) << '\n';
    }
    auto ampAlias = csv (root, "amp_tube_aliasing.csv", "factor,peak_dbfs,requested_hz,nonharmonic_dbc_audio,difference_to_64x_band_dbr");
    for (double peakDb : { -6.0, 6.0 })
        for (double requested : { 1000.0, 7001.0, 13001.0, 19001.0 })
        {
            using namespace ShuntResearch;
            const auto bin = static_cast<int> (2 * std::round ((requested * sineSize / rate - 1) / 2) + 1);
            const auto renderAt = [&] (int factor)
            {
                juce::dsp::Oversampling<double> filters { 1 };
                filters.clearOversamplingStages();
                if (factor == 1) filters.addDummyOversamplingStage();
                std::size_t index = 0;
                for (int stage = 1; stage < factor; stage *= 2, ++index)
                {
                    const auto up = index < MagneticAudio::upWidths.size() ? MagneticAudio::upWidths[index] : 0.08;
                    const auto down = index < MagneticAudio::downWidths.size() ? MagneticAudio::downWidths[index] : 0.08;
                    filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple, static_cast<float> (up), -120.0f, static_cast<float> (down), -120.0f);
                }
                filters.setUsingIntegerLatency (false); filters.initProcessing (ModuleResearch::capacity);
                constexpr int warm = 8192;
                const auto count = warm + static_cast<int> (sineSize) + 1024;
                std::vector<double> raw (static_cast<std::size_t> (count));
                juce::AudioBuffer<double> block (1, ModuleResearch::capacity);
                const auto amplitude = std::pow (10.0, peakDb / 20.0);
                for (int start = 0; start < count; start += ModuleResearch::capacity)
                {
                    for (int n = 0; n < ModuleResearch::capacity; ++n)
                        block.setSample (0, n, amplitude * std::sin (2 * pi * bin * static_cast<double> (start + n) / sineSize));
                    juce::dsp::AudioBlock<double> base (block);
                    auto high = filters.processSamplesUp (base);
                    for (std::size_t n = 0; n < high.getNumSamples(); ++n)
                        high.setSample (0, static_cast<int> (n), AmpTube::stage().process (high.getSample (0, static_cast<int> (n))));
                    filters.processSamplesDown (base);
                    for (int n = 0; n < ModuleResearch::capacity && start + n < count; ++n) raw[static_cast<std::size_t> (start + n)] = block.getSample (0, n);
                }
                return MagneticValidation::alignTrace (raw, warm, filters.getLatencyInSamples(), static_cast<int> (sineSize)).samples;
            };
            const auto reference = renderAt (64);
            for (int factor : { 1, 2, 4, 8 })
            {
                const auto output = renderAt (factor);
                const auto bands = split (output, output, bin);
                double fundamental = 0, nonHarmonic = 0;
                for (const auto& b : bands) { fundamental += b.fundamental; nonHarmonic += b.nonHarmonic; }
                ampAlias << factor << ',' << peakDb << ',' << requested << ',' << powerDb (nonHarmonic / fundamental) << ',' << bandDifference (output, reference) << '\n';
            }
        }
    // 3. Whole plugin CPU, gain tube in and out (defaults otherwise), 48 kHz stereo.
    auto cpu = csv (root, "gain_tube_cpu.csv", "tube,amp,threshold_db,ns_per_host_sample_stereo,percent_core_48k");
    for (const auto& [tubeIn, choice, ampIn] : { std::tuple { false, 0.0f, false }, std::tuple { true, 0.0f, false }, std::tuple { true, 1.0f, false }, std::tuple { true, 0.0f, true } })
    {
        PluginProcessor processor;
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                if (ranged->paramID == "gainTubeOut") ranged->setValueNotifyingHost (tubeIn ? 0.0f : 1.0f);
                if (ranged->paramID == "ampTubeOut") ranged->setValueNotifyingHost (ampIn ? 0.0f : 1.0f);
                if (ranged->paramID == "threshold") ranged->setValueNotifyingHost (ranged->convertTo0to1 (-30.0f));
            }
        processor.getGainTubeVoicing().setCurrent ({ choice });
        processor.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> audio (2, 256);
        juce::MidiBuffer midi;
        const auto block = [&] (int b)
        {
            for (int n = 0; n < 256; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample (ch, n, 0.3f * static_cast<float> (std::sin (2 * ShuntResearch::pi * 110.0 * (b * 256 + n) / 48000.0)));
            processor.processBlock (audio, midi);
        };
        for (int b = 0; b < 200; ++b) block (b);
        const auto begin = std::chrono::steady_clock::now();
        constexpr int blocks = 2000;
        for (int b = 0; b < blocks; ++b) block (b);
        const auto ns = 1.0e9 * std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count() / (blocks * 256.0);
        cpu << (tubeIn ? (choice > 0.5f ? "6386" : "5670") : "out") << ',' << (ampIn ? "in" : "out") << ",-30," << ns << ',' << 100.0 * ns * 48000.0 * 1.0e-9 << '\n';
        std::cout << "gain tube cpu " << tubeIn << ' ' << choice << " amp " << ampIn << ' ' << 100.0 * ns * 48000.0 * 1.0e-9 << '%' << std::endl;
    }
}

void writeWav (const juce::File& path, const std::array<std::vector<double>, 2>& audio, double gain)
{
    juce::AudioBuffer<float> buffer (2, static_cast<int> (audio[0].size()));
    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < buffer.getNumSamples(); ++n) buffer.setSample (ch, n, static_cast<float> (gain * audio[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]));
    auto stream = path.createOutputStream(); REQUIRE (stream != nullptr);
    REQUIRE (stream->setPosition (0)); REQUIRE (stream->truncate().wasOk());
    std::unique_ptr<juce::OutputStream> output = std::move (stream);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions().withSampleRate (ShuntResearch::rate).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    REQUIRE (writer != nullptr); REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
}
// Synthetic material at 48 kHz, set to an RMS grid level, run through each
// path at 8x and returned in full-scale units (peak 1.0 = +18 dBu).
std::array<std::vector<double>, 2> renderPath (Path path, const std::array<std::vector<double>, 2>& gridVolts)
{
    constexpr int blockSize = 512;
    juce::dsp::Oversampling<double> oversampling (2, 3, juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple, true, true);
    oversampling.initProcessing (blockSize);
    std::array<CouplingPath, 2> chains;
    for (auto& chain : chains) chain.prepare (path, couplingRate, perBlockSamples, StageDesign {}, vtb1148OutputC());
    const auto latency = static_cast<int> (oversampling.getLatencyInSamples());
    const auto length = static_cast<int> (gridVolts[0].size());
    juce::AudioBuffer<double> buffer (2, blockSize);
    std::array<std::vector<double>, 2> out;
    for (int start = 0; start < length + latency; start += blockSize)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                buffer.setSample (ch, n, start + n < length ? gridVolts[static_cast<std::size_t> (ch)][static_cast<std::size_t> (start + n)] : 0.0);
        juce::dsp::AudioBlock<double> block (buffer);
        auto high = oversampling.processSamplesUp (block);
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t n = 0; n < high.getNumSamples(); ++n)
                high.setSample (static_cast<int> (ch), static_cast<int> (n), chains[ch].process (high.getSample (static_cast<int> (ch), static_cast<int> (n))));
        oversampling.processSamplesDown (block);
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                if (start + n >= latency && static_cast<int> (out[ch].size()) < length)
                    out[ch].push_back (buffer.getSample (static_cast<int> (ch), n) / fullScalePeakVolts());
    }
    return out;
}
void writeCouplingRenders (const juce::File& root)
{
    const auto audioRoot = root.getChildFile ("coupling_audio"); REQUIRE (audioRoot.createDirectory().wasOk());
    auto renders = csv (root, "coupling_renders.csv", "file,material,grid_dbu_rms,path,output_dbu_rms,unmatched_difference_db,matched_difference_db,band_20_250_difference_db,band_250_2k_difference_db,band_2k_20k_difference_db");
    constexpr std::array materials { "bass", "drums", "vocal" };
    const auto length = static_cast<std::size_t> (ShuntResearch::rate);
    for (int type = 0; type < 3; ++type)
    {
        std::array<std::vector<double>, 2> source;
        double power = 0;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t n = 0; n < length; ++n)
            {
                source[ch].push_back (ShuntResearch::material (type, static_cast<int> (ch), static_cast<double> (n)));
                power += source[ch].back() * source[ch].back();
            }
        const auto rms = std::sqrt (power / static_cast<double> (2 * length));
        for (double dbu : { 0.0, 12.0, 18.0 })
        {
            auto grid = source;
            const auto scale = dbuToPeakVolts (dbu) / std::sqrt (2.0) / rms;
            for (auto& channel : grid) for (auto& x : channel) x *= scale;
            const auto stem = juce::String (materials[static_cast<std::size_t> (type)]) + "_" + juce::String (juce::roundToInt (dbu)) + "dBu";
            writeWav (audioRoot.getChildFile (stem + "_input.wav"), grid, 1 / fullScalePeakVolts());
            std::array<std::future<std::array<std::vector<double>, 2>>, 4> jobs;
            for (std::size_t i = 0; i < jobs.size(); ++i) jobs[i] = std::async (std::launch::async, renderPath, static_cast<Path> (i), grid);
            std::array<std::array<std::vector<double>, 2>, 4> outputs;
            for (std::size_t i = 0; i < jobs.size(); ++i) outputs[i] = jobs[i].get();
            for (std::size_t i = 0; i < outputs.size(); ++i)
            {
                const auto file = stem + "_" + name (static_cast<Path> (i)) + ".wav";
                writeWav (audioRoot.getChildFile (file), outputs[i], 1);
                double error = 0, reference = 0, cross = 0, self = 0, out = 0;
                std::vector<double> difference, referenceSignal;
                for (std::size_t ch = 0; ch < 2; ++ch)
                    for (std::size_t n = 0; n < length; ++n)
                    {
                        const auto y = outputs[i][ch][n], r = outputs[0][ch][n];
                        error += (y - r) * (y - r); reference += r * r; cross += y * r; self += y * y; out += y * y;
                        if (ch == 0) { difference.push_back (y - r); referenceSignal.push_back (r); }
                    }
                const auto gain = cross / std::max (self, 1.0e-30);
                double matched = 0;
                for (std::size_t ch = 0; ch < 2; ++ch)
                    for (std::size_t n = 0; n < length; ++n) matched += std::pow (gain * outputs[i][ch][n] - outputs[0][ch][n], 2);
                // Band split of the (unmatched) difference, left channel, relative to the reference band.
                std::size_t size = 1;
                while (size < length) size <<= 1;
                difference.resize (size, 0); referenceSignal.resize (size, 0);
                const auto bins = ShuntResearch::fft (difference), refBins = ShuntResearch::fft (referenceSignal);
                const auto band = [&] (double lo, double hi)
                {
                    double e = 0, p = 0;
                    for (std::size_t k = 1; k < size / 2; ++k)
                    {
                        const auto f = static_cast<double> (k) * ShuntResearch::rate / static_cast<double> (size);
                        if (f >= lo && f < hi) { e += std::norm (bins[k]); p += std::norm (refBins[k]); }
                    }
                    return ShuntResearch::powerDb (e / std::max (p, 1.0e-30));
                };
                renders << file << ',' << materials[static_cast<std::size_t> (type)] << ',' << dbu << ',' << name (static_cast<Path> (i)) << ','
                        << voltsToDbu (std::sqrt (out / static_cast<double> (2 * length)) * fullScalePeakVolts()) << ','
                        << ShuntResearch::powerDb (error / reference) << ',' << ShuntResearch::powerDb (matched / reference) << ','
                        << band (20, 250) << ',' << band (250, 2000) << ',' << band (2000, 20000) << '\n';
            }
            std::cout << "rendered " << stem << std::endl;
        }
    }
}
}

TEST_CASE ("Koren triode derivatives match finite differences", "[tube-stage]")
{
    const TriodeParameters p { 16.5, 1.3, 1180, 84, 300 };
    for (const auto [va, vg] : { std::pair { 150.0, -8.0 }, std::pair { 250.0, -10.5 }, std::pair { 40.0, 0.0 }, std::pair { 400.0, -25.0 } })
    {
        const auto c = triodeCurrent (p, va, vg);
        const auto h = 1.0e-4;
        const auto dVa = (triodeCurrent (p, va + h, vg).ia - triodeCurrent (p, va - h, vg).ia) / (2 * h);
        const auto dVg = (triodeCurrent (p, va, vg + h).ia - triodeCurrent (p, va, vg - h).ia) / (2 * h);
        CAPTURE (va, vg);
        CHECK (std::abs (c.dVa - dVa) <= 1.0e-6 * std::abs (dVa) + 1.0e-12);
        CHECK (std::abs (c.dVg - dVg) <= 1.0e-6 * std::abs (dVg) + 1.0e-12);
    }
    CHECK (triodeCurrent (p, 0, 0).ia <= 0);
    CHECK (triodeCurrent (p, -10, 0).ia <= 0);
}

TEST_CASE ("Export tube stage reports", "[tube-report]")
{
    const auto* taskValue = std::getenv ("LUMIERE_MODULE_TASK");
    const std::string task = taskValue ? taskValue : "";
    if (task != "tube-fit" && task != "tube-coupling" && task != "tube-coupling-cpu" && task != "tube-aliasing" && task != "gain-tube-fit" && task != "gain-tube-report") { SUCCEED ("Explicit tube report opt-in"); return; }
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR"); REQUIRE (directory != nullptr);
    const juce::File root (juce::String::fromUTF8 (directory)); REQUIRE (root.createDirectory().wasOk());
    if (task == "tube-fit") writeFit (root);
    else if (task == "tube-coupling-cpu") writeCouplingCpu (root);
    else if (task == "tube-aliasing") writeTubeAliasing (root);
    else if (task == "gain-tube-fit") writeGainTubeFit (root);
    else if (task == "gain-tube-report") writeGainTubeReport (root);
    else { writeCoupling (root); writeCouplingRenders (root); }
}

TEST_CASE ("Recorded triode fits reproduce the digitized plate curves", "[tube-stage]")
{
    CHECK (voltageRms (rca12bh7a(), TubeCurves::rca12bh7a) < 4.33);
    CHECK (voltageRms (ge5751(), TubeCurves::ge5751) < 2.03);
    // Published class A1 point within the fit's spread (sheet: 11.5 mA, 3.1 mS, 5.3 kOhm).
    const auto c = triodeCurrent (rca12bh7a(), 250, -10.5);
    CHECK (std::abs (c.ia * 1000 - 11.5) < 1.2);
    CHECK (std::abs (c.dVg * 1000 - 3.1) < 0.3);
    CHECK (std::abs (1 / c.dVa / 1000 - 5.3) < 0.6);
}

TEST_CASE ("Cathode follower fixture sits inside the 12BH7-A ratings", "[tube-stage]")
{
    const StageDesign design;
    const auto vk = CathodeFollower<InductiveLoad>::quiescentCathode (design);
    const auto c = triodeCurrent (design.tube, design.supply - vk, design.bias - vk);
    CHECK (c.ia * 1000 < 20);                       // Cathode current, 20 mA max per section.
    CHECK ((design.supply - vk) * c.ia < 3.5);      // Plate dissipation, 3.5 W max.
    CHECK (vk < 200);                               // Heater-cathode, 200 V max (heater at ground).
    CathodeFollower<InductiveLoad> stage;
    stage.prepare (couplingRate, design, vtb1148OutputC());
    CHECK (stage.outputOhms (0) > 100);
    CHECK (stage.outputOhms (0) < 140);
}

TEST_CASE ("Coupled tube and transformer paths converge and agree at low level", "[tube-stage]")
{
    const auto hot = runTone (Path::reference, 20, 30);
    CHECK (hot.stage.failures == 0);
    CHECK (std::all_of (hot.output.begin(), hot.output.end(), [] (double x) { return ModuleSpike::isFinite (x); }));
    const auto reference = runTone (Path::reference, 1000, -10);
    for (auto path : { Path::linearInterface, Path::perBlockInterface, Path::uncoupled })
    {
        const auto candidate = runTone (path, 1000, -10);
        CAPTURE (name (path));
        CHECK (candidate.stage.failures == 0);
        CHECK (candidate.transformerFailures == 0);
        // Below saturation the linear interface matches (measured about -97 dB);
        // the uncoupled path misses the tube's source resistance (about -36 dB).
        if (path == Path::uncoupled) CHECK (compare (candidate, reference).matchedDb > -45);
        else CHECK (compare (candidate, reference).matchedDb < -80);
    }
}
