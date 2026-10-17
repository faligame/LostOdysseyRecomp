// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include "../../LostOdysseyRecomp/kernel/guest_address_space_layout.h"
#include <ps5platform/kernel.h>
#include <cstdio>

bool ProbeMemory()
{
    using namespace GuestAddressSpace;
    constexpr size_t bytes = PS5_KERNEL_DIRECT_ALIGNMENT;
    constexpr size_t shift = kOffsets[3] - kOffsets[1];
    std::fprintf(stderr, "memory.page=%zu direct_alignment=%zu E_shift=%zu\n",
        PS5_KERNEL_PAGE_SIZE, bytes, shift);
    // Hint only: never replace a range this probe does not own.
    void* reservation = reinterpret_cast<void*>(0x100000000ull);
    int rc = sceKernelReserveVirtualRange(&reservation, kSize, 0, PS5_KERNEL_PAGE_SIZE);
    std::fprintf(stderr, "memory.reserve4GiB=0x%x address=%p preferred=%d\n", rc,
        reservation, reservation == reinterpret_cast<void*>(0x100000000ull));
    if (rc) return false;
    // An arbitrary base is legal for generated base+guest-address loads, but
    // report it: the current Linux backend insists on the preferred address.
    auto* base = static_cast<unsigned char*>(reservation);
    int64_t physical = 0;
    rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes,
        bytes, PS5_KERNEL_DIRECT_TYPE_CPU, &physical);
    bool ac = false;
    if (!rc) {
        void* a = base + kStarts[1];
        void* c = base + kStarts[2];
        const int prot = PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE;
        int ar = sceKernelMapDirectMemory(&a, bytes, prot, PS5_KERNEL_MAP_FIXED, physical, 0);
        int cr = ar ? ar : sceKernelMapDirectMemory(&c, bytes, prot, PS5_KERNEL_MAP_FIXED, physical, 0);
        if (!ar && !cr && a == base + kStarts[1] && c == base + kStarts[2]) {
            auto* av = static_cast<volatile unsigned char*>(a);
            auto* cv = static_cast<volatile unsigned char*>(c);
            av[0x80] = 0x35;
            ac = cv[0x80] == 0x35;
            cv[0x80] = 0xca;
            ac = ac && av[0x80] == 0xca;
        }
        std::fprintf(stderr, "memory.A_map=0x%x C_map=0x%x alias_AC=%s\n", ar, cr, ac ? "PASS" : "FAIL");
    } else {
        std::fprintf(stderr, "memory.direct_alloc=0x%x\n", rc);
    }
    // Do not round the E offset: that would silently change guest semantics.
    const bool eAligned = shift % PS5_KERNEL_PAGE_SIZE == 0;
    std::fprintf(stderr, "memory.alias_E=%s: guest 4KiB shift vs PS5 16KiB page; no semantic workaround installed\n",
        eAligned ? "UNTESTED" : "UNSUPPORTED");
    int unmap = sceKernelMunmap(reservation, kSize);
    int release = rc ? 0 : sceKernelReleaseDirectMemory(physical, bytes);
    std::fprintf(stderr, "memory.cleanup unmap=0x%x release=0x%x\n", unmap, release);
    // E mapping and full production backing/protection remain unimplemented.
    return false;
}
