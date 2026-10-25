# Photos

Drop images here (JPEG, PNG, BMP, GIF, WebP or TIFF) and rebuild. Each one is converted at build time by `tools/convert_photos.py`:

- scaled to 384 px wide (the printer's width)
- cropped to 768 px tall if longer, keeping the middle
- tone-adjusted and dithered to black and white

Each photo takes about 48 bytes per row, so a 384×512 photo is 24 KB. Around 100 photos fit in the Pico 2's 4 MB flash.

To check what they'll look like before printing:

```sh
python3 tools/convert_photos.py photos --preview preview/
```

`sample.png` is a generated test image. Delete it once you've added your own.
