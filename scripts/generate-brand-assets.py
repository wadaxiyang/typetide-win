#!/usr/bin/env python3
"""Compile checked-in SVG geometry to GDI+ paths; Python standard library only.

Run windows/scripts/make-icon.ps1 afterward to export the transparent ICO.
Supported SVG subset: absolute M/L/C/Z paths and circles, no transforms.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "windows/assets"


def number(value):
    return f"{float(value):.6g}" + ("f" if "." in f"{float(value):.6g}" else ".f")


def geometry(element):
    lines = []
    for node in element.iter():
        tag = node.tag.split('}')[-1]
        if tag == 'circle':
            x, y, r = (float(node.attrib[k]) for k in ('cx', 'cy', 'r'))
            lines += ['p.StartFigure();', f'p.AddEllipse({number(x-r)}, {number(y-r)}, {number(2*r)}, {number(2*r)});', 'p.CloseFigure();']
        elif tag == 'path':
            tokens = re.findall(r'[A-Za-z]|-?\d+(?:\.\d+)?', node.attrib['d'])
            x = y = 0
            while tokens:
                command = tokens.pop(0)
                count = {'M': 2, 'L': 2, 'C': 6, 'Z': 0}[command]
                args = tokens[:count]
                del tokens[:count]
                if command == 'M':
                    lines.append('p.StartFigure();')
                elif command == 'L':
                    lines.append('p.AddLine(' + ', '.join(map(number, [x, y] + args)) + ');')
                elif command == 'C':
                    lines.append('p.AddBezier(' + ', '.join(map(number, [x, y] + args)) + ');')
                else:
                    lines.append('p.CloseFigure();')
                if count:
                    x, y = args[-2:]
    return '\n'.join('    ' + line for line in lines)


def main() -> None:
    brand = ET.parse(ASSETS / 'brand.svg').getroot()
    nav = ET.parse(ASSETS / 'navigation.svg').getroot()
    output = '// Generated from windows/assets/*.svg. Run scripts/generate-brand-assets.py.\n#pragma once\n#include <gdiplus.h>\nnamespace svgpaths {\n'
    output += 'inline void Brand(Gdiplus::GraphicsPath& p) {\n' + geometry(brand) + '\n}\n'
    output += 'inline void Navigation(Gdiplus::GraphicsPath& p, int index) {\n    switch (index) {\n'
    for index, symbol in enumerate(nav):
        output += f'    case {index}: // {symbol.attrib["id"]}\n' + geometry(symbol) + '\n        break;\n'
    output += '    }\n}\n} // namespace svgpaths\n'
    (ROOT / 'windows/src/SvgPaths.generated.h').write_text(output, encoding='utf-8', newline='\n')
    (ROOT / 'docs/branding/typetide.svg').write_bytes((ASSETS / 'brand.svg').read_bytes())


if __name__ == "__main__":
    main()
