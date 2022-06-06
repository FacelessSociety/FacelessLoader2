#ifndef CONFIG_H
#define CONFIG_H

#include <efi.h>


#define MAX_BMP_IMPORTS 1


static CHAR16* bmp_imports[MAX_BMP_IMPORTS] = {
    L"kess.bmp"
};


#define PSF1_FONT_PATH L"zap-light16.psf"


#endif
