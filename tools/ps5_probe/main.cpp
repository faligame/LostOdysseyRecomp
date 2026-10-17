// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include <ps5platform/klog.h>
#include <cstdio>
extern "C" int sceSystemServiceHideSplashScreen();
extern "C" int sceSystemServiceLoadExec(const char*, const char* const*);
extern "C" int sceKernelUsleep(unsigned);

// The native title CRT calls this after main: exit through the shell, not exit().
extern "C" void catchReturnFromMain(int status)
{
    std::fprintf(stderr, "[LO PS5] exit.status=%d\n", status);
    std::fflush(nullptr);
    sceSystemServiceLoadExec("exit", nullptr);
    for (;;) sceKernelUsleep(100000);
}
int main()
{
    ps5_klog_capture_stderr("[LO PS5] ");
    std::fprintf(stderr, "LO PS5 experimental probe; no game assets; not a game port\n");
    sceSystemServiceHideSplashScreen();
    const bool memory = ProbeMemory();
    const bool vulkan = ProbeVulkan();
    std::fprintf(stderr, "probe.memory=%s probe.vulkan=%s gameplay=NOT_IMPLEMENTED\n",
        memory ? "PASS" : "FAIL", vulkan ? "PASS" : "FAIL");
    return memory && vulkan ? 0 : 1;
}
