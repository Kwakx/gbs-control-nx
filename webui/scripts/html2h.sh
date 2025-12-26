#!/usr/bin/env bash

cd ..
gzip -c9 build/webui.html > build/webui_html && xxd -i build/webui_html > build/webui_html.h && rm build/webui_html && sed -i -e 's/unsigned char webui_html\[]/const uint8_t webui_html[] PROGMEM/' build/webui_html.h && sed -i -e 's/unsigned int webui_html_len/const unsigned int webui_html_len/' build/webui_html.h
rm -fv build/webui_html.h-e

echo "webui_html.h GENERATED in webui/build/";