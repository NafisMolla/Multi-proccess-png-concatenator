#include "lab_png.h"

int is_png(U8 *buf) {
    // Implement
    U8 png_bytes[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    for (int i = 0; i < 8; ++i){
        if(buf[i] != png_bytes[i]){
            return 0; //not a png
        }
    }
    return 1;
}

int get_png_height(struct data_IHDR *buf) {
    // Implement
	return buf->height;
}

int get_png_width(struct data_IHDR *buf) {
    // Implement
    return buf->width;
}

int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence) {
    // Implement
    FILE *pic = fopen(fp, "rb");
    char IHDR[100];
    fseek(pic, 8, SEEK_SET); //skip first 8 bytes
    fread(IHDR, 4,1,pic);
    printf("%s\n", IHDR);
    return 0;
}
