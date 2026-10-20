"""Plot the offline CSV loops with ReportLab; no plugin or hardware connection."""

import csv
import sys
from pathlib import Path

from reportlab.graphics import renderSVG
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.shapes import Drawing, Line, String
from reportlab.lib.colors import HexColor


def main():
    directory = Path(sys.argv[1])
    with (directory / "loops.csv").open() as stream:
        rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
    groups = {}
    for row in rows:
        groups.setdefault(row["case"], []).append(
            (float(row["field"]), float(row["magnetization"]))
        )

    drawing = Drawing(1100, 500)
    ink = HexColor("#202e38")
    blue, amber, pale = (HexColor(c) for c in ("#147eaa", "#bd641b", "#c3ccd2"))
    drawing.add(String(28, 469, "Experimental magnetic core: modeled loops", fontName="Helvetica-Bold", fontSize=20, fillColor=ink))
    drawing.add(String(28, 445, "16 continuous play operators | memory 0.35 | width 1.0 | dimensionless H and M", fontSize=12, fillColor=ink))
    for x, title, series, colors in (
        (80, "Major loop and memoryless reference", (groups["memoryless"], groups["major"]), (blue, amber)),
        (635, "Nested minor loops and return points", (groups["major"], groups["nested"]), (pale, amber)),
    ):
        drawing.add(String(x, 411, title, fontName="Helvetica-Bold", fontSize=14, fillColor=ink))
        chart = LinePlot()
        chart.x, chart.y, chart.width, chart.height = x, 112, 385, 270
        chart.data = series
        chart.joinedLines = 1
        chart.xValueAxis.valueMin, chart.xValueAxis.valueMax, chart.xValueAxis.valueStep = -3, 3, 1
        chart.yValueAxis.valueMin, chart.yValueAxis.valueMax, chart.yValueAxis.valueStep = -1, 1, 0.5
        for axis in (chart.xValueAxis, chart.yValueAxis):
            axis.labels.fontName = "Helvetica"
            axis.labels.fontSize = 11
            axis.strokeColor = ink
        for i, color in enumerate(colors):
            chart.lines[i].strokeColor = color
            chart.lines[i].strokeWidth = 1.7
        drawing.add(chart)
        drawing.add(String(x + 145, 79, "Normalized field H", fontSize=12, fillColor=ink))
        drawing.add(String(x - 25, 391, "M", fontSize=12, fillColor=ink))
    drawing.add(Line(80, 53, 110, 53, strokeColor=blue, strokeWidth=2))
    drawing.add(String(118, 49, "Memoryless", fontSize=11, fillColor=ink))
    drawing.add(Line(240, 53, 270, 53, strokeColor=amber, strokeWidth=2))
    drawing.add(String(278, 49, "History-dependent prototype", fontSize=11, fillColor=ink))
    drawing.add(String(28, 20, "Offline model only. These are not measured VTB1148 B-H loops or a transformer audio transfer function.", fontSize=11, fillColor=ink))
    output = directory / "loops.svg"
    renderSVG.drawToFile(drawing, str(output))
    print(output)


if __name__ == "__main__":
    main()
