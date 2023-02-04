# SRgen (mosaic generator)

SRgen is a tool that allows you to construct an image from another image serving as a pattern.

## Building
To build C version, simply run:
```
clang -Ofast -march=skylake-avx512 src/SR.c -o bin/SR
# or with gcc / MSVC
```

## Usage

### Running C version:
```
./bin/SR --in input_image.bmp --pattern pattern_image.bmp --out output_image.bmp
```
**C version only works with BMP images in BGR24 format!**

### Running with many frames
To run the application over multiple frames, you can specify input/output path as formatted path, i.e.
```
# ffmpeg -i Video.mp4 -pix_fmt bgr24 frames/frame%04d.bmp
./bin/SR --in frames/frame%04d.bmp --pattern VanGogh.bmp --out out/frame%04d.bmp
# ffmpeg -framerate 29.97 -i out/frame%04d.bmp Result.mp4
```

## Example results
Multiple frames

![How to be single](./example/Video.gif)

Input image:

![How to be single frame 3](./example/1.jpg)

Pattern image:

![Prague metro mosaic](./example/2.jpg)

Block size log = 3, transform = rgb3, transforms = 2