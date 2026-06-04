#!/usr/bin/env python3
"""Generate firmware/esp12e_dashboard/page_html.h from live.html (the source of truth).
The ESP-12E serves the page from PROGMEM; this keeps the .ino in sync with the tested
HTML. Run after editing live.html:  python tools/embed_page.py
"""
import os

D = os.path.join(os.path.dirname(__file__), "..", "firmware", "esp12e_dashboard")
with open(os.path.join(D, "live.html"), "r", encoding="utf-8") as f:
    html = f.read()
assert ')HTML"' not in html, 'live.html contains the raw-string delimiter )HTML" -- change the delimiter'

out = ("// AUTO-GENERATED from live.html by tools/embed_page.py -- do not edit by hand.\n"
       '#ifndef PAGE_HTML_H\n#define PAGE_HTML_H\n'
       'const char PAGE[] PROGMEM = R"HTML(\n' + html + '\n)HTML";\n'
       '#endif\n')
with open(os.path.join(D, "page_html.h"), "w", encoding="utf-8") as f:
    f.write(out)
print(f"wrote page_html.h ({len(html)} bytes of HTML)")
