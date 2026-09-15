# Open Runde text font

`OpenRunde-Medium.otf` is from
[`lauridskern/open-runde`](https://github.com/lauridskern/open-runde) commit
`3e7ed7f3cdfa5523766db7e430066472615fc935`. Open Runde is a rounded variant
of Inter. `OFL.txt` contains its SIL Open Font License 1.1.

The generated LVGL bitmap files are in `../../../romfs/system/font/`. They use
4-bit antialiasing, no bitmap compression, and fast-format kerning. Spark loads
each size on first use and keeps it in SRAM: 14 px for body, reply, and grid
cells; 12 px for hints and note dates; 16 px for note titles and list previews;
18 px for the page title, detail lead, list titles, and grid headings. The
vendor TTF font is rasterized at run time and is much slower, so Spark uses it
only as the fallback for glyphs the bitmaps lack.

Generate the files from this directory:

```sh
for size in 12 14 16 18; do
  npm exec --yes --package=lv_font_conv@1.5.3 -- lv_font_conv \
    --font OpenRunde-Medium.otf \
    --size "$size" --bpp 4 --format bin --no-compress \
    --force-fast-kern-format \
    -r 0x20-0x024F,0x2000-0x206F,0x20AC,0x2122,0x2190-0x21FF \
    -o "../../../romfs/system/font/open_runde_${size}.bin"
done
```

The bitmap file covers Latin text, common punctuation, and available arrows.
Missing glyphs use the existing system font.
