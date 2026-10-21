"""Plot captured paired CPU CSVs with the existing ReportLab chart workflow."""
import csv
import math
import sys
from pathlib import Path

import render_magnetic_validation_report as charts
from reportlab.graphics.shapes import Line, String
from reportlab.lib.colors import HexColor


def main(root):
    rows = list(csv.DictReader((root / 'paired_timing.csv').open()))
    assert len(rows) == 24
    for row in rows:
        assert row['checksum_identical'] == '1' and row['solver_failures'] == '0'
        assert all(math.isfinite(float(value)) for key, value in row.items() if key != 'workload')
    charts.COLORS = [HexColor('#697889'), HexColor('#168071')]
    drawing = charts.canvas(1200, 480, 'Offline transformer: paired stereo Release CPU',
                            '16x FIR unchanged. Median of seven runs; includes stimulus generation. Percentage of one core audio-time budget.')
    for panel, name in enumerate(('low_tone', 'multitone', 'transient')):
        data = sorted((r for r in rows if r['workload'] == name and r['channels'] == '2'),
                      key=lambda r: float(r['sample_rate_hz']))
        series = [[(float(r['sample_rate_hz']) / 1000, float(r[key])) for r in data]
                  for key in ('reference_realtime_percent', 'candidate_realtime_percent')]
        x, y, width, height = 65 + panel * 390, 130, 300, 230
        charts.add_chart(drawing, series, x, y, width, height, name.replace('_', ' '),
                         x_range=(40, 200), y_range=(0, 360))
        line_y = y + height * 100 / 360
        drawing.add(Line(x, line_y, x + width, line_y, strokeColor=HexColor('#a74b51'), strokeDashArray=[3, 3]))
        drawing.add(String(x + 4, line_y + 5, '100% budget', fontSize=8, fillColor=HexColor('#a74b51')))
        drawing.add(String(x + 110, y - 35, 'Sample rate (kHz)', fontSize=10, fillColor=charts.INK))
        at48 = next(r for r in data if r['sample_rate_hz'] == '48000')
        label = f"48 kHz: {float(at48['reference_realtime_percent']):.1f}% -> {float(at48['candidate_realtime_percent']):.1f}%"
        drawing.add(String(x, 55, label, fontSize=12, fillColor=charts.INK))
    for x, label, color in ((65, 'Frozen v1', charts.COLORS[0]), (220, 'Optimized', charts.COLORS[1])):
        drawing.add(Line(x, 88, x + 24, 88, strokeColor=color, strokeWidth=2))
        drawing.add(String(x + 32, 84, label, fontSize=10, fillColor=charts.INK))
    drawing.add(String(65, 24, 'Offline only: median results are not worst-case deadlines or a live-readiness guarantee.',
                       fontSize=10, fillColor=charts.INK))
    charts.save(drawing, root, 'cpu')


if __name__ == '__main__':
    main(Path(sys.argv[1]))
