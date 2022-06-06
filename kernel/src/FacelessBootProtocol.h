/*
 *  MIT License
 *
 *  Copyright (c) 2022 FacelessSociety, Ian Marco Moffett
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */


#ifndef FACELESS_BOOT_PROTOCOL_H
#define FACELESS_BOOT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define MAX_BMP_IMPORTS 1

typedef enum {
    MMAP_RESERVED,
    MMAP_EFI_LOADER_CODE,
    MMAP_EFI_LOADER_DATA,
    MMAP_EFI_BOOTSERVICES_CODE,
    MMAP_EFI_BOOTSERVICES_DATA,
    MMAP_EFI_RUNTIME_SERVICES_CODE,
    MMAP_EFI_RUNTIME_SERVICES_DATA,
    MMAP_USABLE_MEMORY,
    MMAP_UNUSABLE_MEMORY,
    MMAP_ACPI_RECLAIM_MEMORY,
    MMAP_ACPI_MEMORY_NVS,
    MMAP_MEMORY_MAPPED_IO,
    MMAP_MEMORY_MAPPED_IO_PORT_SPACE,
    MMAP_EFI_PAL_CODE,
} mem_type_t;


struct __attribute__((packed)) FacelessMemoryDescriptor {
    uint32_t type;
    void* physAddr;
    void* virtAddr;
    uint64_t nPages;
    uint64_t attr;
};


// BMP structure.
struct __attribute__((packed)) BMP {
    struct __attribute__((packed)) Header {
        uint16_t signature;                     // Magic bytes ('BM').
        uint32_t file_size;                     // Size of file in bytes.
        uint16_t reserved;                      // Reserved.
        uint16_t reserved1;                      // Reserved.
        uint32_t data_offset;                   // Offset from beginning of file to beginning of file.
    } header;

    struct __attribute__((packed)) InfoHeader {
        uint32_t info_hdr_sz;                       // 40.
        uint32_t width;                             // Width.
        uint32_t height;                            // Height.
        uint16_t nplanes;                           // Number of planes.
        uint16_t bits_per_pixel;                    // Color depth.
        uint32_t compression;
        uint32_t image_size;
        uint32_t xpixels_per_meter;
        uint32_t ypixels_per_meter;
        uint32_t colors_used;
        uint32_t important_colors;
    } info_header;

    struct __attribute__((packed)) ColorTable {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t reserved;
    } color_table;

    uint32_t row_size;
    uint32_t array_size;
    char pixel_data[];
};


struct __attribute__((packed)) PSFontHeader {
    unsigned char magic[2];                         // Magic bytes.
    unsigned char mode;                             // 256/512.
    unsigned char chsize;
};


struct __attribute__((packed)) PSFont {
    struct PSFontHeader* header;
    void* glyph_buf;
};


struct FacelessServices { 
    struct PowerManagement {
        void(*shutdown)(void);
    } power;

    struct __attribute__((packed)) MemoryMap {
        struct FacelessMemoryDescriptor* mmap;
        uint64_t mSize;
        uint64_t mDescriptorSize;
    } mmap;

    struct Framebuffer {
        void* base_addr;
        size_t buffer_size;
        unsigned int width;
        unsigned int height;
        unsigned int ppsl;
        uint32_t* backbuffer;
    } framebuffer;

    struct PSFont* psfont;
    struct BMP* bmps[MAX_BMP_IMPORTS];
    void* rsdp;
    uint64_t(*mmap_get_entries)(struct MemoryMap mmap);
    struct FacelessMemoryDescriptor*(*mmap_iterator_helper)(uint64_t i, struct MemoryMap mmap);
    void(*framebuf_putch)(uint32_t color, char chr, unsigned int xOff, unsigned int yOff, uint32_t* framebuffer);
};

#endif
