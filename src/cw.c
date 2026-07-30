#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_ARGS 40
#define ERROR_FILE 41
#define ERROR_BMP 42

#pragma pack(push, 1)

typedef struct {
    unsigned short signature;
    unsigned int filesize;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int pixelArrOffset;
} BitmapFileHeader;

typedef struct {
    unsigned int headerSize;
    unsigned int width;
    unsigned int height;
    unsigned short planes;
    unsigned short bitsPerPixel;
    unsigned int compression;
    unsigned int imageSize;
    unsigned int xPixelsPerMeter;
    unsigned int yPixelsPerMeter;
    unsigned int colorsInColorTable;
    unsigned int importantColorCount;
} BitmapInfoHeader;

#pragma pack(pop)

typedef struct {
    unsigned char b;
    unsigned char g;
    unsigned char r;
} Rgb;

typedef struct {
    int do_circle;
    int do_mirror;
    int do_copy;
    int center_x, center_y;
    int radius;
    int thickness;
    Rgb line_color;
    int fill;
    Rgb fill_color;
    char axis;
    int left_x, left_y;
    int right_x, right_y;
    int dst_x, dst_y;
} Options;

static void print_bmp_info(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return;
    }

    BitmapFileHeader bmfh;
    BitmapInfoHeader bmif;

    fread(&bmfh, 1, sizeof(BitmapFileHeader), f);
    if (bmfh.signature != 0x4D42) {
        fprintf(stderr, "Error: not a BMP file\n");
        fclose(f);
        return;
    }

    fread(&bmif, 1, sizeof(BitmapInfoHeader), f);
    
    int abs_height = (int)bmif.height;
    if (abs_height < 0) abs_height = -abs_height;

    printf("=== BMP Info ===\n");
    printf("File: %s\n", filename);
    printf("Width: %u px\n", bmif.width);
    printf("Height: %d px (absolute: %d)\n", (int)bmif.height, abs_height);
    printf("Bits per pixel: %u\n", bmif.bitsPerPixel);
    printf("Compression: %u\n", bmif.compression);
    printf("Image size: %u bytes\n", bmif.imageSize);

    fclose(f);
}

static Rgb **read_bmp(const char *file_name, BitmapFileHeader *bmfh, BitmapInfoHeader *bmif) {
    FILE *f = fopen(file_name, "rb");
    if (!f) return NULL;

    fread(bmfh, 1, sizeof(BitmapFileHeader), f);
    fread(bmif, 1, sizeof(BitmapInfoHeader), f);

    if (bmfh->signature != 0x4D42 || bmif->bitsPerPixel != 24 || bmif->compression != 0) {
        fclose(f);
        return NULL;
    }

    fseek(f, bmfh->pixelArrOffset, SEEK_SET);

    unsigned int H = abs((int)bmif->height);
    unsigned int W = bmif->width;
    unsigned int padding = (4 - (W * sizeof(Rgb)) % 4) % 4;

    Rgb **arr = malloc(H * sizeof(Rgb *));
    for (unsigned int i = 0; i < H; i++) {
        arr[i] = malloc(W * sizeof(Rgb));
        fread(arr[i], sizeof(Rgb), W, f);
        if (padding > 0) {
            fseek(f, padding, SEEK_CUR);
        }
    }
    fclose(f);

    if ((int)bmif->height > 0) {
        for (unsigned int i = 0; i < H / 2; i++) {
            Rgb *tmp = arr[i];
            arr[i] = arr[H - 1 - i];
            arr[H - 1 - i] = tmp;
        }
    }
    
    bmif->height = H;
    return arr;
}

static void write_bmp(const char *file_name, Rgb **arr, unsigned int H, unsigned int W, BitmapFileHeader bmfh, BitmapInfoHeader bmif) {
    FILE *ff = fopen(file_name, "wb");
    if (!ff) return;

    unsigned int padding = (4 - (W * sizeof(Rgb)) % 4) % 4;
    bmif.imageSize = H * (W * sizeof(Rgb) + padding);
    bmfh.filesize = bmfh.pixelArrOffset + bmif.imageSize;

    fwrite(&bmfh, 1, sizeof(BitmapFileHeader), ff);
    fwrite(&bmif, 1, sizeof(BitmapInfoHeader), ff);

    for (int i = H - 1; i >= 0; i--) {
        fwrite(arr[i], sizeof(Rgb), W, ff);
        if (padding > 0) {
            unsigned char pad[3] = {0, 0, 0};
            fwrite(pad, 1, padding, ff);
        }
    }
    fclose(ff);
}

static void free_bmp(Rgb **arr, unsigned int H) {
    for (unsigned int i = 0; i < H; i++) {
        free(arr[i]);
    }
    free(arr);
}

static void draw_circle(Rgb **arr, unsigned int H, unsigned int W, Options opts) {
    int r_outer = opts.radius + opts.thickness / 2;
    int r_inner = opts.radius - opts.thickness / 2;
    if (r_inner < 0) r_inner = 0;

    int r_outer_sq = r_outer * r_outer;
    int r_inner_sq = r_inner * r_inner;

    for (int y = opts.center_y - r_outer; y <= opts.center_y + r_outer; y++) {
        for (int x = opts.center_x - r_outer; x <= opts.center_x + r_outer; x++) {
            if (x >= 0 && x < (int)W && y >= 0 && y < (int)H) {
                int dx = x - opts.center_x;
                int dy = y - opts.center_y;
                int dist_sq = dx * dx + dy * dy;

                if (dist_sq <= r_outer_sq && dist_sq >= r_inner_sq) {
                    arr[y][x] = opts.line_color;
                } else if (opts.fill && dist_sq < r_inner_sq) {
                    arr[y][x] = opts.fill_color;
                }
            }
        }
    }
}

static void mirror_region(Rgb **arr, unsigned int H, unsigned int W, Options opts) {
    int lx = opts.left_x, rx = opts.right_x;
    int ly = opts.left_y, ry = opts.right_y;

    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (rx > (int)W) rx = W;
    if (ry > (int)H) ry = H;
    if (lx >= rx || ly >= ry) return;

    int width = rx - lx;
    int height = ry - ly;

    if (opts.axis == 'x') {
        for (int y = ly; y < ry; y++) {
            for (int x = 0; x < width / 2; x++) {
                Rgb tmp = arr[y][lx + x];
                arr[y][lx + x] = arr[y][rx - 1 - x];
                arr[y][rx - 1 - x] = tmp;
            }
        }
    } else {
        for (int x = lx; x < rx; x++) {
            for (int y = 0; y < height / 2; y++) {
                Rgb tmp = arr[ly + y][x];
                arr[ly + y][x] = arr[ry - 1 - y][x];
                arr[ry - 1 - y][x] = tmp;
            }
        }
    }
}

static void copy_region(Rgb **arr, unsigned int H, unsigned int W, Options opts) {
    int lx = opts.left_x, rx = opts.right_x;
    int ly = opts.left_y, ry = opts.right_y;

    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (rx > (int)W) rx = W;
    if (ry > (int)H) ry = H;
    if (lx >= rx || ly >= ry) return;

    int w = rx - lx;
    int h = ry - ly;

    Rgb *buffer = malloc(w * h * sizeof(Rgb));
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            buffer[y * w + x] = arr[ly + y][lx + x];
        }
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dest_x = opts.dst_x + x;
            int dest_y = opts.dst_y + y;
            if (dest_x >= 0 && dest_x < (int)W && dest_y >= 0 && dest_y < (int)H) {
                arr[dest_y][dest_x] = buffer[y * w + x];
            }
        }
    }
    
    free(buffer);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s -i input.bmp -o output.bmp [options]\n", argv[0]);
        return ERROR_ARGS;
    }

    Options opts = {0};
    opts.thickness = 1;
    opts.axis = 'x';
    const char *input_file = NULL;
    const char *output_file = "out.bmp";

    static struct option long_opts[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"circle", no_argument, 0, 'c'},
        {"mirror", no_argument, 0, 'm'},
        {"copy", no_argument, 0, 'C'},
        {"center", required_argument, 0, 'e'},
        {"radius", required_argument, 0, 'r'},
        {"thickness", required_argument, 0, 't'},
        {"axis", required_argument, 0, 'a'},
        {"left_up", required_argument, 0, 'u'},
        {"right_down", required_argument, 0, 'd'},
        {"dest_left_up", required_argument, 0, 'D'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:o:cmCe:r:t:a:u:d:D:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'i': input_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 'c': opts.do_circle = 1; break;
            case 'm': opts.do_mirror = 1; break;
            case 'C': opts.do_copy = 1; break;
            case 'e': sscanf(optarg, "%d.%d", &opts.center_x, &opts.center_y); break;
            case 'r': opts.radius = atoi(optarg); break;
            case 't': opts.thickness = atoi(optarg); break;
            case 'a': opts.axis = optarg[0]; break;
            case 'u': sscanf(optarg, "%d.%d", &opts.left_x, &opts.left_y); break;
            case 'd': sscanf(optarg, "%d.%d", &opts.right_x, &opts.right_y); break;
            case 'D': sscanf(optarg, "%d.%d", &opts.dst_x, &opts.dst_y); break;
        }
    }

    if (!input_file) return ERROR_ARGS;

    BitmapFileHeader bmfh;
    BitmapInfoHeader bmif;
    
    Rgb **arr = read_bmp(input_file, &bmfh, &bmif);
    if (!arr) return ERROR_BMP;

    if (opts.do_circle) draw_circle(arr, bmif.height, bmif.width, opts);
    if (opts.do_mirror) mirror_region(arr, bmif.height, bmif.width, opts);
    if (opts.do_copy) copy_region(arr, bmif.height, bmif.width, opts);

    write_bmp(output_file, arr, bmif.height, bmif.width, bmfh, bmif);
    free_bmp(arr, bmif.height);

    return 0;
}
