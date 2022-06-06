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


#include <efi.h>
#include <efilib.h>
#include <elf.h>
#include <stddef.h>
#include <common/services.h>
#include <config.h>

// 2022 Ian Moffett


#define PSF1_MAGIC0 0x00000036
#define PSF1_MAGIC1 0x00000004

struct FacelessServices fs;
UINTN mmap_key;


// Shutsdown the system.
void shutdown(void) {
    RT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
}

// Returns the number of mmap entries.
uint64_t get_mmap_entries(struct MemoryMap mmap) {
    return mmap.mSize / mmap.mDescriptorSize;
}


// Returns a memory descriptor at a specific index in the memory map.
EFI_MEMORY_DESCRIPTOR* mmap_iterator_helper(uint64_t i, struct MemoryMap mmap) {
    return (EFI_MEMORY_DESCRIPTOR*)((int64_t)mmap.mMap + (i * (mmap.mDescriptorSize)));
}




/*
 *  For fatel errors.
 *
 */

void fatal(void) {
    Print(L"System halted. Upon pressing a key, the system will shutdown.");
    EFI_INPUT_KEY tmp;

    while (ST->ConIn->ReadKeyStroke(ST->ConIn, &tmp) == EFI_NOT_READY);
    shutdown();
}


int memcmp(const void* aptr, const void* bptr, size_t n) {
    const unsigned char* a = aptr, *b = bptr;
    for (size_t i = 0; i < n; i++) {
        if (a[i] < b[i]) return -1;
        else if (a[i] > b[i]) return 1;
    }

    return 0;
}


/*
 *  This sets up Graphics Output Protocol (GOP).
 *
 *  @st: System Table.
 *
 */


void init_gop(EFI_SYSTEM_TABLE* st) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    Print(L"Locating Graphics Output Protocol..\n");
    EFI_STATUS s = uefi_call_wrapper(BS->LocateProtocol, 3, &gop_guid, NULL, (void**)&gop);

    if (EFI_ERROR(s)) {
        Print(L"%s() FAILED!: FAILED TO LOCATE GOP.\n", __func__);
        fatal();
    }

    // Set framebuffer struct's values.
    fs.framebuffer.base_addr = (void*)gop->Mode->FrameBufferBase;
    fs.framebuffer.buffer_size = gop->Mode->FrameBufferSize;
    fs.framebuffer.width = gop->Mode->Info->HorizontalResolution;
    fs.framebuffer.height = gop->Mode->Info->VerticalResolution;
    fs.framebuffer.ppsl = gop->Mode->Info->PixelsPerScanLine;
    
    Print(L"Allocating memory for backbuffer..\n");
    st->BootServices->AllocatePool(EfiLoaderData, fs.framebuffer.buffer_size, (void**)&fs.framebuffer.backbuffer);

    // Dump framebuffer info.
    Print(
            L"FRAMEBUFFER BASE: 0x%X\n"
            L"FRAMEBUFFER SIZE: %d\n"
            L"FRAMEBUFFER WIDTH: %d\n"
            L"FRAMEBUFFER HEIGHT: %d\n"
            "FRAMEBUFFER PIXELS PER SCANLINE: %d\n",
            (uint64_t)fs.framebuffer.base_addr,
            fs.framebuffer.buffer_size,
            fs.framebuffer.width,
            fs.framebuffer.height,
            fs.framebuffer.ppsl
            );
}



/*
 *  @path: Filepath for file in root directory.
 *  @imageHandle: Pass in image handle.
 *  @st: Pass in system table.
 *
 *
 */

EFI_FILE* load_file(CHAR16* path, EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* st) {
    EFI_FILE* res;
    EFI_LOADED_IMAGE_PROTOCOL* loadedImage;

    Print(L"Loading %s.. If the system hangs, you may want to check this file.\n", path);

    Print(L"Fetching loaded image protocol..\n");
    // Get the loaded image protocol.
    st->BootServices->HandleProtocol(imageHandle, &gEfiLoadedImageProtocolGuid, (void**)&loadedImage);
    Print(L"Loaded image protocol fetched!\n");

    // Get SFSP.
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
    Print(L"Fetching EFI_SIMPLE_FILE_SYSTEM_PROTOCOL..\n");
    st->BootServices->HandleProtocol(loadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&fs);
    Print(L"EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fetched!\n");

    // Open volume at root directory.
    EFI_FILE* dir = NULL;
    fs->OpenVolume(fs, &dir);
    Print(L"Volume opened.\n");

    EFI_STATUS s = dir->Open(dir, &res, path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    Print(L"Directory opened.\n");
    Print(L"File fetched: %s\n\n", path);

    if (s != EFI_SUCCESS) {
        Print(L"<s!=EFI_SUCCESS@%d>\n", __LINE__);
        fatal();
    }
    return res;
}


void load_font(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* st) {
    // Load the font.
    EFI_FILE* font = load_file(PSF1_FONT_PATH, imageHandle, st);

    if (!(font)) {
        Print(L"%s() failed: Failed to load font.\n", __func__);
        fatal();
    }

    Print(L"Allocating %d bytes of memory for PSFont header..\n", sizeof(struct PSFontHeader));
    struct PSFontHeader* header;

    // Allocate memory for the header.
    st->BootServices->AllocatePool(EfiLoaderData, sizeof(struct PSFontHeader), (void**)&header);

    // Load font into buffer.
    UINTN size = sizeof(struct PSFontHeader);
    Print(L"Loading %d bytes into memory from font.\n", size);
    font->Read(font, &size, header);

    if (!(header->magic[0] & PSF1_MAGIC0) || !(header->magic[1] & PSF1_MAGIC1)) {
        Print(L"%d() failed!: PSFontHeader magic invalid!", __func__);
        fatal();
    }

    UINTN glyph_buffer_size = header->chsize * 256;

    if (header->mode == 1) {
        glyph_buffer_size = header->chsize * 512;
    }

    // Load font data.
    void* glyph_buf;

    // Set position to end of header.
    font->SetPosition(font, sizeof(struct PSFontHeader));

    // Allocate memory for glyph buffer.
    Print(L"Allocating %d bytes of memory for font glyph buffer..\n");
    st->BootServices->AllocatePool(EfiLoaderData, glyph_buffer_size, (void**)&glyph_buf);
    Print(L"Loading font data into memory..\n");
    font->Read(font, &glyph_buffer_size, glyph_buf);

    struct PSFont* fontres;
    st->BootServices->AllocatePool(EfiLoaderData, sizeof(struct PSFont), (void**)&fontres);
    fontres->header = header;
    fontres->glyph_buf = glyph_buf;
    fs.psfont = fontres;


}


// Places a character on the screen.
void putChar(uint32_t color, char chr, unsigned int xOff, unsigned int yOff, uint32_t* framebuffer) {
    char* fontPtr = fs.psfont->glyph_buf + (chr * fs.psfont->header->chsize);
    for (unsigned long y = yOff; y < yOff + 16; y++){
        for (unsigned long x = xOff; x < xOff+8; x++){
        if ((*fontPtr & (0b10000000 >> (x - xOff))) > 0){
            *(unsigned int*)(framebuffer + x + (y * fs.framebuffer.ppsl)) = color;
        }

        }
        fontPtr++;
    }
}


void load_all_bmps(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* sysTable) {
    for (int i = 0; i < MAX_BMP_IMPORTS; ++i) {
        EFI_FILE* bmp_file = load_file(bmp_imports[i], imageHandle, sysTable);

        // This tmp will be to get the file size.
        struct BMP tmp;
        UINTN tmp_readsz = sizeof(tmp.header);

        Print(L"Reading BMP header into memory..\n");
        bmp_file->Read(bmp_file, &tmp_readsz, &tmp.header);

        Print(L"Checking BMP header signature..\n");
        if ((tmp.header.signature & 0xFF) != 'B' || (tmp.header.signature >> 8) != 'M') {
            Print(L"BMP header signature invalid!\n");
            fatal();
        }

        Print(L"BMP signature is valid!\n");

        struct BMP* bmp = NULL;
        
        // Allocate memory for the entire BMP.
        Print(L"Allocating %d needed for BMP.\n", tmp.header.file_size);
        UINTN bmp_sz = tmp.header.file_size;
        EFI_STATUS s = sysTable->BootServices->AllocatePool(EfiLoaderData, bmp_sz, (void**)&bmp);

        if (bmp == NULL || s != EFI_SUCCESS) {
            Print(L"Failed to load one of the BMPs: ALLOC_POOL_FAILED\n");
            fatal();
        }

        // Load that memory chunk.
        Print(L"Loading allocated memory with BMP.\n");
        bmp_file->Read(bmp_file, &bmp_sz, bmp);
        Print(L"Finished loading BMP into memory! Filling FacelessServices slot..\n");

        // Fill BMPS slot.
        fs.bmps[i] = bmp;
        Print(L"Slot filled!\n");
    }
}


/*
 *  Fetches the Root System Description Pointer.
 *  
 *  @st: System Table.
 *
 */

void* get_rsdp(EFI_SYSTEM_TABLE* st) {
    EFI_CONFIGURATION_TABLE* config_table = st->ConfigurationTable;
    EFI_GUID rsdp_guid = ACPI_20_TABLE_GUID;
    void* rsdp = NULL;

    for (UINTN i = 0; i < st->NumberOfTableEntries; ++i) {
        if (CompareGuid(&config_table[i].VendorGuid, &rsdp_guid)) {
            if (memcmp((char*)"RSD PTR ", (char*)config_table->VendorTable, 8) == 0) {
                rsdp = (void*)config_table->VendorTable;
                break;
            }
        }

        ++config_table;
    }

    return rsdp;
}


// Sets stuff for runtime.
void setup_services(EFI_SYSTEM_TABLE* sysTable) {
    fs.power.shutdown = shutdown;

    // Setup memory map.
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINTN map_size, descriptor_size;
    UINT32 descriptor_version;

    Print(L"Fetching memory map..\n");
    sysTable->BootServices->GetMemoryMap(&map_size, map, &mmap_key, &descriptor_size, &descriptor_version);
    sysTable->BootServices->AllocatePool(EfiLoaderData, map_size, (void**)&map);
    sysTable->BootServices->GetMemoryMap(&map_size, map, &mmap_key, &descriptor_size, &descriptor_version);
    EFI_STATUS s = sysTable->BootServices->GetMemoryMap(&map_size, map, &mmap_key, &descriptor_size, &descriptor_version);

    if (s != EFI_SUCCESS) { 
        Print(L"FATAL: GetMemoryMap returned a non-zero value. <bug@%d>\n", __LINE__);
        fatal();
    }

    Print(L"Memory map fetched.\n");

    // Set the Faceless Services table entries.
    fs.mmap.mMap = map;
    fs.mmap.mSize = map_size;
    fs.mmap.mDescriptorSize = descriptor_size;

    // Setup memory map services.
    fs.mmap_get_entries = get_mmap_entries;
    fs.mmap_iterator_helper = mmap_iterator_helper;
    fs.framebuf_putch = putChar;
    
    Print(L"Fetching Root System Description Pointer..\n");
    fs.rsdp = get_rsdp(sysTable);
}


// Greets the user.
void greet(void) {
    EFI_TIME time;
    RT->GetTime(&time, NULL);
    Print(
            L"Welcome, Friend. Today Is: %d/%d/%d %d:%d:%d\n",
            time.Month,
            time.Day,
            time.Year,
            time.Hour,
            time.Minute,
            time.Second);

    Print(L"Press any key to boot.\n");

    EFI_INPUT_KEY tmp;
    while (ST->ConIn->ReadKeyStroke(ST->ConIn, &tmp) == EFI_NOT_READY);
}


void boot(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* st) {
    // Get the kernel file.
    EFI_FILE* kernel = load_file(L"kernel.elf", image_handle, st);

    Elf64_Ehdr header;
    UINTN file_info_size;

    // Get kernel info.
    kernel->GetInfo(kernel, &gEfiFileInfoGuid, &file_info_size, NULL);

    // Read kernel header into memory.
    Print(L"Reading in kernel ELF header.\n");
    UINTN size = sizeof(header);
    kernel->Read(kernel, &size, &header);

    Print(L"Verifying kernel ELF header..\n");
    if (memcmp(&header.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0 ||
            header.e_ident[EI_CLASS] != ELFCLASS64 || 
            header.e_type != ET_EXEC ||
            header.e_machine != EM_X86_64 ||
            header.e_version != EV_CURRENT) {
        // -------------------------------

        Print(L"Kernel ELF header bad!\n");
        fatal();
    }

    Print(L"Kernel ELF header verified!\n");

    // Setup program header(s).
    Elf64_Phdr* program_headers;
    kernel->SetPosition(kernel, header.e_phoff);

    // This is basically saying we added header.e_phoff to the file
    // pointer base.
    Print(L"Kernel FP_BASE_OFFSET => header.e_phoff\n");

    
    UINTN program_header_size = header.e_phnum * header.e_phentsize;
    // Allocate memory for program header(s).
    st->BootServices->AllocatePool(EfiLoaderData, program_header_size, (void**)&program_headers);
    Print(L"Memory allocated for program headers.\n");
    
    // Read in the program header(s)!
    kernel->Read(kernel, &program_header_size, program_headers);

    // Set everything up now!

    for (Elf64_Phdr* phdr = program_headers; (char*)phdr < (char*)program_headers + header.e_phnum * header.e_phentsize; phdr = (Elf64_Phdr*)((char*)phdr + header.e_phentsize)) {
        if (phdr->p_type == PT_LOAD) {
            int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
            Print(L"phdr->p_type == PT_LOAD\n");
            Elf64_Addr segment = phdr->p_paddr;
            Print(L"Segment fetched from program headers.\n");
            st->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);
            Print(L"Allocated some pages for ELF load segment.\n");
            kernel->SetPosition(kernel, phdr->p_offset);
            Print(L"FP offset set to program offset.\n");
            UINTN size = phdr->p_filesz;
            kernel->Read(kernel, &size, (void*)segment);
            Print(L"Program read into memory.\n");
        }
    }

    // Reset Console-Out (i.e clearing buffer).
    st->ConOut->Reset(st->ConOut, 1);
    void(*kernel_entry)(struct FacelessServices*) = ((__attribute__((sysv_abi))void(*)(struct FacelessServices*))header.e_entry);

    // Exit boot-services.
    st->BootServices->ExitBootServices(image_handle, mmap_key);

    // Call kernel.
    kernel_entry(&fs);
}



// Entry point.
EFI_STATUS efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* sysTable) {
    InitializeLib(imageHandle, sysTable);

    // Greet the user, always be nice! :)
    greet();

    // Setup services..
    setup_services(sysTable);

    // Load a runtime font.
    load_font(imageHandle, sysTable);

    // Set GOP.
    init_gop(sysTable);

    // Load all BMPs.
    load_all_bmps(imageHandle, sysTable);

    // Finally, boot.
    boot(imageHandle, sysTable);

    __asm__ __volatile__("cli; hlt");
    return 0;
}
