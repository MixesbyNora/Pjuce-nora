#pragma once
#include <array>

// Average plate characteristics digitized from the original scans in
// docs/references (RCA-12BH7A-1955.pdf, GE-5751-1952.pdf). Method: page rendered,
// grid lines located by pixel density, then each curve's crossing found along
// horizontal rows between grid lines (docs/TubeStage.md). Plot readings, not
// measurements; grid labels, text and the curve ends are excluded.
namespace TubeCurves
{
struct Point { double vg, va, ma; };

// RCA 12BH7-A, Mar 1955, PDF p4, each triode unit. 10 V and 1 mA per grid square.
inline constexpr std::array rca12bh7a {
    Point { 0, 5.7, 1.5 }, Point { 0, 10.5, 2.5 }, Point { 0, 15.3, 3.5 }, Point { 0, 20.2, 4.5 }, Point { 0, 24.8, 5.5 },
    Point { 0, 29.4, 6.5 }, Point { 0, 34.1, 7.5 }, Point { 0, 38.4, 8.5 }, Point { 0, 41.7, 9.5 }, Point { 0, 50.4, 11.5 },
    Point { 0, 54.5, 12.5 }, Point { 0, 58.8, 13.5 }, Point { 0, 66.0, 15.5 }, Point { 0, 69.7, 16.5 }, Point { 0, 73.3, 17.5 },
    Point { 0, 76.6, 18.5 }, Point { 0, 80.3, 19.5 }, Point { 0, 83.5, 20.5 }, Point { 0, 87.2, 21.5 }, Point { 0, 90.5, 22.5 },
    Point { 0, 93.7, 23.5 }, Point { 0, 100.5, 25.5 }, Point { 0, 103.9, 26.5 }, Point { 0, 106.9, 27.5 }, Point { 0, 112.1, 29.5 },
    Point { 0, 116.3, 30.5 }, Point { 0, 119.7, 31.5 }, Point { 0, 122.3, 32.5 }, Point { 0, 125.6, 33.5 }, Point { 0, 129.3, 34.5 },
    Point { 0, 131.5, 35.5 }, Point { 0, 138.8, 37.5 }, Point { 0, 144.5, 39.5 }, Point { 0, 148.8, 40.5 }, Point { 0, 157.0, 43.5 },
    Point { 0, 160.0, 44.5 }, Point { 0, 162.6, 45.5 }, Point { 0, 166.5, 46.5 }, Point { 0, 169.8, 47.5 }, Point { 0, 172.4, 48.5 },
    Point { -5, 74.4, 1.5 }, Point { -5, 87.4, 2.5 }, Point { -5, 97.2, 3.5 }, Point { -5, 105.9, 4.5 }, Point { -5, 114.1, 5.5 },
    Point { -5, 121.2, 6.5 }, Point { -5, 128.0, 7.5 }, Point { -5, 133.2, 8.5 }, Point { -5, 138.4, 9.5 }, Point { -5, 143.6, 10.5 },
    Point { -5, 148.8, 11.5 }, Point { -5, 152.9, 12.5 }, Point { -5, 157.2, 13.5 }, Point { -5, 161.3, 14.5 }, Point { -5, 165.5, 15.5 },
    Point { -5, 169.2, 16.5 }, Point { -5, 173.7, 17.5 }, Point { -5, 178.3, 18.5 }, Point { -5, 181.5, 19.5 }, Point { -5, 186.1, 20.5 },
    Point { -5, 190.0, 21.5 }, Point { -5, 194.3, 22.5 }, Point { -5, 198.8, 23.5 }, Point { -5, 201.5, 24.5 }, Point { -5, 206.7, 25.5 },
    Point { -5, 210.8, 26.5 }, Point { -5, 214.5, 27.5 }, Point { -5, 219.2, 28.5 }, Point { -5, 222.1, 29.5 }, Point { -5, 227.3, 30.5 },
    Point { -5, 231.2, 31.5 }, Point { -5, 235.3, 32.5 }, Point { -5, 239.8, 33.5 }, Point { -5, 242.8, 34.5 }, Point { -5, 248.9, 35.5 },
    Point { -5, 251.7, 36.5 }, Point { -5, 255.7, 37.5 }, Point { -5, 259.8, 38.5 }, Point { -5, 263.9, 39.5 },
    Point { -10, 165.9, 1.5 }, Point { -10, 179.6, 2.5 }, Point { -10, 191.1, 3.5 }, Point { -10, 200.8, 4.5 }, Point { -10, 209.3, 5.5 },
    Point { -10, 217.5, 6.5 }, Point { -10, 222.9, 7.5 }, Point { -10, 229.0, 8.5 }, Point { -10, 234.8, 9.5 }, Point { -10, 244.4, 11.5 },
    Point { -10, 249.2, 12.5 }, Point { -10, 254.1, 13.5 }, Point { -10, 264.1, 15.5 }, Point { -10, 268.9, 16.5 }, Point { -10, 273.8, 17.5 },
    Point { -10, 278.6, 18.5 }, Point { -10, 283.4, 19.5 }, Point { -10, 288.6, 20.5 }, Point { -10, 292.9, 21.5 }, Point { -10, 298.3, 22.5 },
    Point { -10, 301.2, 23.5 }, Point { -10, 306.8, 24.5 }, Point { -10, 311.3, 25.5 }, Point { -10, 316.3, 26.5 }, Point { -10, 320.9, 27.5 },
    Point { -10, 325.5, 28.5 }, Point { -10, 330.4, 29.5 }, Point { -10, 334.5, 30.5 }, Point { -10, 339.5, 31.5 }, Point { -10, 344.1, 32.5 },
    Point { -15, 235.5, 1.5 }, Point { -15, 249.8, 2.5 }, Point { -15, 263.2, 3.5 }, Point { -15, 274.3, 4.5 }, Point { -15, 284.1, 5.5 },
    Point { -15, 293.1, 6.5 }, Point { -15, 300.5, 7.5 }, Point { -15, 308.3, 8.5 }, Point { -15, 315.0, 9.5 }, Point { -15, 321.3, 10.5 },
    Point { -15, 328.7, 11.5 }, Point { -15, 335.4, 12.5 }, Point { -15, 341.5, 13.5 }, Point { -15, 349.1, 14.5 }, Point { -15, 355.8, 15.5 },
    Point { -15, 362.5, 16.5 }, Point { -15, 369.4, 17.5 }, Point { -15, 376.0, 18.5 }, Point { -15, 382.5, 19.5 }, Point { -15, 389.2, 20.5 },
    Point { -15, 395.9, 21.5 }, Point { -15, 401.7, 22.5 }, Point { -15, 408.9, 23.5 }, Point { -15, 415.2, 24.5 }, Point { -15, 421.5, 25.5 },
    Point { -20, 308.5, 1.5 }, Point { -20, 329.3, 2.5 }, Point { -20, 346.5, 3.5 }, Point { -20, 359.5, 4.5 }, Point { -20, 371.0, 5.5 },
    Point { -20, 382.5, 6.5 }, Point { -20, 392.2, 7.5 }, Point { -20, 401.1, 8.5 }, Point { -20, 410.4, 9.5 }, Point { -20, 419.5, 10.5 },
    Point { -20, 427.8, 11.5 }, Point { -20, 435.3, 12.5 }, Point { -20, 442.3, 13.5 }, Point { -20, 449.9, 14.5 }, Point { -20, 457.4, 15.5 },
    Point { -20, 463.5, 16.5 }, Point { -20, 470.4, 17.5 }, Point { -20, 477.2, 18.5 }, Point { -20, 481.7, 19.5 },
    Point { -25, 385.5, 1.5 }, Point { -25, 409.5, 2.5 }, Point { -25, 427.3, 3.5 }, Point { -25, 440.8, 4.5 }, Point { -25, 453.8, 5.5 },
    Point { -25, 464.8, 6.5 }, Point { -25, 474.4, 7.5 }, Point { -25, 483.7, 8.5 }, Point { -25, 492.4, 9.5 }, Point { -25, 500.4, 10.5 },
    Point { -25, 509.2, 11.5 }, Point { -25, 517.1, 12.5 }, Point { -25, 523.4, 13.5 }, Point { -25, 530.3, 14.5 },
    Point { -30, 457.2, 1.5 }, Point { -30, 481.9, 2.5 }, Point { -30, 500.8, 3.5 }, Point { -30, 517.1, 4.5 }, Point { -30, 530.1, 5.5 },
    Point { -30, 542.9, 6.5 }, Point { -30, 553.2, 7.5 },
    Point { -35, 525.3, 1.5 }, Point { -35, 551.3, 2.5 }, Point { -35, 571.5, 3.5 },
    Point { -40, 598.8, 1.5 }
};

// GE GL-5751, ETX-245A, Jun 1952, p2, triode unit. 4 V and 0.1 mA per grid square.
inline constexpr std::array ge5751 {
    Point { 0, 8.7, 0.35 }, Point { 0, 14.0, 0.55 }, Point { 0, 19.8, 0.75 }, Point { 0, 33.1, 1.15 }, Point { 0, 40.5, 1.35 },
    Point { 0, 48.2, 1.55 }, Point { 0, 56.1, 1.75 }, Point { 0, 63.6, 1.95 }, Point { 0, 71.0, 2.15 }, Point { 0, 77.9, 2.35 },
    Point { 0, 84.4, 2.55 }, Point { 0, 91.2, 2.75 }, Point { 0, 97.6, 2.95 }, Point { 0, 103.8, 3.15 }, Point { 0, 109.7, 3.35 },
    Point { 0, 115.4, 3.55 }, Point { 0, 121.4, 3.75 }, Point { 0, 127.1, 3.95 }, Point { 0, 132.8, 4.15 }, Point { 0, 138.8, 4.35 },
    Point { 0, 143.9, 4.55 }, Point { 0, 149.7, 4.75 }, Point { 0, 165.9, 5.35 }, Point { 0, 171.2, 5.55 },
    Point { -0.5, 29.3, 0.35 }, Point { -0.5, 41.3, 0.55 }, Point { -0.5, 52.8, 0.75 }, Point { -0.5, 62.6, 0.95 }, Point { -0.5, 71.4, 1.15 },
    Point { -0.5, 79.7, 1.35 }, Point { -0.5, 87.6, 1.55 }, Point { -0.5, 95.1, 1.75 }, Point { -0.5, 102.2, 1.95 }, Point { -0.5, 109.2, 2.15 },
    Point { -0.5, 116.1, 2.35 }, Point { -0.5, 122.6, 2.55 }, Point { -0.5, 135.1, 2.95 }, Point { -0.5, 141.3, 3.15 }, Point { -0.5, 147.2, 3.35 },
    Point { -0.5, 153.2, 3.55 }, Point { -0.5, 159.5, 3.75 }, Point { -0.5, 165.3, 3.95 }, Point { -0.5, 171.3, 4.15 }, Point { -0.5, 177.2, 4.35 },
    Point { -0.5, 183.1, 4.55 }, Point { -0.5, 188.8, 4.75 }, Point { -0.5, 194.5, 4.95 },
    Point { -1, 69.2, 0.35 }, Point { -1, 81.3, 0.55 }, Point { -1, 93.0, 0.75 }, Point { -1, 103.3, 0.95 }, Point { -1, 112.3, 1.15 },
    Point { -1, 120.8, 1.35 }, Point { -1, 128.6, 1.55 }, Point { -1, 135.7, 1.75 }, Point { -1, 142.9, 1.95 }, Point { -1, 149.7, 2.15 },
    Point { -1, 156.3, 2.35 }, Point { -1, 169.3, 2.75 }, Point { -1, 175.7, 2.95 }, Point { -1, 188.4, 3.35 }, Point { -1, 195.0, 3.55 },
    Point { -1, 207.1, 3.95 }, Point { -1, 213.1, 4.15 }, Point { -1, 219.0, 4.35 }, Point { -1, 224.3, 4.55 }, Point { -1, 230.7, 4.75 },
    Point { -1, 236.0, 4.95 },
    Point { -1.5, 101.5, 0.35 }, Point { -1.5, 115.3, 0.55 }, Point { -1.5, 126.9, 0.75 }, Point { -1.5, 137.3, 0.95 }, Point { -1.5, 147.5, 1.15 },
    Point { -1.5, 156.3, 1.35 }, Point { -1.5, 164.7, 1.55 }, Point { -1.5, 172.4, 1.75 }, Point { -1.5, 179.8, 1.95 }, Point { -1.5, 187.5, 2.15 },
    Point { -1.5, 194.3, 2.35 }, Point { -1.5, 201.0, 2.55 }, Point { -1.5, 208.0, 2.75 }, Point { -1.5, 215.2, 2.95 }, Point { -1.5, 221.4, 3.15 },
    Point { -1.5, 227.9, 3.35 }, Point { -1.5, 234.3, 3.55 }, Point { -1.5, 241.3, 3.75 }, Point { -1.5, 247.7, 3.95 },
    Point { -2, 136.6, 0.35 }, Point { -2, 150.5, 0.55 }, Point { -2, 163.1, 0.75 }, Point { -2, 174.0, 0.95 }, Point { -2, 184.0, 1.15 },
    Point { -2, 192.9, 1.35 }, Point { -2, 201.1, 1.55 }, Point { -2, 209.5, 1.75 }, Point { -2, 217.6, 1.95 }, Point { -2, 225.0, 2.15 },
    Point { -2, 232.5, 2.35 }, Point { -2, 239.7, 2.55 }, Point { -2, 246.4, 2.75 }, Point { -2, 253.7, 2.95 }, Point { -2, 260.2, 3.15 },
    Point { -2, 267.2, 3.35 }, Point { -2, 273.2, 3.55 },
    Point { -2.5, 169.7, 0.35 }, Point { -2.5, 186.0, 0.55 }, Point { -2.5, 200.3, 0.75 }, Point { -2.5, 212.9, 0.95 }, Point { -2.5, 223.9, 1.15 },
    Point { -2.5, 234.4, 1.35 }, Point { -2.5, 243.7, 1.55 }, Point { -2.5, 251.8, 1.75 }, Point { -2.5, 259.6, 1.95 }, Point { -2.5, 267.5, 2.15 },
    Point { -2.5, 274.5, 2.35 }, Point { -2.5, 288.3, 2.75 }, Point { -2.5, 295.1, 2.95 }, Point { -2.5, 300.7, 3.15 }, Point { -2.5, 307.3, 3.35 },
    Point { -3, 200.9, 0.35 }, Point { -3, 219.8, 0.55 }, Point { -3, 235.3, 0.75 }, Point { -3, 247.4, 0.95 }, Point { -3, 258.0, 1.15 },
    Point { -3, 267.8, 1.35 }, Point { -3, 276.7, 1.55 }, Point { -3, 285.1, 1.75 }, Point { -3, 293.0, 1.95 }, Point { -3, 300.8, 2.15 },
    Point { -3, 308.3, 2.35 }, Point { -3, 315.5, 2.55 }, Point { -3, 322.1, 2.75 }, Point { -3, 328.5, 2.95 }, Point { -3, 335.2, 3.15 },
    Point { -3.5, 234.4, 0.35 }, Point { -3.5, 254.5, 0.55 }, Point { -3.5, 269.6, 0.75 }, Point { -3.5, 282.4, 0.95 }, Point { -3.5, 292.9, 1.15 },
    Point { -3.5, 303.5, 1.35 }, Point { -3.5, 312.2, 1.55 }, Point { -3.5, 320.6, 1.75 }, Point { -3.5, 328.7, 1.95 }, Point { -3.5, 336.8, 2.15 },
    Point { -4, 260.7, 0.35 }, Point { -4, 283.4, 0.55 }, Point { -4, 301.1, 0.75 }, Point { -4, 315.4, 0.95 }, Point { -4, 326.7, 1.15 },
    Point { -4, 336.7, 1.35 }
};

// Published typical operating points from the same sheets, for validation only.
// Zero means the sheet gives no value for that column.
struct Typical { const char* source; double va, vg, ma, mu, gmMs, rpKohm; };
inline constexpr std::array rca12bh7aTypical {
    Typical { "RCA 12BH7-A characteristics", 250, -10.5, 11.5, 16.5, 3.1, 5.3 },
    Typical { "RCA 12BH7-A at -14 V grid", 250, -14, 4, 0, 0, 0 },
    Typical { "RCA 12BH7-A 50 uA cutoff (approx.)", 250, -23, 0.05, 0, 0, 0 }
};
inline constexpr std::array ge5751Typical {
    Typical { "GE 5751 design center", 100, -1, 0.8, 70, 1.2, 58 },
    Typical { "GE 5751 absolute", 250, -3, 1.0, 70, 1.2, 58 }
};
}
