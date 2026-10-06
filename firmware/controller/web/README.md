# The web page

`index.html` is the page the controller serves at `/`. The build turns it
into `web_index.h` (`tools/web_page.py`), which `src/net/http.c` includes as
the `INDEX_HTML` literal. The page lives in flash and streams from there.

Rules for editing it:

- Keep it ASCII. Use entities in the markup and `\u` escapes in the script.
- Limits that the firmware also checks come from the C headers as
  placeholders: `{{STR(BUDGET_MIN_W)}}` for a number, `{{NET_NTP_DEFAULT}}`
  for a string macro. Never write those values into the page by hand.
- Nothing loads from outside the device: no fonts, libraries or images
  from a CDN. The graphs are inline SVG.
- `conn_t.static_len` is 16-bit, so the built page must stay under 64 KB.
  The build fails if it doesn't. It is about 35 KB today.
- The port lights on the page copy `src/engine/led_pattern.c`. Change both
  together.

The minifier only strips comments, indentation and blank lines, so what you
write is what the browser runs. To try a change, flash a FAKE_BLADES build
and open its page: the simulated blades exercise every port state.
