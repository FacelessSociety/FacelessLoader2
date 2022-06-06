#include <FacelessBootProtocol.h>

void _start(struct FacelessServices* _services) {
    __asm__ __volatile__("cli; hlt");
}
