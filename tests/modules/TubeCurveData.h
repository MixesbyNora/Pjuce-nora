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
// GE 5670, ET-T1083A (Aug 1956), p5 upper: average plate characteristics, each
// section. 4 V and 0.5 mA per grid square; read along current rows. Grid 0 to -10 V
// (positive-grid curves and the dashed grid-current lines are excluded).
inline constexpr std::array ge5670 {
    Point { 0, 23.7, 0.75 }, Point { 0, 35.7, 1.75 }, Point { 0, 44.8, 2.75 }, Point { 0, 52.6, 3.75 }, Point { 0, 60.1, 4.75 },
    Point { 0, 67.4, 5.75 }, Point { 0, 73.3, 6.75 }, Point { 0, 80.0, 7.75 }, Point { 0, 85.5, 8.75 }, Point { 0, 91.5, 9.75 },
    Point { 0, 97.0, 10.75 }, Point { 0, 102.6, 11.75 }, Point { 0, 107.4, 12.75 }, Point { 0, 112.8, 13.75 }, Point { 0, 123.2, 15.75 },
    Point { 0, 127.7, 16.75 }, Point { 0, 132.6, 17.75 }, Point { 0, 137.5, 18.75 }, Point { 0, 142.3, 19.75 }, Point { -1, 52.9, 0.75 },
    Point { -1, 67.1, 1.75 }, Point { -1, 76.6, 2.75 }, Point { -1, 85.0, 3.75 }, Point { -1, 92.6, 4.75 }, Point { -1, 106.7, 6.75 },
    Point { -1, 113.2, 7.75 }, Point { -1, 119.5, 8.75 }, Point { -1, 125.1, 9.75 }, Point { -1, 131.4, 10.75 }, Point { -1, 136.7, 11.75 },
    Point { -1, 142.3, 12.75 }, Point { -1, 147.7, 13.75 }, Point { -1, 152.9, 14.75 }, Point { -1, 158.4, 15.75 }, Point { -1, 168.6, 17.75 },
    Point { -1, 173.6, 18.75 }, Point { -2, 80.3, 0.75 }, Point { -2, 98.5, 1.75 }, Point { -2, 109.8, 2.75 }, Point { -2, 119.3, 3.75 },
    Point { -2, 127.7, 4.75 }, Point { -2, 142.6, 6.75 }, Point { -2, 149.3, 7.75 }, Point { -2, 156.0, 8.75 }, Point { -2, 162.3, 9.75 },
    Point { -2, 168.0, 10.75 }, Point { -2, 173.3, 11.75 }, Point { -2, 179.4, 12.75 }, Point { -2, 184.5, 13.75 }, Point { -2, 189.2, 14.75 },
    Point { -2, 195.1, 15.75 }, Point { -2, 199.9, 16.75 }, Point { -2, 204.6, 17.75 }, Point { -3, 107.5, 0.75 }, Point { -3, 126.7, 1.75 },
    Point { -3, 139.5, 2.75 }, Point { -3, 149.4, 3.75 }, Point { -3, 159.1, 4.75 }, Point { -3, 175.4, 6.75 }, Point { -3, 182.5, 7.75 },
    Point { -3, 189.1, 8.75 }, Point { -3, 195.8, 9.75 }, Point { -3, 201.9, 10.75 }, Point { -3, 207.9, 11.75 }, Point { -3, 213.4, 12.75 },
    Point { -3, 219.4, 13.75 }, Point { -3, 224.6, 14.75 }, Point { -3, 230.0, 15.75 }, Point { -4, 134.0, 0.75 }, Point { -4, 154.5, 1.75 },
    Point { -4, 168.1, 2.75 }, Point { -4, 179.2, 3.75 }, Point { -4, 188.9, 4.75 }, Point { -4, 205.8, 6.75 }, Point { -4, 213.8, 7.75 },
    Point { -4, 221.0, 8.75 }, Point { -4, 228.4, 9.75 }, Point { -4, 235.3, 10.75 }, Point { -4, 241.0, 11.75 }, Point { -4, 247.3, 12.75 },
    Point { -4, 253.0, 13.75 }, Point { -4, 258.9, 14.75 }, Point { -5, 157.5, 0.75 }, Point { -5, 180.6, 1.75 }, Point { -5, 195.6, 2.75 },
    Point { -5, 207.3, 3.75 }, Point { -5, 217.7, 4.75 }, Point { -5, 227.6, 5.75 }, Point { -5, 236.1, 6.75 }, Point { -5, 244.3, 7.75 },
    Point { -5, 251.6, 8.75 }, Point { -5, 259.2, 9.75 }, Point { -5, 265.2, 10.75 }, Point { -5, 271.9, 11.75 }, Point { -5, 278.2, 12.75 },
    Point { -6, 180.1, 0.75 }, Point { -6, 206.9, 1.75 }, Point { -6, 224.0, 2.75 }, Point { -6, 236.2, 3.75 }, Point { -6, 247.3, 4.75 },
    Point { -6, 256.5, 5.75 }, Point { -6, 265.3, 6.75 }, Point { -6, 273.5, 7.75 }, Point { -6, 281.5, 8.75 }, Point { -6, 289.1, 9.75 },
    Point { -6, 296.4, 10.75 }, Point { -6, 303.4, 11.75 }, Point { -7, 204.5, 0.75 }, Point { -7, 231.9, 1.75 }, Point { -7, 249.2, 2.75 },
    Point { -7, 262.0, 3.75 }, Point { -7, 273.7, 4.75 }, Point { -7, 284.2, 5.75 }, Point { -7, 293.4, 6.75 }, Point { -7, 302.4, 7.75 },
    Point { -7, 311.1, 8.75 }, Point { -7, 319.1, 9.75 }, Point { -7, 325.9, 10.75 }, Point { -8, 228.7, 0.75 }, Point { -8, 255.7, 1.75 },
    Point { -8, 274.5, 2.75 }, Point { -8, 287.6, 3.75 }, Point { -8, 299.7, 4.75 }, Point { -8, 309.8, 5.75 }, Point { -8, 319.9, 6.75 },
    Point { -8, 329.1, 7.75 }, Point { -8, 337.3, 8.75 }, Point { -8, 345.8, 9.75 }, Point { -8, 353.7, 10.75 }, Point { -9, 253.8, 0.75 },
    Point { -9, 281.3, 1.75 }, Point { -9, 298.7, 2.75 }, Point { -9, 312.6, 3.75 }, Point { -9, 325.0, 4.75 }, Point { -9, 336.1, 5.75 },
    Point { -9, 345.8, 6.75 }, Point { -9, 355.7, 7.75 }, Point { -9, 364.3, 8.75 }, Point { -9, 372.8, 9.75 }, Point { -10, 278.6, 0.75 },
    Point { -10, 306.2, 1.75 }, Point { -10, 323.5, 2.75 }, Point { -10, 337.1, 3.75 }, Point { -10, 348.6, 4.75 }, Point { -10, 359.4, 5.75 },
    Point { -10, 369.3, 6.75 }, Point { -10, 379.2, 7.75 }, Point { -10, 387.4, 8.75 }
};
// GE 5670 p5 lower: average transfer characteristics, each section (Eb 100-300 V).
// 0.5 V and 1 mA per square; read along grid-voltage columns (current error).
inline constexpr std::array ge5670Transfer {
    Point { -12.75, 300, 0.16 }, Point { -12.25, 300, 0.36 }, Point { -11.75, 300, 0.56 }, Point { -11.25, 300, 0.83 }, Point { -10.75, 300, 1.08 },
    Point { -10.25, 300, 1.48 }, Point { -9.75, 300, 1.98 }, Point { -9.25, 300, 2.62 }, Point { -8.75, 300, 3.4 }, Point { -8.25, 300, 4.36 },
    Point { -7.75, 300, 5.48 }, Point { -7.25, 300, 6.81 }, Point { -6.75, 300, 8.43 }, Point { -6.25, 300, 10.38 }, Point { -5.75, 300, 12.65 },
    Point { -5.25, 300, 15.1 }, Point { -4.75, 300, 17.8 }, Point { -10.75, 250, 0.11 }, Point { -10.25, 250, 0.26 }, Point { -9.75, 250, 0.43 },
    Point { -9.25, 250, 0.68 }, Point { -8.75, 250, 1.0 }, Point { -8.25, 250, 1.41 }, Point { -7.75, 250, 1.98 }, Point { -7.25, 250, 2.65 },
    Point { -6.75, 250, 3.48 }, Point { -6.25, 250, 4.6 }, Point { -5.75, 250, 5.96 }, Point { -5.25, 250, 7.69 }, Point { -4.75, 250, 9.79 },
    Point { -4.25, 250, 12.24 }, Point { -3.75, 250, 15.02 }, Point { -3.25, 250, 17.94 }, Point { -8.75, 200, 0.12 }, Point { -8.25, 200, 0.29 },
    Point { -7.75, 200, 0.47 }, Point { -7.25, 200, 0.68 }, Point { -6.75, 200, 0.97 }, Point { -6.25, 200, 1.38 }, Point { -5.75, 200, 1.97 },
    Point { -5.25, 200, 2.82 }, Point { -4.75, 200, 3.94 }, Point { -4.25, 200, 5.31 }, Point { -3.75, 200, 7.11 }, Point { -3.25, 200, 9.35 },
    Point { -2.75, 200, 12.01 }, Point { -2.25, 200, 14.98 }, Point { -1.75, 200, 18.3 }, Point { -6.25, 150, 0.16 }, Point { -5.75, 150, 0.3 },
    Point { -5.25, 150, 0.49 }, Point { -4.75, 150, 0.8 }, Point { -4.25, 150, 1.26 }, Point { -3.75, 150, 2.01 }, Point { -3.25, 150, 3.1 },
    Point { -2.75, 150, 4.69 }, Point { -2.25, 150, 6.87 }, Point { -1.75, 150, 9.44 }, Point { -1.25, 150, 12.45 }, Point { -0.75, 150, 15.67 },
    Point { -0.25, 150, 19.23 }, Point { -3.75, 100, 0.18 }, Point { -3.25, 100, 0.41 }, Point { -2.75, 100, 0.81 }, Point { -2.25, 100, 1.5 },
    Point { -1.75, 100, 2.77 }, Point { -1.25, 100, 4.75 }, Point { -0.75, 100, 7.18 }, Point { -0.25, 100, 10.04 }
};
// GE 6386, ET-T1113 (Aug 1954), p4 lower: average transfer characteristics, each
// section (Eb 100-300 V), the remote-cutoff tail to -55 V. 2 V and 2 mA per square;
// read along grid-voltage columns. (The p3/p4 upper charts are cascode: not used.)
inline constexpr std::array ge6386Transfer {
    Point { -55, 300, 1.51 }, Point { -53, 300, 1.76 }, Point { -51, 300, 2.06 }, Point { -49, 300, 2.35 }, Point { -47, 300, 2.65 },
    Point { -45, 300, 3.0 }, Point { -43, 300, 3.38 }, Point { -41, 300, 3.78 }, Point { -39, 300, 4.26 }, Point { -37, 300, 4.77 },
    Point { -35, 300, 5.31 }, Point { -33, 300, 5.96 }, Point { -31, 300, 6.65 }, Point { -29, 300, 7.43 }, Point { -27, 300, 8.29 },
    Point { -25, 300, 9.21 }, Point { -23, 300, 10.29 }, Point { -21, 300, 11.59 }, Point { -19, 300, 13.22 }, Point { -17, 300, 15.41 },
    Point { -15, 300, 18.22 }, Point { -13, 300, 22.08 }, Point { -11, 300, 27.71 }, Point { -9, 300, 36.04 }, Point { -55, 250, 0.34 },
    Point { -51, 250, 0.63 }, Point { -49, 250, 0.82 }, Point { -47, 250, 1.03 }, Point { -45, 250, 1.28 }, Point { -43, 250, 1.51 },
    Point { -41, 250, 1.78 }, Point { -39, 250, 2.08 }, Point { -37, 250, 2.43 }, Point { -35, 250, 2.79 }, Point { -33, 250, 3.19 },
    Point { -31, 250, 3.67 }, Point { -29, 250, 4.14 }, Point { -27, 250, 4.81 }, Point { -25, 250, 5.46 }, Point { -23, 250, 6.17 },
    Point { -21, 250, 7.07 }, Point { -19, 250, 8.08 }, Point { -17, 250, 9.34 }, Point { -15, 250, 10.96 }, Point { -13, 250, 13.27 },
    Point { -11, 250, 16.69 }, Point { -9, 250, 22.21 }, Point { -7, 250, 30.52 }, Point { -43, 200, 0.36 }, Point { -39, 200, 0.67 },
    Point { -37, 200, 0.86 }, Point { -35, 200, 1.11 }, Point { -33, 200, 1.36 }, Point { -31, 200, 1.6 }, Point { -29, 200, 1.93 },
    Point { -27, 200, 2.25 }, Point { -25, 200, 2.64 }, Point { -23, 200, 3.09 }, Point { -21, 200, 3.59 }, Point { -19, 200, 4.16 },
    Point { -17, 200, 5.0 }, Point { -15, 200, 5.98 }, Point { -13, 200, 7.18 }, Point { -11, 200, 8.96 }, Point { -9, 200, 11.88 },
    Point { -7, 200, 16.35 }, Point { -5, 200, 24.58 }, Point { -3, 200, 36.42 }, Point { -29, 150, 0.42 }, Point { -27, 150, 0.61 },
    Point { -25, 150, 0.84 }, Point { -23, 150, 1.11 }, Point { -21, 150, 1.39 }, Point { -19, 150, 1.74 }, Point { -17, 150, 2.12 },
    Point { -15, 150, 2.56 }, Point { -13, 150, 3.17 }, Point { -11, 150, 4.03 }, Point { -9, 150, 5.25 }, Point { -7, 150, 7.12 },
    Point { -5, 150, 10.87 }, Point { -3, 150, 18.93 }, Point { -1, 150, 30.6 }, Point { -19, 100, 0.31 }, Point { -17, 100, 0.48 },
    Point { -15, 100, 0.65 }, Point { -13, 100, 0.95 }, Point { -11, 100, 1.32 }, Point { -9, 100, 1.8 }, Point { -7, 100, 2.44 },
    Point { -5, 100, 3.55 }, Point { -3, 100, 6.34 }, Point { -1, 100, 13.33 }
};
inline constexpr std::array ge5670Typical { Typical { "GE 5670 class A1 (Rk 240 ohm)", 150, -1.968, 8.2, 35, 5.5, 6.4 } };
inline constexpr std::array ge6386Typical { Typical { "GE 6386 class A1 (Rk 200 ohm)", 100, -1.92, 9.6, 17, 4.0, 4.25 } };
// GE 6386 p2: "Grid voltage, approximate, Gm = 100 micromhos: -16 V" at the class A1
// plate voltage (100 V). The remote-cutoff anchor for deep gain reduction.
inline constexpr double ge6386GridFor100uS = -16.0, ge6386PlateFor100uS = 100.0;
}
