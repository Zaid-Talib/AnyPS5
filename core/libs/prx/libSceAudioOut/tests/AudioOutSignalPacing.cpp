#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <thread>
#include <vector>

extern "C" {
int APS5_VABI sceAudioOutOpen(int, int, int, std::uint32_t, std::uint32_t, std::uint32_t);
int APS5_VABI sceAudioOutClose(int);
int APS5_VABI sceAudioOutOutput(int, const void*);
std::uint64_t APS5_VABI sceKernelGetProcessTime();
}

static void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

namespace {

constexpr int user = 0x10000000;
constexpr int portTypeMain = 0;
constexpr std::uint32_t formatS16Mono = 0;
constexpr std::uint32_t frames = 256;
constexpr std::uint32_t frequency = 48000;
constexpr int filledBlocks = 16;
constexpr std::uint64_t queuedAudioUs = filledBlocks * (1000000ULL * frames / frequency);

void Ignore(int) {}

}

int main() {
    Require(setenv("SDL_AUDIODRIVER", "aps5-no-device", 1) == 0, "the audio driver must be overridable");
    struct sigaction action{};
    action.sa_handler = Ignore;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    Require(sigaction(SIGUSR2, &action, nullptr) == 0, "the signal handler must install");

    sceKernelGetProcessTime();
    const std::uint64_t start = sceKernelGetProcessTime();
    const int handle = sceAudioOutOpen(user, portTypeMain, 0, frames, frequency, formatS16Mono);
    Require(handle > 0, "port must open");
    const std::vector<std::int16_t> block(frames, 256);
    for (int index = 0; index < filledBlocks; ++index)
        Require(sceAudioOutOutput(handle, block.data()) == static_cast<int>(frames), "output must accept the block");

    std::atomic<bool> draining{false};
    std::thread drainer([&] {
        draining.store(true);
        Require(sceAudioOutOutput(handle, nullptr) == static_cast<int>(frames), "a null output must wait for the queue");
    });
    while (!draining.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Require(pthread_kill(drainer.native_handle(), SIGUSR2) == 0, "the drainer must be signalled");
    drainer.join();
    Require(sceKernelGetProcessTime() - start >= queuedAudioUs, "a signal must not cut the wait for the queued audio short");
    Require(sceAudioOutClose(handle) == 0, "port must close");
}
