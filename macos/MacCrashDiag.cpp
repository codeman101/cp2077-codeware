// DIAG R14. Prints the fault and the top frames to stderr on a fatal signal. Steam's crash handler and the
// Frida gadget swallow some crashes without writing an .ips, so this is the only record.
//
// R16a. After the report the process must exit. Until R16 the handler restored SIG_DFL and returned, so the
// faulting instruction ran again and took the default action. In the Phase 6 sessions that froze the game: no
// thread logged anything after the report, and the player had to close it from the launcher. The default action hands the
// crash to the process crash reporters (Steam's handler, ReportCrash), which suspend the process while they dump it.
// The report is already on stderr, so the handler now ends the process itself with _exit(128 + signal), no crash
// report. CODEWARE_R16A_CRASHMODE=raise switches back to "SIG_DFL and re-raise" (used once to measure the freeze).
//
// Frames come from the frame-pointer chain (Apple arm64 keeps it). Each address prints as image+offset.
// The handler uses snprintf and write only. It is a diagnostic for a process that is already dying.

#include "MacR13.hpp"
#include "MacR15.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <sys/ucontext.h>
#include <unistd.h>

namespace
{
// 0 = _exit after the report (default), 1 = restore SIG_DFL and re-raise. Set once in InstallCrashDiag.
volatile sig_atomic_t g_raiseMode = 0;

void Emit(const char* aText)
{
    const auto len = std::strlen(aText);
    (void)!write(STDERR_FILENO, aText, len);
}

// Finds the image that holds aAddress. Prints "name+0xoff" into aOut.
void Describe(uintptr_t aAddress, char* aOut, size_t aSize)
{
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (!header)
        {
            continue;
        }
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        auto* cmd = reinterpret_cast<const load_command*>(header + 1);
        for (uint32_t c = 0; c < header->ncmds; ++c)
        {
            if (cmd->cmd == LC_SEGMENT_64)
            {
                const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
                const auto start = static_cast<uintptr_t>(seg->vmaddr) + static_cast<uintptr_t>(slide);
                if (aAddress >= start && aAddress < start + seg->vmsize)
                {
                    const char* name = _dyld_get_image_name(i);
                    const char* base = name ? std::strrchr(name, '/') : nullptr;
                    base = base ? base + 1 : (name ? name : "?");
                    std::snprintf(aOut, aSize, "%s+0x%lx", base,
                                  static_cast<unsigned long>(aAddress - (reinterpret_cast<uintptr_t>(header))));
                    return;
                }
            }
            cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
        }
    }
    std::snprintf(aOut, aSize, "?");
}

void Handler(int aSignal, siginfo_t* aInfo, void* aContext)
{
    // One report per process. A second fault inside the handler goes straight to the default action.
    static volatile sig_atomic_t s_entered = 0;
    if (s_entered)
    {
        std::signal(aSignal, SIG_DFL);
        if (!g_raiseMode)
        {
            _exit(128 + aSignal);
        }
        return;
    }
    s_entered = 1;

    char line[512];
    char where[160];
    auto* uc = static_cast<ucontext_t*>(aContext);
    const auto& ss = uc->uc_mcontext->__ss;
    const uintptr_t pc = static_cast<uintptr_t>(arm_thread_state64_get_pc(ss));
    const uintptr_t lr = static_cast<uintptr_t>(arm_thread_state64_get_lr(ss));
    const uintptr_t fp = static_cast<uintptr_t>(arm_thread_state64_get_fp(ss));
    const uintptr_t sp = static_cast<uintptr_t>(arm_thread_state64_get_sp(ss));

    std::snprintf(line, sizeof(line), "[crash] signal=%d code=%d addr=%p tid=%llu\n", aSignal, aInfo ? aInfo->si_code : -1,
                  aInfo ? aInfo->si_addr : nullptr, static_cast<unsigned long long>(pthread_mach_thread_np(pthread_self())));
    Emit(line);
    Describe(pc, where, sizeof(where));
    std::snprintf(line, sizeof(line), "[crash] #0 pc=0x%lx %s\n", static_cast<unsigned long>(pc), where);
    Emit(line);
    Describe(lr, where, sizeof(where));
    std::snprintf(line, sizeof(line), "[crash] #1 lr=0x%lx %s\n", static_cast<unsigned long>(lr), where);
    Emit(line);

    // Frame-pointer walk. [fp] = previous fp, [fp + 8] = return address. Stay inside this thread's stack.
    const auto self = pthread_self();
    const auto stackTop = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(self));
    const auto stackBottom = stackTop - pthread_get_stacksize_np(self);
    uintptr_t frame = fp;
    for (int depth = 2; depth < 14; ++depth)
    {
        if (frame < stackBottom || frame + 16 > stackTop || (frame & 0xF) != 0)
        {
            break;
        }
        const auto next = *reinterpret_cast<const uintptr_t*>(frame);
        const auto ret = *reinterpret_cast<const uintptr_t*>(frame + 8);
        Describe(ret, where, sizeof(where));
        std::snprintf(line, sizeof(line), "[crash] #%d 0x%lx %s\n", depth, static_cast<unsigned long>(ret), where);
        Emit(line);
        if (next <= frame)
        {
            break;
        }
        frame = next;
    }
    std::snprintf(line, sizeof(line), "[crash] sp=0x%lx x0=0x%llx x1=0x%llx x2=0x%llx x19=0x%llx x20=0x%llx\n",
                  static_cast<unsigned long>(sp), static_cast<unsigned long long>(ss.__x[0]),
                  static_cast<unsigned long long>(ss.__x[1]), static_cast<unsigned long long>(ss.__x[2]),
                  static_cast<unsigned long long>(ss.__x[19]), static_cast<unsigned long long>(ss.__x[20]));
    Emit(line);

    // R17. The script validator keeps the property it was checking; a fault inside the game's validator is otherwise
    // anonymous. Guarded: a second fault in here ends the process through the default action below.
    static volatile sig_atomic_t s_describing = 0;
    if (!s_describing)
    {
        s_describing = 1;
        char state[512];
        state[0] = '\0';
        Mac::DescribeLastValidatedProperty(state, sizeof(state));
        if (state[0])
        {
            std::snprintf(line, sizeof(line), "[crash] last validated property: %s\n", state);
            Emit(line);
        }
    }

    std::signal(aSignal, SIG_DFL);
    if (g_raiseMode)
    {
        // Default action: the crash goes to the crash reporters. This is the path that froze the game.
        std::raise(aSignal);
        return;
    }
    Emit("[crash] exiting now (_exit), no crash report\n");
    _exit(128 + aSignal);
}
}

void Mac::InstallCrashDiag()
{
    {
        const char* mode = std::getenv("CODEWARE_R16A_CRASHMODE");
        g_raiseMode = (mode && std::strcmp(mode, "raise") == 0) ? 1 : 0;
    }
    static char s_altStack[1 << 16];
    stack_t alt{};
    alt.ss_sp = s_altStack;
    alt.ss_size = sizeof(s_altStack);
    alt.ss_flags = 0;
    sigaltstack(&alt, nullptr);

    struct sigaction action{};
    action.sa_sigaction = &Handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE})
    {
        sigaction(sig, &action, nullptr);
    }
}
