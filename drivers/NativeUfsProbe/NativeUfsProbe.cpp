// SPDX-License-Identifier: BSD-2-Clause-Patent
// Minimal MSM8998 UFS warm-link GPT/LUN probe for OnePlus 5 (cheeseburger).
//
// The controller is left enabled by the firmware.  This probe follows the
// proven DinnerABC/UniXNU-cheeseburger P8/P11 UFS path: attach as an
// IOPlatformDevice after the device tree is published, use a private
// physically-contiguous UTRL/UCD/data allocation, install that queue, send a
// NOP OUT first, then query LUN capacity and read LUN0's GPT with SCSI READ(10).
// It never reuses firmware descriptors, never programs an SMMU, and has no
// SCSI write/format/unmap path.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <sys/types.h>
#include <sys/param.h>
#include <kern/debug.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <libkern/libkern.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>

static volatile uint64_t gUfsCommands;
static volatile uint64_t gUfsInFlight;
static volatile uint64_t gUfsLastLba;
static volatile uint64_t gUfsLastBytes;
static volatile uint64_t gUfsLastLoops;
static volatile uint64_t gUfsLastTicks;
static bool gNopActive;

#define UFS_TIMEOUT_TICKS 100000000ULL   // about 5.2 s at the 19.2 MHz timebase
static volatile uint64_t gWindowRequests;
static volatile uint64_t gWindowBytes;
static volatile uint64_t gWindowInFlight;
static volatile uint64_t gWindowLastBlock;
static volatile uint64_t gWindowLastCount;
static volatile uint64_t gWindowLastReqBlock;
static volatile uint64_t gWindowLastReqCount;
static volatile uint64_t gWindowStartTicks;
static volatile uint32_t gH103LogMode;
static volatile uint64_t gH103LogFirst;
static volatile uint64_t gH103RootFirst;
static volatile uint64_t gH103RootLast;
static uint64_t gH103HeartbeatStart;
static volatile int gH103SkipPattern;
static uint32_t gH103SlotCursor;
static volatile int gH103Quiet;
static volatile uint64_t gH103LastSlotTicks;
static volatile int gH103SlotBusy;
static volatile int gH103LastSlotMsgX = -1;
// H103fl diagnostics: 0=no slot writer, 1=default cadence, 2=2 s fast control.
static volatile int gH103SlotMode = 1;
// H106: ios-sweet 0069's batching. The flush runs at most once per interval,
// writes only the message bytes produced since the previous flush, and writes
// nothing while the kernel is quiet. h103-log-secs sets the interval (1..60).
static volatile uint32_t gH103LogSecs = 10;
// A 1 Hz work-loop timer drives the display heartbeat cell independently of
// UFS I/O, so a frozen cell means a real kernel/work-loop hang.
static volatile int gH103HbTimerOn;
static volatile uint64_t gH103StartTicks;
static volatile uint32_t gH103SlotWriteErrors;
// H103fm: even when the message buffer is unchanged, rewrite the header every
// two seconds from the next completed read.  uptime_s then proves whether the
// read path was still alive after the last kernel log line.
static volatile uint64_t gH103LastHeaderTicks;
static volatile uint32_t gH103LastMsgBytes;
// timer_call lives in osfmk and is a C symbol. Declare it with C linkage here
// instead of including kern/timer_call.h, whose XNU declarations are C++ in
// this translation unit and would produce mangled names.
struct timer_call;
typedef struct timer_call *timer_call_t;
typedef void *timer_call_param_t;
typedef void (*timer_call_func_t) (timer_call_param_t, timer_call_param_t);
extern "C" {
timer_call_t timer_call_alloc (timer_call_func_t func, timer_call_param_t param0);
boolean_t timer_call_enter (timer_call_t call, uint64_t deadline, uint32_t flags);
boolean_t timer_call_cancel (timer_call_t call);
void timer_call_free (timer_call_t call);
}
static timer_call_t gH103TimerCall;
static volatile uint32_t gH103TimerFires;
static void h103TimerCallFunc (timer_call_param_t, timer_call_param_t);
// H103fn: post-root IOGPUFamily load probe. The display archive's A540 stub
// cannot be called before user space is up (A1 panicked); this only tests
// whether the kext can be loaded after SpringBoard exec, without touching the
// display archive.
extern "C" kern_return_t OSKextLoadKextWithIdentifier (const char *);
extern "C" volatile uint32_t native_boot_flags;
static volatile int gH103LoadIogpu;
static volatile int gH103IogpuTried;
static volatile uint32_t gH103IogpuRc = 0xffffffffU;
static void h103MaybeLoadIogpu (void);
// H104e: publish the real prelinked IOAcceleratorES/IOGPU class after
// backboardd has exec'd. This does not register a local OSMetaClass.
extern "C" int native_iogpu_accel_probe (void);
static volatile int gH103IogpuProbeTried;
static volatile int gH103IogpuProbeRc = -1;
static void h103MaybePublishIogpu (void);
extern "C" {
volatile uint32_t native_h103_slot_state = 0;
}
class NativeUfsProbe;
static NativeUfsProbe *gH103Probe;
static void h103SlotWriteThunk (void);
static void h103HeaderWriteThunk (void);
static void h103FlushNow (void);

// H103cg's probe periodically redrew the GOP test pattern from its heartbeat.
// The display archive is force-loaded into the kernel and exports this symbol;
// call it directly like the original code (a weak import can bind to null).
extern "C" int native_legacy_test_pattern(void);

// H104i: after backboardd is up, ask the XNU proc-diag helper to dump the
// display-side thread states. The helper lives in the kernel because the
// driver's public headers keep struct task/thread incomplete.
extern "C" void native_proc_state_dump(void);
// H124: the full per-process wait graph is only printed at the later stages;
// the +2 s snapshot proved the ipc_mqueue_full block there is a startup
// transient, so the extra ~70 lines are not worth the console time early on.
extern "C" void native_proc_wait_graph(void);

// H108: independent heartbeat. ios-sweet's log thread sleeps on tsleep, not
// IOSleep; this port never woke from IOSleep, so this thread first proves
// whether tsleep wakes here. When it does, it flips only the 16x16 heartbeat
// cell (no full 2x2 redraw), runs any due dump and flushes that dump, so the
// diagnostics no longer depend on UFS read traffic.
// H109: tsleep does not wake either (only "NATIVE_TICK: thread rc=0" appears).
// The heartbeat and the dump wakeup now come from a timer_call, which runs
// from the timer interrupt and needs no thread timer at all.
extern "C" void native_legacy_heartbeat_cell(void);
// H115: display-side event counters and the first-frame content scan. The
// client frame is scanned (nonblack count and bounding box) while the CPU copy
// runs, so a black screen can be attributed to a black client frame.
extern "C" volatile uint32_t native_legacy_client_calls;
extern "C" volatile uint32_t native_legacy_transfer_calls;
extern "C" volatile uint32_t native_legacy_frame_count;
extern "C" volatile uint32_t native_legacy_frame_kind[32];
extern "C" volatile uint64_t native_legacy_frame_stats[32][5];

// H111: ArmCpu counts every INTID 27 (virtual timer) it takes before calling
// rtclock_intr(). A zero count after minutes of uptime means the timer
// interrupt itself never reaches the kernel, which would explain IOSleep,
// tsleep and timer_call all failing to wake anything.
extern "C" volatile uint64_t armcpu_timer_interrupts;
// H127b: which candidate timer PPI (19 or 27) actually reaches EL1, plus the
// storm guard's last masked INTID.
extern "C" volatile uint64_t armcpu_timer19_interrupts;
extern "C" volatile uint64_t armcpu_timer27_interrupts;
extern "C" volatile uint32_t armcpu_irq_storm_id;
extern "C" volatile uint64_t armcpu_irq_storm_count;
extern "C" volatile uint32_t armcpu_timer_dispatch;
extern "C" volatile uint64_t armcpu_timer_rearm_count;
extern "C" volatile uint64_t armcpu_ipi_interrupts;
// H114: ArmCpu export that reads the boot CPU's redistributor PPI 27 state.
extern "C" void armcpu_probe_ppi27 (void);
extern "C" volatile uint64_t armcpu_ppi_probe[13];

static int gH109Chan;
static volatile uint32_t gH109Ticks;
static timer_call_t gH109TimerCall;
// H117: h114-gic=0 keeps the H114 redistributor/ICC reads out of the A/B run.
// H118/H120: the GIC values are cached at init; no runtime GIC/ICC reads.
static volatile uint32_t gH114GicProbe = 1;
// H110: h109-tick=0 keeps the timer_call off so the same kernel can be booted
// with and without the interrupt heartbeat.
static volatile uint32_t gH110TickEnabled = 1;
static volatile uint32_t gH110WorkerWakes;

// H105: this thread never wakes from IOSleep on this kernel. No H103/H104
// capture contains a single NATIVE_UFS_ALIVE line after NATIVE_UFS_HB: enter,
// so the H104i/H104j dump placed inside the heartbeat loop below could never
// run. Drive the dump from the UFS completion and read paths instead: they are
// proven alive (NATIVE_UFS_PROGRESS). Three snapshots follow the gate, so the
// state just after exec and the later stall are both recorded.
// H107: the gate also opens 60 s after SpringBoard exec when backboardd never
// runs, so a green boot leaves evidence too.
static volatile uint64_t gH105GateTicks;
static volatile uint64_t gH107SpringBoardTicks;
static volatile int gH105DumpStage;

static bool
h107GateOpen (void)
{
  if (native_boot_flags & 4) {
    return true;
  }
  if (!(native_boot_flags & 8)) {
    return false;
  }
  uint64_t now = mach_absolute_time ();
  if (!gH107SpringBoardTicks) {
    gH107SpringBoardTicks = now;
    return false;
  }
  // The launchd ordering race (~200-230 s) is decided long before this.
  return now - gH107SpringBoardTicks >= 60 * 19200000ULL;
}

static bool
h105DumpTick (void)
{
  if (gH105DumpStage >= 3 || !h107GateOpen ()) {
    return false;
  }
  uint64_t now = mach_absolute_time ();
  if (!gH105GateTicks) {
    gH105GateTicks = now;
    return false;
  }
  uint64_t elapsed = now - gH105GateTicks;
  unsigned stage = 0;
  if (gH105DumpStage == 0 && elapsed >= 2 * 19200000ULL) {
    stage = 1;
  } else if (gH105DumpStage == 1 && elapsed >= 12 * 19200000ULL) {
    stage = 2;
  } else if (gH105DumpStage == 2 && elapsed >= 60 * 19200000ULL) {
    stage = 3;
  }
  if (!stage) {
    return false;
  }
  gH105DumpStage = stage;
  IOLog ("NATIVE_PROC_DUMP: trigger stage=%u flags=%x cmds=%llu elapsed_ms=%llu%s\n",
         stage, native_boot_flags, (unsigned long long) gUfsCommands,
         (unsigned long long) (elapsed / 19200),
         (native_boot_flags & 4) ? "" : " fallback");
  native_proc_state_dump ();
  if (stage >= 2) {
    native_proc_wait_graph ();
  }
  return true;
}

// H103ea keeps H103bd's 10-second heartbeat byte-for-byte: the ALIVE console
// line is written first, then the 2x2 panel. H103bd was built exactly this way
// and is the only confirmed 2x2 image; changing the interval or dropping the
// ALIVE line was not proven safe.
static void
heartbeatThread (void *, wait_result_t)
{
  gH103HeartbeatStart = mach_absolute_time ();
  IOLog ("NATIVE_UFS_HB: enter\n");
  for (;;) {
    IOSleep (10000);
    IOLog ("NATIVE_UFS_ALIVE: winFlight=%llu ufsFlight=%llu cmds=%llu "
           "winReq=%llu winBytes=%llu lastCmd=%llx/%llu req=%llx/%llu\n",
           (unsigned long long) gWindowInFlight,
           (unsigned long long) gUfsInFlight,
           (unsigned long long) gUfsCommands,
           (unsigned long long) gWindowRequests,
           (unsigned long long) gWindowBytes,
           (unsigned long long) gUfsLastLba,
           (unsigned long long) gUfsLastBytes,
           (unsigned long long) gWindowLastReqBlock,
           (unsigned long long) gWindowLastReqCount);
    // H105: the process-state dump lives in the UFS completion path below.
    // Nothing may depend on this thread waking; IOSleep never returns here.
    if (!gH103SkipPattern && !gH103HbTimerOn && !(native_boot_flags & 4)) {
      native_legacy_test_pattern ();
    }
    uint64_t start = gWindowStartTicks;
    if (gWindowInFlight && start && mach_absolute_time () - start > 10 * 19200000ULL) {
      panic ("NATIVE_UFS_STALL: req=%llx/%llu cmds=%llu winBytes=%llu "
             "lastCmd=%llx/%llu ufsFlight=%llu",
             (unsigned long long) gWindowLastReqBlock,
             (unsigned long long) gWindowLastReqCount,
             (unsigned long long) gUfsCommands,
             (unsigned long long) gWindowBytes,
             (unsigned long long) gUfsLastLba,
             (unsigned long long) gUfsLastBytes,
             (unsigned long long) gUfsInFlight);
    }
  }
}

// H103fm: timer_call runs from the timer interrupt, unlike IOTimerEventSource
// and IOSleep which depend on a kernel thread wakeup. It is only a diagnostic:
// h103-hb-timer=2 arms it and the 3rd cell then blinks from interrupt context.
static void
h103TimerCallFunc (timer_call_param_t, timer_call_param_t)
{
  gH103TimerFires = gH103TimerFires + 1;
  if (!gH103SkipPattern) {
    native_legacy_test_pattern ();
  }
  if (gH103TimerCall) {
    timer_call_enter (gH103TimerCall, mach_absolute_time () + 19200000ULL, 0);
  }
}

// H103fn: only trigger after SpringBoard has exec'd, i.e. after user space and
// kextd/driverkitd are running. Never call this pre-root.
static void
h103MaybeLoadIogpu (void)
{
  if (!gH103LoadIogpu || gH103IogpuTried) {
    return;
  }
  if (!(native_boot_flags & 8)) {
    return;
  }
  gH103IogpuTried = 1;
  IOLog ("NATIVE_IOGPU_LOAD: begin flags=%x\n", native_boot_flags);
  kern_return_t kr = OSKextLoadKextWithIdentifier ("com.apple.iokit.IOGPUFamily");
  gH103IogpuRc = static_cast<uint32_t> (kr);
  IOLog ("NATIVE_IOGPU_LOAD: rc=%x flags=%x\n", gH103IogpuRc,
         native_boot_flags);
}

static void
h103MaybePublishIogpu (void)
{
  if (gH103IogpuProbeTried || !(native_boot_flags & 4)) {
    return;
  }
  gH103IogpuProbeTried = 1;
  gH103IogpuProbeRc = native_iogpu_accel_probe ();
  IOLog ("NATIVE_IOGPU_PUBLISH: rc=%d flags=%x\n", gH103IogpuProbeRc,
         native_boot_flags);
}

// H103ea: the 2-second slot is a separate thread. H103bd's heartbeat is left
// at its proven 10-second cadence, so the persistent log cannot perturb the
// 2x2 redraw order that H103bd uses.
static void
h103SlotThread (void *, wait_result_t)
{
  if (!gH103SlotMode) {
    return;
  }
  native_h103_slot_state = 3;
  for (;;) {
    IOSleep (2000);
    if (native_boot_flags & 4) {
      h103SlotWriteThunk ();
    }
  }
}

#define MSM8998_UFS_HC_BASE 0x1da4000ULL
#define MSM8998_UFS_HC_SIZE 0x2500ULL
#define MSM8998_GCC_BASE 0x100000ULL
#define MSM8998_GCC_SIZE 0xb0000ULL

#define UFS_RSP_OFFSET 512U
#define UFS_PRDT_OFFSET 1024U
#define UFS_UCD_OFFSET 0x100U
#define UFS_FW_UCD_SIZE 0xF00U
#define UFS_FW_DATA_OFFSET 0x800U
#define UFS_FW_DATA_MAX 0x700U
#define UFS_DATA_MAX 0x20000U

// H103 persistent log snapshot: SHLOG001 v3 header + 1 MiB kernel msgbuf in the
// last 32 MiB of userdata_ios. Written once after root; the boot log ring holds
// the early NATIVE_LEGACY lines. TWRP reads the window back read-only.
#define H103_LOG_BLOCKS 8192U
#define H103_LOG_MSG_BLOCKS 256U

struct H103LogHeader {
  char magic[8];
  uint32_t version, header_bytes;
  uint64_t boot_id;
  uint64_t first_lba;
  uint32_t window_blocks, msg_first, msg_blocks, msg_bytes;
  uint32_t slot_first, slot_blocks, slot_count, slots_written;
  uint32_t uptime_s, rounds, write_errors, gpt_open, gpt_last, state;
  char tag[32];
  uint32_t export_first, export_blocks, export_bytes, export_generation;
  uint32_t export_below;
};

struct H103MsgBuf {
  int msg_magic, msg_size, msg_bufx, msg_bufr;
  char *msg_bufc;
};
extern "C" struct H103MsgBuf *msgbufp;

static uint64_t
h103LogTicks (void)
{
  uint64_t value = 0;
  __asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (value));
  return value;
}

static uint32_t
h103CopyMsgbuf (uint8_t *out)
{
  memset (out, 0, H103_LOG_MSG_BLOCKS * 4096U);
  struct H103MsgBuf *m = msgbufp;
  if (!m || !m->msg_bufc || m->msg_size <= 0) {
    return 0;
  }
  int size = m->msg_size, x = m->msg_bufx;
  if (x < 0 || x >= size) {
    x = 0;
  }
  uint32_t n = 0;
  for (int i = 0; i < size && n < H103_LOG_MSG_BLOCKS * 4096U; ++i) {
    char c = m->msg_bufc[(x + i) % size];
    if (c) {
      out[n++] = static_cast<uint8_t> (c);
    }
  }
  return n;
}

static uint32_t
h103CopyMsgbufTail (uint8_t *out, uint32_t cap)
{
  memset (out, 0, cap);
  struct H103MsgBuf *m = msgbufp;
  if (!m || !m->msg_bufc || m->msg_size <= 0) {
    return 0;
  }
  int size = m->msg_size, x = m->msg_bufx;
  if (x < 0 || x >= size) {
    x = 0;
  }
  uint32_t n = (cap < static_cast<uint32_t> (size)) ? cap : static_cast<uint32_t> (size);
  uint32_t start = (static_cast<uint32_t> (x) + static_cast<uint32_t> (size) - n) %
                   static_cast<uint32_t> (size);
  for (uint32_t i = 0; i < n; ++i) {
    out[i] = static_cast<uint8_t> (m->msg_bufc[(start + i) % static_cast<uint32_t> (size)]);
  }
  return n;
}

#define UFS_REG_IS 0x20U
#define UFS_REG_IE 0x24U
#define UFS_REG_HCS 0x30U
#define UFS_REG_HCE 0x34U
#define UFS_REG_UTRLBA 0x50U
#define UFS_REG_UTRLBAU 0x54U
#define UFS_REG_UTRLDBR 0x58U
#define UFS_REG_UTRLRSR 0x60U
#define UFS_REG_TASK_DOORBELL 0x78U
#define UFS_REG_AHIT 0x18U
#define UFS_REG_UICCMD 0x90U
#define UFS_REG_UICARG1 0x94U
#define UFS_REG_UICARG2 0x98U
#define UFS_REG_UICARG3 0x9cU
#define UFS_IS_UCCS 0x400U
#define UFS_IS_UEE 0x2U
#define UFS_UIC_DME_GET 0x01U
#define UFS_UIC_DME_HIBERNATE_EXIT 0x18U

#define UFS_FATAL_STATUS 0x71880U

struct UfsQueue {
  uintptr_t hc;
  uint8_t *list;
  uint8_t *ucd;
  uint8_t *data;
  uint64_t ucdPhys;
  uint64_t dataPhys;
  uint32_t dataCapacity;
  bool poisoned;
};

class NativeUfsProbe;

class NativeUfsLun : public IOBlockStorageDevice {
  OSDeclareDefaultStructors (NativeUfsLun)
  NativeUfsProbe *host = nullptr;
  uint8_t lun = 0;
  uint64_t last = 0;
  uint32_t sector = 0;

public:
  bool configure (NativeUfsProbe *parent, uint8_t unit, uint64_t lastLba, uint32_t blockSize);
  IOReturn doEjectMedia () override { return kIOReturnUnsupported; }
  IOReturn doFormatMedia (UInt64) override { return kIOReturnNotWritable; }
  UInt32 doGetFormatCapacities (UInt64 *, UInt32) const override { return 0; }
  char *getVendorString () override { return const_cast<char *> ("Qualcomm"); }
  char *getProductString () override { return const_cast<char *> ("MSM8998 UFS read-only"); }
  char *getRevisionString () override { return const_cast<char *> ("B5"); }
  char *getAdditionalDeviceInfoString () override { return const_cast<char *> ("NativeUfsProbe warm-link LUN"); }
  IOReturn reportBlockSize (UInt64 *value) override { *value = sector; return kIOReturnSuccess; }
  IOReturn reportMaxValidBlock (UInt64 *value) override { *value = last; return kIOReturnSuccess; }
  IOReturn reportMediaState (bool *present, bool *changed) override {
    *present = true;
    if (changed) {
      *changed = false;
    }
    return kIOReturnSuccess;
  }
  IOReturn reportRemovability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportEjectability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportWriteProtection (bool *value) override { *value = true; return kIOReturnSuccess; }
  IOReturn doSynchronizeCache () override { return kIOReturnNotWritable; }
  IOReturn doSynchronize (UInt64, UInt64, IOOptionBits) override { return kIOReturnNotWritable; }
  IOReturn getWriteCacheState (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn setWriteCacheState (bool) override { return kIOReturnNotWritable; }
  IOReturn doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                             IOStorageAttributes *attributes,
                             IOStorageCompletion *completion) override;
};

class NativeUfsWindow : public IOBlockStorageDevice {
  OSDeclareDefaultStructors (NativeUfsWindow)
  NativeUfsProbe *host = nullptr;
  uint64_t absolute = 0;
  uint64_t bytes = 0;
  uint8_t *bounce = nullptr;
  uint8_t *readAhead = nullptr;
  uint64_t readAheadLba = 0;
  uint64_t readAheadBytes = 0;
  IOLock *lock = nullptr;
  char label[32] = {};
  bool online = false;

public:
  bool configure (NativeUfsProbe *parent, const char *name, uint64_t absoluteByte,
                  uint64_t length);
  bool start (IOService *) override { online = true; return true; }
  void free () override {
    if (readAhead) { IOFree (readAhead, UFS_DATA_MAX); readAhead = nullptr; }
    IOBlockStorageDevice::free ();
  }
  IOReturn doEjectMedia () override { return kIOReturnUnsupported; }
  IOReturn doFormatMedia (UInt64) override { return kIOReturnNotWritable; }
  UInt32 doGetFormatCapacities (UInt64 *, UInt32) const override { return 0; }
  char *getVendorString () override { return const_cast<char *> ("Native"); }
  char *getProductString () override { return label; }
  char *getRevisionString () override { return const_cast<char *> ("B5"); }
  char *getAdditionalDeviceInfoString () override { return const_cast<char *> ("SHNGROOT read-only window"); }
  IOReturn reportBlockSize (UInt64 *value) override { *value = 512; return kIOReturnSuccess; }
  IOReturn reportMaxValidBlock (UInt64 *value) override { *value = bytes / 512 - 1; return kIOReturnSuccess; }
  IOReturn reportMediaState (bool *present, bool *changed) override {
    *present = online;
    if (changed) {
      *changed = false;
    }
    return kIOReturnSuccess;
  }
  IOReturn reportRemovability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportEjectability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportWriteProtection (bool *value) override { *value = true; return kIOReturnSuccess; }
  IOReturn doSynchronizeCache () override { return online ? kIOReturnSuccess : kIOReturnNotReady; }
  IOReturn doSynchronize (UInt64, UInt64, IOOptionBits) override { return doSynchronizeCache (); }
  IOReturn getWriteCacheState (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn setWriteCacheState (bool) override { return kIOReturnNotWritable; }
  IOReturn doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                             IOStorageAttributes *attributes,
                             IOStorageCompletion *completion) override;
};

class NativeUfsProbe : public IOService {
  OSDeclareDefaultStructors (NativeUfsProbe)
  UfsQueue fQueue = {};
  OSSharedPtr<IOMemoryMap> fListMap;
  OSSharedPtr<IOMemoryMap> fUcdMap;
  IOLock *fLock = nullptr;
  bool fReady = false;
  uint64_t fLogFirst = 0;
  uint32_t fLogBlocks = 0;
  uint64_t fLogImageBytes = 0;
  IOBufferMemoryDescriptor *fLogDma = nullptr;
  uint8_t *fLogData = nullptr;
  uint64_t fLogDataPhys = 0;
  IOWorkLoop *fHbLoop = nullptr;
  IOTimerEventSource *fHbTimer = nullptr;
  uint64_t fHbNext = 0;
  static void logThread (void *, wait_result_t);
  static void hbTimerFired (OSObject *owner, IOTimerEventSource *sender);
  void hbTimerTick (IOTimerEventSource *sender);
  bool logWrite (uint64_t rel, uint32_t blocks, const void *src);
  bool logSync (void);

public:
  bool start (IOService *provider) override;
  void free () override;
  bool h103Snapshot (const char *tag, bool pause, bool verify);
  void h103SlotWrite (void);
  void h103HeaderOnly (void);
  void h103SingleWrite (void);
  void h103WindowRead (void);
  bool readBlocks (uint8_t unit, uint64_t block, uint64_t count, uint32_t blockSize,
                   IOMemoryDescriptor *dst, uint64_t *done);
  bool readWindowBulk (uint64_t lba, uint32_t blocks, void *out);
  bool readWindow4k (uint64_t lba, void *out);
};

static void
h103SlotWriteThunk (void)
{
  if (gH103Probe) {
    gH103Probe->h103SlotWrite ();
  }
}

static void
h103HeaderWriteThunk (void)
{
  if (gH103Probe) {
    gH103Probe->h103HeaderOnly ();
  }
}

// H108: one flush, shared by the read path and the 1 Hz diagnostic thread.
// h103SlotWrite() computes its delta from gH103LastSlotMsgX, so this helper
// owns the busy flag, the interval stamp and the cursor update.
static void
h103FlushNow (void)
{
  if (gH103SlotBusy) {
    return;
  }
  struct H103MsgBuf *m = msgbufp;
  if (!m || !m->msg_bufc || m->msg_size <= 0) {
    return;
  }
  int x = m->msg_bufx;
  if (x < 0 || x >= m->msg_size) {
    x = 0;
  }
  gH103SlotBusy = 1;
  gH103LastSlotTicks = mach_absolute_time ();
  h103SlotWriteThunk ();
  gH103SlotBusy = 0;
  gH103LastSlotMsgX = x;
}

// H116: called by the display archive from the client-open and frame-copy paths.
// Those paths still run after the UFS read path has gone quiet, so a forced
// flush here is the only way to persist the black-screen moment.
extern "C" void
native_log_flush_hook (void)
{
  if (gH103Probe == nullptr) {
    return;
  }
  h103FlushNow ();
}

// H117: called from XNU's panic path before the machine-layer handler prints
// and stops the machine. The marker goes into the message buffer first, so a
// written log proves a panic even though the panic text is printed afterwards.
extern "C" void
native_panic_flush (void)
{
  if (gH103Probe == nullptr) {
    return;
  }
  IOLog ("NATIVE_PANIC: flags=%x client=%u xfer=%u frames=%u\n",
         native_boot_flags, native_legacy_client_calls,
         native_legacy_transfer_calls, native_legacy_frame_count);
  h103FlushNow ();
}

// The spawned threads never wake from IOSleep on this kernel. Run the slot
// timer from the normal read paths only after fLock has been released; calling
// it from ufsCommand's completion would deadlock on the non-recursive fLock.
static void
h103SlotTick (void)
{
  // H109: prove the timer_call heartbeat from the log, independently of the
  // display: one line every 30 s when the tick counter advanced.
  static volatile uint32_t printedTicks;
  uint32_t ticks = gH109Ticks;
  if (ticks && ticks != printedTicks && (ticks % 30 == 0 || printedTicks == 0)) {
    printedTicks = ticks;
    IOLog ("NATIVE_TICK: sec=%u flags=%x gate=%d\n", ticks, native_boot_flags,
           h107GateOpen () ? 1 : 0);
  }
  // H111/H112: sample the arch timer state every 5 s from the read path, so a
  // freeze leaves one in the last screen. The ArmCpu counter only moves when
  // INTID 27 actually arrives; CNTV_CTL shows whether the kernel enabled and
  // unmasked the virtual timer at all.
  {
    static volatile uint64_t lastIrqPrint;
    uint64_t nowIrq = mach_absolute_time ();
    if (!lastIrqPrint) {
      lastIrqPrint = nowIrq;
    } else if (nowIrq - lastIrqPrint >= 5 * 19200000ULL) {
      lastIrqPrint = nowIrq;
      // H113: the inline "mrs %0, cntv_ctl_el0" read returned a kernel
      // address, so use the same builtins XNU's machine_routines.c uses.
      // DAIF and the GIC group/Priority Mask registers tell whether ANY
      // interrupt can reach this CPU.
      uint64_t ctl = __builtin_arm_rsr64 ("CNTV_CTL_EL0");
      uint64_t tval = __builtin_arm_rsr64 ("CNTV_TVAL_EL0");
      uint64_t daif = 0, pmr = 0, grp1 = 0;
      __asm__ volatile ("mrs %0, daif" : "=r" (daif));
      __asm__ volatile ("mrs %0, icc_pmr_el1" : "=r" (pmr));
      __asm__ volatile ("mrs %0, icc_igrpen1_el1" : "=r" (grp1));
      IOLog ("NATIVE_IRQ: timer=%llu ipi=%llu daif=%llx pmr=%llx grp1=%llx ctl=%llx tval=%llx t19=%llu t27=%llu dispatch=%u rearm=%llu storm=%llx sn=%llu flags=%x tick=%u\n",
             (unsigned long long) armcpu_timer_interrupts,
             (unsigned long long) armcpu_ipi_interrupts,
             (unsigned long long) (daif & 0x3ffULL),
             (unsigned long long) (pmr & 0xffULL),
             (unsigned long long) (grp1 & 1ULL),
             (unsigned long long) ctl, (unsigned long long) tval,
             (unsigned long long) armcpu_timer19_interrupts,
             (unsigned long long) armcpu_timer27_interrupts,
             armcpu_timer_dispatch,
             (unsigned long long) armcpu_timer_rearm_count,
             (unsigned long long) armcpu_irq_storm_id,
             (unsigned long long) armcpu_irq_storm_count,
             native_boot_flags, gH109Ticks);
      if (gH114GicProbe) {
        IOLog ("NATIVE_GIC: typer=%llx isen=%llx igrp=%llx ipend=%llx ipri=%llx\n",
               (unsigned long long) armcpu_ppi_probe[0],
               (unsigned long long) armcpu_ppi_probe[1],
               (unsigned long long) armcpu_ppi_probe[2],
               (unsigned long long) armcpu_ppi_probe[3],
               (unsigned long long) armcpu_ppi_probe[4]);
        // H120: DS bit, redistributor wake state, Group 0 enable and the retried
        // group assignment. The ICC_HPPIR/HPPIR0/RPR reads are gone: they crash.
        IOLog ("NATIVE_GIC2: dctl=%llx gtyper=%llx waker=%llx grp0=%llu igrp2=%llx\n",
               (unsigned long long) armcpu_ppi_probe[5],
               (unsigned long long) armcpu_ppi_probe[9],
               (unsigned long long) armcpu_ppi_probe[6],
               (unsigned long long) armcpu_ppi_probe[7],
               (unsigned long long) armcpu_ppi_probe[8]);
        IOLog ("NATIVE_GIC3: isen_off=%llx isen_on=%llx grp27=%llx\n",
               (unsigned long long) armcpu_ppi_probe[10],
               (unsigned long long) armcpu_ppi_probe[11],
               (unsigned long long) armcpu_ppi_probe[12]);
      }
    }
  }
  // H112: one forced dump + flush 120 s after the UFS start, independent of
  // any exec flag. The last three boots froze near 197 s before any gated
  // flush, so this guarantees one written record before that point.
  {
    static volatile int forcedDone;
    if (!forcedDone && gH103StartTicks &&
        mach_absolute_time () - gH103StartTicks >= 120 * 19200000ULL) {
      forcedDone = 1;
      // H127e: from here on PPI 19 only re-arms CNTV. Keep a durable state
      // field in every NATIVE_IRQ sample because this ring can overwrite the
      // one-shot transition line long before the log is pulled.
      armcpu_timer_dispatch = 1;
      IOLog ("NATIVE_TIMER_REARM: on t19=%llu t27=%llu rearm=%llu\n",
             (unsigned long long) armcpu_timer19_interrupts,
             (unsigned long long) armcpu_timer27_interrupts,
             (unsigned long long) armcpu_timer_rearm_count);
      IOLog ("NATIVE_TICK: forced dump at uptime 120 s flags=%x\n", native_boot_flags);
      native_proc_state_dump ();
      native_proc_wait_graph ();
      if (gH103SlotMode) {
        h103FlushNow ();
      }
    }
  }
  if (!gH103SlotMode) {
    return;
  }
  h103MaybeLoadIogpu ();
  h103MaybePublishIogpu ();
  // H104f/H107: no slot/header writes before backboardd actual exec. When
  // backboardd never runs, SpringBoard + 60 s opens the same gate so the dump
  // and one flush still record why it stopped.
  if (!h107GateOpen ()) {
    return;
  }
  // H115: flush on the first display-side event, so the record exists before
  // backboardd replaces the console with its first frame (or before a crash).
  static volatile uint32_t seenEvents;
  uint32_t events = 0;
  if (native_boot_flags & 4) {
    events |= 1;      // backboardd exec
  }
  if (native_boot_flags & 0x1000) {
    events |= 2;      // IOMobile match succeeded for backboardd
  }
  if (native_legacy_client_calls) {
    events |= 4;      // display user client opened
  }
  if (native_legacy_transfer_calls) {
    events |= 8;      // first client frame reached the scanout
  }
  bool force = (events & ~seenEvents) != 0;
  if (force) {
    seenEvents |= events;
    IOLog ("NATIVE_FRAME: event=%x flags=%x client=%u xfer=%u frames=%u\n",
           events, native_boot_flags, native_legacy_client_calls,
           native_legacy_transfer_calls, native_legacy_frame_count);
  }
  uint64_t interval = (gH103SlotMode == 2 ? 2u : gH103LogSecs) * 19200000ULL;
  uint64_t now = mach_absolute_time ();
  // H107: run any due dump before reading the message cursor, and force the
  // flush on the same pass so the stage lines reach the disk.
  bool dumped = h105DumpTick ();
  if (!gH103LastSlotTicks) {
    gH103LastSlotTicks = now;
    if (!dumped && !force) {
      return;
    }
  }
  if (gH103SlotBusy) {
    return;
  }
  if (!dumped && !force && now - gH103LastSlotTicks < interval) {
    return;
  }
  struct H103MsgBuf *m = msgbufp;
  if (!m || !m->msg_bufc || m->msg_size <= 0) {
    return;
  }
  int x = m->msg_bufx;
  if (x < 0 || x >= m->msg_size) {
    x = 0;
  }
  if (x == gH103LastSlotMsgX) {
    // H106: ios-sweet 0069's rule - nothing new to persist means no data
    // write. Only the header is rewritten, at the same slow interval, so
    // uptime_s still shows whether the read path is alive.
    if (now - gH103LastHeaderTicks >= interval) {
      gH103LastSlotTicks = now;
      gH103LastHeaderTicks = now;
      gH103SlotBusy = 1;
      h103HeaderWriteThunk ();
      gH103SlotBusy = 0;
    }
    return;
  }
  gH103LastSlotTicks = now;
  // H115: record what the client frame actually contained before the flush, so
  // a black screen can be told from a lost display path.
  {
    static volatile uint32_t printedFrames;
    uint32_t frames = native_legacy_frame_count;
    if (frames && frames != printedFrames) {
      printedFrames = frames;
      uint32_t k = frames > 32 ? 31 : frames - 1;
      IOLog ("NATIVE_FRAME: frames=%u kind=%u nonblack=%llu bbox=%llu,%llu-%llu,%llu\n",
             frames, native_legacy_frame_kind[k],
             (unsigned long long) native_legacy_frame_stats[k][0],
             (unsigned long long) native_legacy_frame_stats[k][1],
             (unsigned long long) native_legacy_frame_stats[k][2],
             (unsigned long long) native_legacy_frame_stats[k][3],
             (unsigned long long) native_legacy_frame_stats[k][4]);
    }
  }
  // H108: h103FlushNow owns the busy flag and the cursor update; the writer
  // leaves gH103LastSlotMsgX at the previous value until the delta is written.
  h103FlushNow ();
}

// H109: the timer interrupt fires this every second. It flips the 16x16 cell
// (a plain framebuffer fill, no locks), counts ticks and wakes the dump worker.
// Printing, task locks and UFS writes stay in the worker's thread context.
static void
h109TimerFunc (timer_call_param_t, timer_call_param_t)
{
  gH109Ticks = gH109Ticks + 1;
  uint32_t n = gH109Ticks;
  if (n <= 3 || (n % 10) == 0) {
    IOLog ("NATIVE_TICK: fire=%u flags=%x\n", n, native_boot_flags);
  }
  native_legacy_heartbeat_cell ();
  if (gH103SlotMode && gH105DumpStage < 3 && h107GateOpen ()) {
    thread_wakeup ((event_t) &gH109Chan);
  }
  if (gH109TimerCall) {
    timer_call_enter (gH109TimerCall, mach_absolute_time () + 19200000ULL, 0);
  }
}

// H109: the dump worker sleeps on an explicit wakeup, never on a thread timer,
// so it does not depend on the timer wakeup path that IOSleep and tsleep both
// missed. It runs any due dump stage and flushes that dump.
static void
h109WorkerThread (void *, wait_result_t)
{
  thread_set_thread_name (current_thread (), "h109_dump");
  for (;;) {
    assert_wait (&gH109Chan, THREAD_INTERRUPTIBLE);
    thread_block (nullptr);
    gH110WorkerWakes = gH110WorkerWakes + 1;
    IOLog ("NATIVE_TICK: worker wake=%u flags=%x\n",
           gH110WorkerWakes, native_boot_flags);
    if (!gH103SlotMode || !h107GateOpen ()) {
      continue;
    }
    if (h105DumpTick ()) {
      h103FlushNow ();
    }
  }
}

OSDefineMetaClassAndStructors (NativeUfsProbe, IOService)
OSDefineMetaClassAndStructors (NativeUfsLun, IOBlockStorageDevice)

OSDefineMetaClassAndStructors (NativeUfsWindow, IOBlockStorageDevice)

static uint32_t
read32 (uintptr_t base, unsigned offset)
{
  uint32_t value = *reinterpret_cast<volatile uint32_t *> (base + offset);
  __asm__ volatile ("dmb oshld" ::: "memory");
  return value;
}

static void
write32 (uintptr_t base, unsigned offset, uint32_t value)
{
  __asm__ volatile ("dsb sy" ::: "memory");
  *reinterpret_cast<volatile uint32_t *> (base + offset) = value;
  __asm__ volatile ("dsb sy" ::: "memory");
}

static uint32_t
getBe32 (const uint8_t *at)
{
  return (uint32_t (at[0]) << 24) | (uint32_t (at[1]) << 16) |
         (uint32_t (at[2]) << 8) | at[3];
}

static uint64_t
getBe64 (const uint8_t *at)
{
  return (uint64_t (getBe32 (at)) << 32) | getBe32 (at + 4);
}

static uint32_t
getLe32 (const uint8_t *at)
{
  return uint32_t (at[0]) | (uint32_t (at[1]) << 8) |
         (uint32_t (at[2]) << 16) | (uint32_t (at[3]) << 24);
}

static uint64_t
getLe64 (const uint8_t *at)
{
  return uint64_t (getLe32 (at)) | (uint64_t (getLe32 (at + 4)) << 32);
}

static void
putBe32 (uint8_t *at, uint32_t value)
{
  at[0] = value >> 24;
  at[1] = value >> 16;
  at[2] = value >> 8;
  at[3] = value;
}

static void
zero32 (void *start, size_t length)
{
  volatile uint32_t *word = static_cast<volatile uint32_t *> (start);
  for (size_t index = 0; index < length / 4; index++) {
    word[index] = 0;
  }
}

static void
cacheClean (void *start, size_t length)
{
  uintptr_t address = reinterpret_cast<uintptr_t> (start) & ~uintptr_t (63);
  uintptr_t end = reinterpret_cast<uintptr_t> (start) + length;
  __asm__ volatile ("dsb sy" ::: "memory");
  for (; address < end; address += 64) {
    __asm__ volatile ("dc civac, %0" :: "r" (address) : "memory");
  }
  __asm__ volatile ("dsb sy" ::: "memory");
}

static void
cacheInvalidate (void *start, size_t length)
{
  uintptr_t address = reinterpret_cast<uintptr_t> (start) & ~uintptr_t (63);
  uintptr_t end = reinterpret_cast<uintptr_t> (start) + length;
  __asm__ volatile ("dsb sy" ::: "memory");
  for (; address < end; address += 64) {
    __asm__ volatile ("dc ivac, %0" :: "r" (address) : "memory");
  }
  __asm__ volatile ("dsb sy" ::: "memory");
}

static uintptr_t
mapPhys (uint64_t phys, uint64_t length, OSSharedPtr<IOMemoryMap> &keep)
{
  IODeviceMemory *memory = IODeviceMemory::withRange (phys, length);
  if (!memory) {
    return 0;
  }
  keep = OSSharedPtr<IOMemoryMap> (memory->map (kIOMapAnywhere), OSNoRetain);
  memory->release ();
  return keep ? static_cast<uintptr_t> (keep->getVirtualAddress ()) : 0;
}

static void
dumpSmmu (uintptr_t smmu, const char *name)
{
  uint32_t idr0 = read32 (smmu, 0x20);
  uint32_t idr1 = read32 (smmu, 0x24);
  uint32_t idr2 = read32 (smmu, 0x28);
  IOLog ("NATIVE_UFS_GPT: smmu %s idr0=%08x idr1=%08x idr2=%08x sCR0=%08x\n",
         name, idr0, idr1, idr2, read32 (smmu, 0x00));
  unsigned groups = idr0 & 0xff;
  if (groups > 64) {
    groups = 64;
  }
  for (unsigned index = 0; index < groups; index++) {
    uint32_t smr = read32 (smmu, 0x800 + 4 * index);
    if (!(smr & 0x80000000U)) {
      continue;
    }
    uint32_t s2cr = read32 (smmu, 0xc00 + 4 * index);
    IOLog ("NATIVE_UFS_GPT: smmu %s smr[%u]=%08x s2cr=%08x\n",
           name, index, smr, s2cr);
  }
}

static bool
waitRegister (uintptr_t base, unsigned offset, uint32_t mask, uint32_t expected,
              unsigned microseconds);

static bool
uicCommand (uintptr_t hc, uint32_t opcode, uint32_t arg1, uint32_t arg2,
            uint32_t *result)
{
  uint32_t pending = read32 (hc, UFS_REG_IS);
  if (pending & (UFS_IS_UCCS | UFS_IS_UEE)) {
    write32 (hc, UFS_REG_IS, pending & (UFS_IS_UCCS | UFS_IS_UEE));
  }
  write32 (hc, UFS_REG_UICARG1, arg1);
  write32 (hc, UFS_REG_UICARG2, arg2);
  write32 (hc, UFS_REG_UICARG3, 0);
  write32 (hc, UFS_REG_UICCMD, opcode);
  if (!waitRegister (hc, UFS_REG_IS, UFS_IS_UCCS, UFS_IS_UCCS, 100000)) {
    return false;
  }
  uint32_t status = read32 (hc, UFS_REG_IS);
  write32 (hc, UFS_REG_IS, UFS_IS_UCCS | UFS_IS_UEE);
  if (result) {
    *result = read32 (hc, UFS_REG_UICARG3);
  }
  return (status & UFS_IS_UEE) == 0;
}

static void
spinTicks (uint64_t ticks)
{
  uint64_t start = mach_absolute_time ();
  while (mach_absolute_time () - start < ticks) {
    __asm__ volatile ("yield" ::: "memory");
  }
}

static bool
waitRegister (uintptr_t base, unsigned offset, uint32_t mask, uint32_t expected,
              unsigned microseconds)
{
  uint64_t deadline = mach_absolute_time () + uint64_t (microseconds) * 20ULL;
  while (mach_absolute_time () < deadline) {
    if ((read32 (base, offset) & mask) == expected) {
      return true;
    }
    __asm__ volatile ("yield" ::: "memory");
  }
  return false;
}

static bool
compatibleContains (OSData *compatible, const char *needle)
{
  if (!compatible) {
    return false;
  }
  const uint8_t *bytes = static_cast<const uint8_t *> (compatible->getBytesNoCopy ());
  size_t length = compatible->getLength ();
  size_t wanted = strlen (needle) + 1;
  if (!bytes || length < wanted) {
    return false;
  }
  for (size_t offset = 0; offset + wanted <= length; offset++) {
    if (memcmp (bytes + offset, needle, wanted) == 0) {
      return true;
    }
  }
  return false;
}

static bool
readCommandAllowed (unsigned opcode, unsigned direction, unsigned bytes)
{
  if (bytes > UFS_DATA_MAX || (bytes & 3)) {
    return false;
  }
  switch (opcode) {
    case 0x00: return direction == 0 && bytes == 0; // TEST UNIT READY
    case 0x25: return direction == 2 && bytes == 8; // READ CAPACITY(10)
    case 0x9e: return direction == 2 && bytes == 32; // READ CAPACITY(16)
    case 0x28: return direction == 2 && bytes != 0; // READ(10)
    case 0x2a: return direction == 1 && bytes != 0; // WRITE(10), persistent log
    case 0x35: return direction == 0 && bytes == 0; // SYNCHRONIZE CACHE, log
    default: return false;
  }
}

static bool
waitForCompletion (uint8_t *list, uint32_t *ocs, unsigned *loops,
                   uint64_t timeoutTicks)
{
  uint64_t start = mach_absolute_time ();
  unsigned loop = 0;
  for (;;) {
    cacheInvalidate (list, 64);
    *ocs = read32 (reinterpret_cast<uintptr_t> (list), 8) & 0xff;
    if (*ocs != 0x0f) {
      *loops = loop;
      return true;
    }
    if (timeoutTicks && mach_absolute_time () - start > timeoutTicks) {
      *loops = loop;
      return false;
    }
    if (loop++ > 100000000U) {
      *loops = loop;
      return false;
    }
    __asm__ volatile ("yield" ::: "memory");
  }
}

// Bounded lock acquisition: a stuck holder must not turn a diagnostic read into
// an unobservable hang. 19.2 MHz timebase, so 38400000 ticks is two seconds.
static bool
lockWithTimeout (IOLock *lock, const char *who, uint64_t timeoutTicks)
{
  uint64_t start = mach_absolute_time ();
  while (!IOLockTryLock (lock)) {
    if (mach_absolute_time () - start > timeoutTicks) {
      IOLog ("NATIVE_UFS_LOCK: %s timeout ticks=%llu\n", who,
             (unsigned long long) (mach_absolute_time () - start));
      return false;
    }
    __asm__ volatile ("yield" ::: "memory");
  }
  return true;
}

static bool
blockRange (uint64_t block, uint64_t count, uint64_t last, uint32_t blockSize)
{
  return count && blockSize >= 512 && blockSize <= 4096 &&
         (blockSize & (blockSize - 1)) == 0 && block <= last &&
         count - 1 <= last - block;
}

static void
dumpMediaServices (void)
{
  OSDictionary *matching = IOService::serviceMatching ("IOMedia");
  OSIterator *iterator = matching ? IOService::getMatchingServices (matching) : nullptr;
  unsigned count = 0;
  while (iterator && count < 16) {
    IOService *media = OSDynamicCast (IOService, iterator->getNextObject ());
    if (!media) {
      break;
    }
    OSString *content = OSDynamicCast (OSString, media->getProperty ("Content"));
    OSString *bsd = OSDynamicCast (OSString, media->getProperty ("BSD Name"));
    OSString *uuid = OSDynamicCast (OSString, media->getProperty ("UUID"));
    OSNumber *size = OSDynamicCast (OSNumber, media->getProperty ("Size"));
    OSNumber *base = OSDynamicCast (OSNumber, media->getProperty ("Base"));
    IOLog ("NATIVE_UFS_MEDIA: media[%u] content=%s bsd=%s size=%llx base=%llx uuid=%s\n",
           count,
           content ? content->getCStringNoCopy () : "-",
           bsd ? bsd->getCStringNoCopy () : "-",
           size ? size->unsigned64BitValue () : 0ULL,
           base ? base->unsigned64BitValue () : 0ULL,
           uuid ? uuid->getCStringNoCopy () : "-");
    count++;
  }
  OSSafeReleaseNULL (iterator);
  OSSafeReleaseNULL (matching);
  IOLog ("NATIVE_UFS_MEDIA: total=%u\n", count);
}

static bool
rootMediaRelevant (OSString *content, OSString *uuid, OSNumber *size)
{
  if (content) {
    const char *value = content->getCStringNoCopy ();
    if (value && (strcmp (value, "Apple_APFS") == 0 ||
                  strcmp (value, "Apple_HFS") == 0)) {
      return true;
    }
  }
  if (uuid) {
    const char *value = uuid->getCStringNoCopy ();
    if (value && (strcmp (value, "04769D26-2CA0-4825-B443-E2E5F88300F2") == 0 ||
                  strcmp (value, "8846FE78-2910-4297-AF97-D59BB6F8FC16") == 0)) {
      return true;
    }
  }
  if (size) {
    uint64_t value = size->unsigned64BitValue ();
    return value == 6077580800ULL || value == 3303014400ULL;
  }
  return false;
}

static void
dumpRootMedia (void)
{
  OSDictionary *matching = IOService::serviceMatching ("IOMedia");
  OSIterator *iterator = matching ? IOService::getMatchingServices (matching) : nullptr;
  unsigned total = 0;
  unsigned shown = 0;
  while (iterator && total < 512) {
    IOService *media = OSDynamicCast (IOService, iterator->getNextObject ());
    if (!media) {
      break;
    }
    total++;
    OSString *content = OSDynamicCast (OSString, media->getProperty ("Content"));
    OSString *bsd = OSDynamicCast (OSString, media->getProperty ("BSD Name"));
    OSString *uuid = OSDynamicCast (OSString, media->getProperty ("UUID"));
    OSNumber *size = OSDynamicCast (OSNumber, media->getProperty ("Size"));
    OSNumber *base = OSDynamicCast (OSNumber, media->getProperty ("Base"));
    if (shown < 32 && rootMediaRelevant (content, uuid, size)) {
      IOLog ("NATIVE_UFS_ROOT_MEDIA: media[%u] content=%s bsd=%s size=%llx base=%llx uuid=%s\n",
             shown,
             content ? content->getCStringNoCopy () : "-",
             bsd ? bsd->getCStringNoCopy () : "-",
             size ? size->unsigned64BitValue () : 0ULL,
             base ? base->unsigned64BitValue () : 0ULL,
             uuid ? uuid->getCStringNoCopy () : "-");
      shown++;
    }
  }
  OSSafeReleaseNULL (iterator);
  OSSafeReleaseNULL (matching);
  IOLog ("NATIVE_UFS_ROOT_MEDIA: total=%u shown=%u\n", total, shown);
}

// Temporary B5 bring-up authority: the windows are read-only and their content
// came from the verified SHNGROOT image.  Replace with VerifiedRoot before any
// final configuration.
extern "C" int
native_verified_block_root_authorized (dev_t device)
{
  return device != NODEV;
}

extern "C" int
native_verified_block_volume_device (const char *uuid, dev_t *device,
                                    char *path, size_t size)
{
  if (!uuid || !device || !path || size < 6 || strlen (uuid) != 36) {
    return 0;
  }
  *device = NODEV;
  path[0] = 0;
  OSDictionary *matching = IOService::serviceMatching ("IOMedia");
  OSDictionary *properties = OSDictionary::withCapacity (1);
  OSString *wanted = OSString::withCString (uuid);
  if (!matching || !properties || !wanted) {
    OSSafeReleaseNULL (wanted);
    OSSafeReleaseNULL (properties);
    OSSafeReleaseNULL (matching);
    return 0;
  }
  bool ready = properties->setObject ("UUID", wanted) &&
               matching->setObject ("IOPropertyMatch", properties);
  OSSafeReleaseNULL (wanted);
  OSSafeReleaseNULL (properties);
  OSIterator *iterator = ready ? IOService::getMatchingServices (matching) : nullptr;
  OSSafeReleaseNULL (matching);
  if (!iterator) {
    return 0;
  }
  unsigned matches = 0;
  bool authorized = false;
  dev_t candidate = NODEV;
  char candidatePath[64] = {};
  while (OSObject *object = iterator->getNextObject ()) {
    IOService *media = OSDynamicCast (IOService, object);
    if (!media) {
      continue;
    }
    if (++matches != 1) {
      authorized = false;
      break;
    }
    OSString *identity = OSDynamicCast (OSString, media->getProperty ("UUID"));
    OSString *name = OSDynamicCast (OSString, media->getProperty (kIOBSDNameKey));
    OSNumber *major = OSDynamicCast (OSNumber, media->getProperty (kIOBSDMajorKey));
    OSNumber *minor = OSDynamicCast (OSNumber, media->getProperty (kIOBSDMinorKey));
    if (!identity || strcmp (identity->getCStringNoCopy (), uuid) || !name ||
        !major || !minor || !name->getLength () ||
        name->getLength () >= sizeof (candidatePath) - 6 ||
        name->getLength () >= size - 6 ||
        major->unsigned64BitValue () > 255 ||
        minor->unsigned64BitValue () > 0xffffff) {
      continue;
    }
    candidate = static_cast<dev_t> (makedev (major->unsigned32BitValue (),
                                             minor->unsigned32BitValue ()));
    if (!native_verified_block_root_authorized (candidate)) {
      continue;
    }
    snprintf (candidatePath, sizeof (candidatePath), "/dev/%s",
              name->getCStringNoCopy ());
    authorized = true;
  }
  OSSafeReleaseNULL (iterator);
  if (!authorized || matches != 1) {
    return 0;
  }
  *device = candidate;
  strlcpy (path, candidatePath, size);
  return 1;
}

static bool
ufsSubmit (UfsQueue *q, unsigned direction, unsigned bytes,
           uint64_t timeoutTicks = UFS_TIMEOUT_TICKS)
{
  if (!q || q->poisoned || bytes > q->dataCapacity ||
      read32 (q->hc, UFS_REG_UTRLDBR) ||
      (read32 (q->hc, UFS_REG_IS) & UFS_FATAL_STATUS)) {
    return false;
  }
  uint8_t *ucd = q->ucd;
  if (ucd[0] == 1) {
    if (!readCommandAllowed (ucd[16], direction, bytes)) {
      return false;
    }
  } else if (ucd[0] != 0 || bytes || direction) {
    return false;
  }
  uint32_t *utrd = reinterpret_cast<uint32_t *> (q->list);
  zero32 (utrd, 32);
  // Vendor ufshci.h/ufshcd.c: SCSI is CT=0, NOP OUT is UTP_CMD_TYPE_DEV_MANAGE
  // (CT=2); both carry UTP_REQ_DESC_INT_CMD.  0x10000000 is native UFS CT=1 and
  // leaves this controller's UTP engine stuck.
  utrd[0] = (ucd[0] == 0) ? 0x21000000U : (0x01000000U | (direction << 25));
  utrd[2] = 0x0fU;
  utrd[4] = static_cast<uint32_t> (q->ucdPhys);
  utrd[5] = static_cast<uint32_t> (q->ucdPhys >> 32);
  utrd[6] = (512U >> 2) | ((UFS_RSP_OFFSET >> 2) << 16);
  utrd[7] = (bytes ? 1U : 0U) | ((UFS_PRDT_OFFSET >> 2) << 16);
  if (bytes) {
    uint32_t *prd = reinterpret_cast<uint32_t *> (ucd + UFS_PRDT_OFFSET);
    prd[0] = static_cast<uint32_t> (q->dataPhys);
    prd[1] = static_cast<uint32_t> (q->dataPhys >> 32);
    prd[2] = 0;
    prd[3] = bytes - 1;
  }
  gUfsCommands = gUfsCommands + 1;
  gUfsInFlight = 1;
  if (gNopActive) {
    IOLog ("NATIVE_UFS_NOP: submit before cacheClean is=%x hcs=%x db=%x\n",
           read32 (q->hc, UFS_REG_IS), read32 (q->hc, UFS_REG_HCS),
           read32 (q->hc, UFS_REG_UTRLDBR));
  }
  cacheClean (q->list, 4096);
  cacheClean (q->ucd, UFS_FW_UCD_SIZE);
  if (gNopActive) {
    IOLog ("NATIVE_UFS_NOP: submit after cacheClean is=%x hcs=%x db=%x\n",
           read32 (q->hc, UFS_REG_IS), read32 (q->hc, UFS_REG_HCS),
           read32 (q->hc, UFS_REG_UTRLDBR));
    spinTicks (19200);
    IOLog ("NATIVE_UFS_NOP: submit before doorbell\n");
  }
  write32 (q->hc, UFS_REG_IS, 0xffffffffU);
  write32 (q->hc, UFS_REG_UTRLDBR, 1);
  if (gNopActive) {
    IOLog ("NATIVE_UFS_NOP: submit after doorbell is=%x hcs=%x db=%x\n",
           read32 (q->hc, UFS_REG_IS), read32 (q->hc, UFS_REG_HCS),
           read32 (q->hc, UFS_REG_UTRLDBR));
    spinTicks (19200);
  }
  uint64_t commandStart = mach_absolute_time ();
  uint32_t ocs = 0x0f;
  unsigned loops = 0;
  if (gNopActive) {
    IOLog ("NATIVE_UFS_NOP: waiting timeout=%llu\n",
           (unsigned long long) timeoutTicks);
  }
  if (!waitForCompletion (q->list, &ocs, &loops, timeoutTicks)) {
    q->poisoned = true;
    gUfsInFlight = 0;
    gUfsLastLoops = loops;
    gUfsLastTicks = mach_absolute_time () - commandStart;
    IOLog ("NATIVE_UFS_GPT: OCS timeout type=%08x ocs=%x loops=%u "
           "db=%x is=%x hcs=%x hce=%x ucd0=%x cdb=%02x%02x%02x%02x%02x%02x\n",
           utrd[0], ocs, loops,
           read32 (q->hc, UFS_REG_UTRLDBR), read32 (q->hc, UFS_REG_IS),
           read32 (q->hc, UFS_REG_HCS), read32 (q->hc, UFS_REG_HCE),
           q->ucd[0], q->ucd[16], q->ucd[17], q->ucd[18], q->ucd[19],
           q->ucd[20], q->ucd[21]);
    return false;
  }
  cacheInvalidate (q->list, 4096);
  cacheInvalidate (q->ucd, UFS_FW_UCD_SIZE);
  if (ocs) {
    gUfsInFlight = 0;
    gUfsLastLoops = loops;
    gUfsLastTicks = mach_absolute_time () - commandStart;
    IOLog ("NATIVE_UFS_GPT: OCS=%x type=%08x loops=%u\n", ocs, utrd[0], loops);
    return false;
  }
  gUfsInFlight = 0;
  gUfsLastLoops = loops;
  gUfsLastTicks = mach_absolute_time () - commandStart;
  if ((gUfsCommands & 63) == 0) {
    // H103bd's UFS completion path redrew the 2x2 before printing PROGRESS.
    // That frequent redraw, not the 10-second heartbeat alone, is what kept
    // the panel alive while the console scrolled. Restore it verbatim. Keep
    // the redraw independent of gH103Quiet so a later quiet build cannot lose
    // the panel again. H103fl's 1 Hz timer adds extra redraws; at the stuck
    // screen a 1 Hz blink proves the work loop is alive, a static cell means
    // the timer did not fire or the kernel hung.
    // H105: dump before the draw (a display open can block it), and stop
    // drawing once backboardd has exec'd, as H104j intended for the heartbeat.
    h105DumpTick ();
    {
      // H124: repaint whenever a gate bit changes instead of freezing the
      // panel at its pre-backboardd colour. The old "stop drawing once
      // backboardd exec'd" left the 2x2 green even after the white gate was
      // reached, because the heartbeat thread never wakes without a timer.
      static uint32_t paintedFlags;
      uint32_t gate = native_boot_flags & 0xfff;
      if (!(native_boot_flags & 4) || gate != paintedFlags) {
        paintedFlags = gate;
        native_legacy_test_pattern ();
      }
    }
    if (!gH103Quiet) {
      IOLog ("NATIVE_UFS_PROGRESS: cmds=%llu last=%llx/%llu loops=%llu ticks=%llu "
             "winReq=%llu winBytes=%llu winFlight=%llu lastWin=%llx/%llu "
             "req=%llx/%llu\n",
             (unsigned long long) gUfsCommands,
             (unsigned long long) gUfsLastLba,
             (unsigned long long) gUfsLastBytes,
             (unsigned long long) gUfsLastLoops,
             (unsigned long long) gUfsLastTicks,
             (unsigned long long) gWindowRequests,
             (unsigned long long) gWindowBytes,
             (unsigned long long) gWindowInFlight,
             (unsigned long long) gWindowLastBlock,
             (unsigned long long) gWindowLastCount,
             (unsigned long long) gWindowLastReqBlock,
             (unsigned long long) gWindowLastReqCount);
    }
  }
  return true;
}

static bool
ufsCommand (UfsQueue *q, uint8_t lun, const uint8_t *cdb, unsigned bytes, void *dst)
{
  if (!q || !readCommandAllowed (cdb[0], bytes ? 2 : 0, bytes) || q->poisoned ||
      bytes > q->dataCapacity || read32 (q->hc, UFS_REG_UTRLDBR)) {
    return false;
  }
  zero32 (q->ucd, UFS_FW_UCD_SIZE);
  uint8_t *req = q->ucd;
  req[0] = 1;
  req[1] = bytes ? 0x40 : 0;
  req[2] = lun;
  req[3] = 1;
  putBe32 (req + 12, bytes);
  memcpy (req + 16, cdb, 16);
  cacheInvalidate (q->data, bytes);
  if (!ufsSubmit (q, bytes ? 2 : 0, bytes)) {
    return false;
  }
  cacheInvalidate (q->data, bytes);
  const uint8_t *response = req + UFS_RSP_OFFSET;
  if (response[0] != 0x21 || response[2] != lun || response[3] != 1 ||
      response[4] || response[6] || response[7] || response[10] ||
      response[11] || getBe32 (response + 12)) {
    IOLog ("NATIVE_UFS_GPT: bad response type=%x lun=%x tag=%x flags=%x r6=%x status=%x r10=%x r11=%x residual=%x\n",
           response[0], response[2], response[3], response[4], response[6],
           response[7], response[10], response[11], getBe32 (response + 12));
    return false;
  }
  if (bytes && dst && dst != q->data) {
    memcpy (dst, q->data, bytes);
  }
  return true;
}

static bool
ufsWriteCommand (UfsQueue *q, uint8_t lun, const uint8_t *cdb, unsigned bytes,
                 const void *src)
{
  if (!q || q->poisoned || !src || !bytes || bytes > q->dataCapacity ||
      (bytes & 3) || read32 (q->hc, UFS_REG_UTRLDBR)) {
    return false;
  }
  if (src != q->data) {
    memcpy (q->data, src, bytes);
  }
  zero32 (q->ucd, UFS_FW_UCD_SIZE);
  uint8_t *req = q->ucd;
  req[0] = 1;
  req[1] = 0x20;
  req[2] = lun;
  req[3] = 1;
  putBe32 (req + 12, bytes);
  memcpy (req + 16, cdb, 16);
  cacheClean (q->data, bytes);
  if (!ufsSubmit (q, 1, bytes)) {
    return false;
  }
  const uint8_t *response = req + UFS_RSP_OFFSET;
  if (response[0] != 0x21 || response[2] != lun || response[3] != 1 ||
      response[4] || response[6] || response[7] || response[10] ||
      response[11] || getBe32 (response + 12)) {
    IOLog ("NATIVE_UFS_LOG: bad write response type=%x lun=%x tag=%x flags=%x r6=%x status=%x residual=%x\n",
           response[0], response[2], response[3], response[4], response[6],
           response[7], getBe32 (response + 12));
    return false;
  }
  return true;
}

static bool
ufsSync (UfsQueue *q)
{
  if (!q || q->poisoned || read32 (q->hc, UFS_REG_UTRLDBR)) {
    return false;
  }
  zero32 (q->ucd, UFS_FW_UCD_SIZE);
  uint8_t *req = q->ucd;
  req[0] = 1;
  req[1] = 0;
  req[2] = 0;
  req[3] = 1;
  req[16] = 0x35;
  if (!ufsSubmit (q, 0, 0)) {
    return false;
  }
  const uint8_t *response = req + UFS_RSP_OFFSET;
  if (response[0] != 0x21 || response[3] != 1 || response[4] ||
      response[6] || response[7] || response[10] || response[11]) {
    return false;
  }
  return true;
}

static bool
ufsNop (UfsQueue *q)
{
  IOLog ("NATIVE_UFS_NOP: enter\n");
  if (!q) {
    return false;
  }
  IOLog ("NATIVE_UFS_NOP: precheck\n");
  uint32_t doorbell = read32 (q->hc, UFS_REG_UTRLDBR);
  IOLog ("NATIVE_UFS_NOP: precheck poisoned=%u db=%x\n",
         q->poisoned ? 1 : 0, doorbell);
  if (q->poisoned || doorbell) {
    return false;
  }
  zero32 (q->ucd, UFS_FW_UCD_SIZE);
  q->ucd[3] = 1;
  gNopActive = true;
  IOLog ("NATIVE_UFS_NOP: before submit\n");
  bool submitted = ufsSubmit (q, 0, 0, UFS_TIMEOUT_TICKS);
  gNopActive = false;
  IOLog ("NATIVE_UFS_NOP: after submit ok=%u poisoned=%u\n",
         submitted ? 1 : 0, q->poisoned ? 1 : 0);
  if (!submitted) {
    return false;
  }
  const uint8_t *response = q->ucd + UFS_RSP_OFFSET;
  return response[0] == 0x20 && response[3] == 1;
}

bool
NativeUfsProbe::readBlocks (uint8_t unit, uint64_t block, uint64_t count,
                            uint32_t blockSize, IOMemoryDescriptor *dst,
                            uint64_t *done)
{
  if (!fReady || !fLock || !dst || dst->getDirection () != kIODirectionIn ||
      !blockSize || block > 0xffffffffULL) {
    return false;
  }
  if (!lockWithTimeout (fLock, "readBlocks", 38400000ULL)) {
    IOLog ("NATIVE_UFS_LOCK: readBlocks blocked block=%llx count=%llu\n",
           block, count);
    return false;
  }
  uint64_t total = 0;
  bool ok = true;
  while (count) {
    unsigned chunk = static_cast<unsigned> (fQueue.dataCapacity / blockSize);
    if (chunk == 0) {
      ok = false;
      break;
    }
    if (count < chunk) {
      chunk = static_cast<unsigned> (count);
    }
    uint8_t cdb[16] = { 0 };
    cdb[0] = 0x28;
    putBe32 (cdb + 2, static_cast<uint32_t> (block));
    cdb[7] = static_cast<uint8_t> (chunk >> 8);
    cdb[8] = static_cast<uint8_t> (chunk);
    unsigned bytes = chunk * blockSize;
    gUfsLastLba = block;
    gUfsLastBytes = bytes;
    if (!ufsCommand (&fQueue, unit, cdb, bytes, nullptr)) {
      ok = false;
      break;
    }
    if (dst->writeBytes (static_cast<IOByteCount> (total), fQueue.data,
                         static_cast<IOByteCount> (bytes)) !=
        static_cast<IOByteCount> (bytes)) {
      ok = false;
      break;
    }
    total += bytes;
    block += chunk;
    count -= chunk;
  }
  IOLockUnlock (fLock);
  h103SlotTick ();
  if (done) {
    *done = total;
  }
  return ok;
}

bool
NativeUfsProbe::readWindowBulk (uint64_t lba, uint32_t blocks, void *out)
{
  if (!fReady || !fLock || !out || !blocks ||
      blocks > UFS_DATA_MAX / 4096 ||
      lba > 0xffffffffULL - (blocks - 1)) {
    return false;
  }
  uint64_t lockStart = mach_absolute_time ();
  if (!lockWithTimeout (fLock, "readWindowBulk", 38400000ULL)) {
    IOLog ("NATIVE_UFS_LOCK: readWindowBulk blocked lba=%llx blocks=%u wait=%llu\n",
           lba, blocks, (unsigned long long) (mach_absolute_time () - lockStart));
    return false;
  }
  uint64_t lockTicks = mach_absolute_time () - lockStart;
  uint8_t cdb[16] = { 0x28 };
  putBe32 (cdb + 2, static_cast<uint32_t> (lba));
  cdb[7] = static_cast<uint8_t> (blocks >> 8);
  cdb[8] = static_cast<uint8_t> (blocks);
  unsigned bytes = blocks * 4096;
  gUfsLastLba = lba;
  gUfsLastBytes = bytes;
  uint64_t commandStart = mach_absolute_time ();
  bool ok = ufsCommand (&fQueue, 0, cdb, bytes, out);
  uint64_t commandTicks = mach_absolute_time () - commandStart;
  uint64_t unlockStart = mach_absolute_time ();
  IOLockUnlock (fLock);
  h103SlotTick ();
  uint64_t unlockTicks = mach_absolute_time () - unlockStart;
  static unsigned gBulkPhases;
  unsigned call = gBulkPhases++;
  if (!gH103Quiet && (call < 8 || commandTicks > 19200000ULL || lockTicks > 19200000ULL)) {
    IOLog ("NATIVE_UFS_BULK: lba=%llx blocks=%u lock=%llu cmd=%llu unlock=%llu ok=%u\n",
           lba, blocks, (unsigned long long) lockTicks,
           (unsigned long long) commandTicks,
           (unsigned long long) unlockTicks, ok ? 1 : 0);
  }
  return ok;
}

bool
NativeUfsProbe::readWindow4k (uint64_t lba, void *out)
{
  return readWindowBulk (lba, 1, out);
}

bool
NativeUfsProbe::logWrite (uint64_t rel, uint32_t blocks, const void *src)
{
  if (!fReady || !fLock || !src || !blocks || rel >= fLogBlocks ||
      blocks > fLogBlocks - rel || blocks > UFS_DATA_MAX / 4096) {
    return false;
  }
  if (!fLogDma || !fLogData || !fLogDataPhys) {
    return false;
  }
  const unsigned bytes = blocks * 4096U;
  if (!lockWithTimeout (fLock, "logWrite", 38400000ULL)) {
    return false;
  }
  uint8_t cdb[16] = { 0x2a };
  cdb[1] = 0x08;  // FUA: commit this log write without a SYNCHRONIZE CACHE
  putBe32 (cdb + 2, static_cast<uint32_t> (fLogFirst + rel));
  cdb[7] = static_cast<uint8_t> (blocks >> 8);
  cdb[8] = static_cast<uint8_t> (blocks);
  // getBytesNoCopy() on the DMA descriptor is not a CPU-write mapping here.
  // Fill it through the IOMemoryDescriptor writeBytes() path instead, then
  // pass the DMA pointer as src so ufsWriteCommand() skips its own memcpy.
  IOByteCount copied = fLogDma->writeBytes (0, src, bytes);
  if (copied != static_cast<IOByteCount> (bytes)) {
    IOLog ("NATIVE_UFS_LOG: log DMA writeBytes short=%llu want=%u lba=%llx\n",
           (unsigned long long) copied, bytes,
           (unsigned long long) (fLogFirst + rel));
    IOLockUnlock (fLock);
    return false;
  }
  uint8_t *savedData = fQueue.data;
  uint64_t savedPhys = fQueue.dataPhys;
  uint32_t savedCapacity = fQueue.dataCapacity;
  fQueue.data = fLogData;
  fQueue.dataPhys = fLogDataPhys;
  fQueue.dataCapacity = UFS_DATA_MAX;
  bool ok = ufsWriteCommand (&fQueue, 0, cdb, bytes, fLogData);
  fQueue.data = savedData;
  fQueue.dataPhys = savedPhys;
  fQueue.dataCapacity = savedCapacity;
  IOLockUnlock (fLock);
  return ok;
}

bool
NativeUfsProbe::logSync (void)
{
  if (!fReady || !fLock) {
    return false;
  }
  if (!lockWithTimeout (fLock, "logSync", 38400000ULL)) {
    return false;
  }
  bool ok = ufsSync (&fQueue);
  IOLockUnlock (fLock);
  return ok;
}

void
NativeUfsProbe::logThread (void *raw, wait_result_t)
{
  NativeUfsProbe *self = static_cast<NativeUfsProbe *> (raw);
  if (!self) {
    return;
  }
  thread_set_thread_name (current_thread (), "h103_log");
  // Let root mount and the early display/root messages enter the kernel ring.
  IOSleep (20000);
  self->h103Snapshot ("thread", false, false);
}

bool
NativeUfsProbe::h103Snapshot (const char *tag, bool pause, bool verify)
{
  if (!fLogFirst || !fLogBlocks || !fReady || !fLock) {
    IOLog ("NATIVE_UFS_LOG: snapshot skipped tag=%s first=%llx blocks=%u "
           "ready=%u lock=%u\n", tag ? tag : "?", (unsigned long long) fLogFirst,
           fLogBlocks, fReady ? 1 : 0, fLock ? 1 : 0);
    return false;
  }

  // Write the self-test before inspecting the window. The old code returned
  // early when block 0 held stale data, which made every retry after a partial
  // write impossible to distinguish from a dead writer.
  //
  // Keep the two 4 KiB self-test buffers off the stack: start() already uses a
  // 9 KiB frame and h103Snapshot() used to add another 8 KiB, overflowing the
  // 16 KiB kernel stack before the first WRITE(10).
  uint8_t *test = static_cast<uint8_t *> (IOMallocAligned (4096, 4096));
  uint8_t *check = static_cast<uint8_t *> (IOMallocAligned (4096, 4096));
  if (!test || !check) {
    if (test) {
      IOFreeAligned (test, 4096);
    }
    if (check) {
      IOFreeAligned (check, 4096);
    }
    IOLog ("NATIVE_UFS_LOG: snapshot test allocation failed tag=%s\n",
           tag ? tag : "?");
    if (pause) {
      IOSleep (30000);
    }
    return false;
  }
  memset (test, 0, 4096);
  memset (check, 0, 4096);
  memcpy (test, "H103TEST", 8);
  snprintf (reinterpret_cast<char *> (test + 16), 128,
            "tag=%s lba=%llx wrote=0 read=0", tag ? tag : "?",
            (unsigned long long) fLogFirst);
  bool testWrite = logWrite (0, 1, test);
  bool testRead = false;
  if (verify) {
    testRead = testWrite && readWindow4k (fLogFirst, check) &&
               memcmp (check, "H103TEST", 8) == 0;
  }
  snprintf (reinterpret_cast<char *> (test + 16), 128,
            "tag=%s lba=%llx wrote=%u read=%u", tag ? tag : "?",
            (unsigned long long) fLogFirst, testWrite ? 1 : 0,
            testRead ? 1 : 0);
  logWrite (0, 1, test);
  bool endWrite = logWrite (H103_LOG_BLOCKS - 1, 1, test);
  IOLog ("NATIVE_UFS_LOG: write test tag=%s verify=%u wrote=%u read=%u end=%u "
         "lba=%llx\n", tag ? tag : "?", verify ? 1 : 0, testWrite ? 1 : 0,
         testRead ? 1 : 0, endWrite ? 1 : 0,
         (unsigned long long) fLogFirst);
  if (!testWrite || (verify && !testRead)) {
    IOFreeAligned (test, 4096);
    IOFreeAligned (check, 4096);
    if (pause) {
      IOSleep (30000);
    }
    return false;
  }
  IOFreeAligned (check, 4096);
  check = nullptr;
  IOFreeAligned (test, 4096);
  test = nullptr;

  uint8_t *body = static_cast<uint8_t *> (
    IOMallocAligned (H103_LOG_MSG_BLOCKS * 4096U, 4096));
  if (!body) {
    IOLog ("NATIVE_UFS_LOG: snapshot body allocation failed tag=%s\n",
           tag ? tag : "?");
    if (pause) {
      IOSleep (30000);
    }
    return false;
  }
  uint32_t msgBytes = h103CopyMsgbuf (body);
  bool ok = true;
  for (uint32_t offset = 0; ok && offset < H103_LOG_MSG_BLOCKS; offset += 32) {
    ok = logWrite (1 + offset, 32, body + offset * 4096U);
  }

  uint8_t *head = static_cast<uint8_t *> (IOMallocAligned (4096, 4096));
  if (!head) {
    IOFreeAligned (body, H103_LOG_MSG_BLOCKS * 4096U);
    IOLog ("NATIVE_UFS_LOG: snapshot header allocation failed tag=%s\n",
           tag ? tag : "?");
    if (pause) {
      IOSleep (30000);
    }
    return false;
  }
  memset (head, 0, 4096);
  H103LogHeader h;
  memset (&h, 0, sizeof h);
  memcpy (h.magic, "SHLOG001", 8);
  h.version = 3;
  h.header_bytes = sizeof h;
  h.boot_id = h103LogTicks ();
  h.first_lba = fLogFirst;
  h.window_blocks = fLogBlocks;
  h.msg_first = 1;
  h.msg_blocks = H103_LOG_MSG_BLOCKS;
  h.msg_bytes = msgBytes;
  h.slot_first = 257;
  h.slot_blocks = 32;
  h.slot_count = (fLogBlocks > 257) ? (fLogBlocks - 257) / 32 : 0;
  h.slots_written = gH103SlotCursor;
  h.rounds = gH103SlotCursor ? gH103SlotCursor : 1;
  h.uptime_s = gH103StartTicks ?
      static_cast<uint32_t> ((mach_absolute_time () - gH103StartTicks) / 19200000ULL) : 0;
  h.write_errors = gH103SlotWriteErrors;
  h.export_first = static_cast<uint32_t> (gUfsCommands);
  h.export_blocks = static_cast<uint32_t> (gUfsLastLba);
  h.export_bytes = static_cast<uint32_t> (gUfsLastBytes);
  h.export_generation = ((gUfsInFlight ? 1U : 0U) | (gWindowInFlight ? 2U : 0U) |
                         ((gH103TimerFires & 0xffffffU) << 8));
  h.export_below = static_cast<uint32_t> (gUfsLastLoops);
  h.state = gH103SlotCursor ? 3 : 2;
  strlcpy (h.tag, "cheeseburger-H103", sizeof h.tag);
  memcpy (head, &h, sizeof h);
  ok = ok && logWrite (0, 1, head);
  // Do not issue SYNCHRONIZE CACHE here. The WRITE(10) uses FUA; the UFS sync
  // command has been observed to hold fLock indefinitely, which also blocks
  // the heartbeat late snapshot and 2x2 redraw.
  IOLog ("NATIVE_UFS_LOG: snapshot tag=%s ok=%u msg=%u first=%llx blocks=%u\n",
         tag ? tag : "?", ok ? 1 : 0, msgBytes,
         (unsigned long long) fLogFirst, fLogBlocks);
  IOFreeAligned (head, 4096);
  IOFreeAligned (body, H103_LOG_MSG_BLOCKS * 4096U);
  if (pause) {
    IOSleep (30000);
  }
  return ok;
}

void
NativeUfsProbe::h103SlotWrite (void)
{
  static uint32_t attempts;
  bool ready = fLogFirst && fLogBlocks && fReady && fLock && fLogDma;
  if (!ready) {
    if (attempts++ < 8) {
      IOLog ("NATIVE_UFS_SLOT: wait=%u first=%llx blocks=%u ready=%u "
             "lock=%u dma=%u\n", attempts,
             (unsigned long long) fLogFirst, fLogBlocks,
             fReady ? 1 : 0, fLock ? 1 : 0, fLogDma ? 1 : 0);
    }
    return;
  }
  const uint32_t slotCount = (fLogBlocks > 257) ? (fLogBlocks - 257) / 32 : 0;
  if (!slotCount) {
    if (attempts++ < 8) {
      IOLog ("NATIVE_UFS_SLOT: no slots blocks=%u\n", fLogBlocks);
    }
    return;
  }
  native_h103_slot_state = 1;
  uint8_t *body = static_cast<uint8_t *> (IOMallocAligned (32U * 4096U, 4096));
  if (!body) {
    if (attempts++ < 8) {
      IOLog ("NATIVE_UFS_SLOT: body allocation failed\n");
    }
    return;
  }
  // H106: ios-sweet 0069's delta write. Only the bytes produced since the
  // previous flush are copied and written, capped at one 32-block slot. A
  // quiet kernel therefore writes nothing but the header.
  struct H103MsgBuf *m = msgbufp;
  uint32_t size = (m && m->msg_size > 0) ? static_cast<uint32_t> (m->msg_size) : 0;
  uint32_t cursor = (m && m->msg_bufx >= 0 && m->msg_bufx < m->msg_size) ?
      static_cast<uint32_t> (m->msg_bufx) : 0;
  uint32_t previous = gH103LastSlotMsgX < 0 ? 0 : static_cast<uint32_t> (gH103LastSlotMsgX);
  uint32_t delta = size ? (cursor + size - previous) % size : 0;
  uint32_t blocks = (delta + 4095U) / 4096U + 1U;
  if (blocks > 32U) {
    blocks = 32U;
  }
  if (blocks < 1U) {
    blocks = 1U;
  }
  uint32_t n = h103CopyMsgbufTail (body, blocks * 4096U);
  uint32_t slot = gH103SlotCursor % slotCount;
  bool ok = logWrite (257U + slot * 32U, blocks, body);
  if (!ok) {
    gH103SlotWriteErrors = gH103SlotWriteErrors + 1;
  }
  if (attempts++ < 8) {
    IOLog ("NATIVE_UFS_SLOT: attempt=%u slot=%u lba=%llx body=%u blocks=%u delta=%u n=%u fires=%u\n",
           attempts, slot, (unsigned long long) (fLogFirst + 257U + slot * 32U),
           ok ? 1 : 0, blocks, delta, n, gH103TimerFires);
  }
  if (ok) {
    H103LogHeader h;
    memset (&h, 0, sizeof h);
    memcpy (h.magic, "SHLOG001", 8);
    h.version = 3;
    h.header_bytes = sizeof h;
    h.boot_id = h103LogTicks ();
    h.first_lba = fLogFirst;
    h.window_blocks = fLogBlocks;
    h.msg_first = 1;
    h.msg_blocks = H103_LOG_MSG_BLOCKS;
    h.msg_bytes = n;
    h.slot_first = 257;
    h.slot_blocks = 32;
    h.slot_count = slotCount;
    h.slots_written = gH103SlotCursor + 1;
    h.rounds = gH103SlotCursor + 1;
    h.uptime_s = gH103StartTicks ?
        static_cast<uint32_t> ((mach_absolute_time () - gH103StartTicks) / 19200000ULL) : 0;
    h.write_errors = gH103SlotWriteErrors;
    h.export_first = static_cast<uint32_t> (gUfsCommands);
    h.export_blocks = static_cast<uint32_t> (gUfsLastLba);
    h.export_bytes = static_cast<uint32_t> (gUfsLastBytes);
    h.export_generation = ((gUfsInFlight ? 1U : 0U) | (gWindowInFlight ? 2U : 0U) |
                         ((gH103TimerFires & 0xffffffU) << 8));
    h.export_below = static_cast<uint32_t> (gUfsLastLoops);
    h.state = 3;
    strlcpy (h.tag, "cheeseburger-H103", sizeof h.tag);
    uint8_t *head = static_cast<uint8_t *> (IOMallocAligned (4096, 4096));
    if (head) {
      memset (head, 0, 4096);
      memcpy (head, &h, sizeof h);
      bool headOk = logWrite (0, 1, head);
      if (attempts < 8) {
        IOLog ("NATIVE_UFS_SLOT: header=%u cursor=%u\n",
               headOk ? 1 : 0, gH103SlotCursor);
      }
      if (headOk) {
        gH103SlotCursor++;
        gH103LastMsgBytes = n;
        gH103LastHeaderTicks = mach_absolute_time ();
        native_h103_slot_state = 2;
      } else {
        gH103SlotWriteErrors = gH103SlotWriteErrors + 1;
      }
      IOFreeAligned (head, 4096);
    }
  }
  IOFreeAligned (body, 32U * 4096U);
}

void
NativeUfsProbe::h103HeaderOnly (void)
{
  const uint32_t slotCount = (fLogBlocks > 257) ? (fLogBlocks - 257) / 32 : 0;
  if (!fLogFirst || !fLogBlocks || !fLogDma || !fLogData || !fLogDataPhys ||
      !slotCount) {
    return;
  }
  H103LogHeader h;
  memset (&h, 0, sizeof h);
  memcpy (h.magic, "SHLOG001", 8);
  h.version = 3;
  h.header_bytes = sizeof h;
  h.boot_id = h103LogTicks ();
  h.first_lba = fLogFirst;
  h.window_blocks = fLogBlocks;
  h.msg_first = 1;
  h.msg_blocks = H103_LOG_MSG_BLOCKS;
  h.msg_bytes = gH103LastMsgBytes;
  h.slot_first = 257;
  h.slot_blocks = 32;
  h.slot_count = slotCount;
  h.slots_written = gH103SlotCursor;
  h.rounds = gH103SlotCursor ? gH103SlotCursor : 1;
  h.uptime_s = gH103StartTicks ?
      static_cast<uint32_t> ((mach_absolute_time () - gH103StartTicks) / 19200000ULL) : 0;
  h.write_errors = gH103SlotWriteErrors;
  h.export_first = static_cast<uint32_t> (gUfsCommands);
  h.export_blocks = static_cast<uint32_t> (gUfsLastLba);
  h.export_bytes = static_cast<uint32_t> (gUfsLastBytes);
  h.export_generation = ((gUfsInFlight ? 1U : 0U) | (gWindowInFlight ? 2U : 0U) |
                         ((gH103TimerFires & 0xffffffU) << 8));
  h.export_below = static_cast<uint32_t> (gUfsLastLoops);
  h.state = gH103SlotCursor ? 3 : 2;
  strlcpy (h.tag, "cheeseburger-H103", sizeof h.tag);
  uint8_t *head = static_cast<uint8_t *> (IOMallocAligned (4096, 4096));
  if (!head) {
    return;
  }
  memset (head, 0, 4096);
  memcpy (head, &h, sizeof h);
  bool ok = logWrite (0, 1, head);
  if (!ok) {
    gH103SlotWriteErrors = gH103SlotWriteErrors + 1;
  } else {
    gH103LastHeaderTicks = mach_absolute_time ();
  }
  IOFreeAligned (head, 4096);
}

void
NativeUfsProbe::free (void)
{
  if (gH103TimerCall) {
    timer_call_cancel (gH103TimerCall);
    timer_call_free (gH103TimerCall);
    gH103TimerCall = nullptr;
  }
  if (fHbTimer) {
    fHbTimer->cancelTimeout ();
    if (fHbLoop) {
      fHbLoop->removeEventSource (fHbTimer);
    }
    fHbTimer->release ();
    fHbTimer = nullptr;
  }
  if (fHbLoop) {
    fHbLoop->release ();
    fHbLoop = nullptr;
  }
  IOService::free ();
}

void
NativeUfsProbe::hbTimerFired (OSObject *owner, IOTimerEventSource *sender)
{
  NativeUfsProbe *self = static_cast<NativeUfsProbe *> (owner);
  if (self) {
    self->hbTimerTick (sender);
  }
}

void
NativeUfsProbe::hbTimerTick (IOTimerEventSource *sender)
{
  static unsigned fires;
  static int stallLogged;
  fires++;
  if (fires <= 4) {
    IOLog ("NATIVE_UFS_HB_TIMER: fire=%u cmds=%llu\n", fires,
           (unsigned long long) gUfsCommands);
  }
  if (!gH103SkipPattern) {
    native_legacy_test_pattern ();
  }
  uint64_t now = mach_absolute_time ();
  if (gUfsInFlight && gWindowStartTicks &&
      now - gWindowStartTicks > 5 * 19200000ULL) {
    if (!stallLogged) {
      stallLogged = 1;
      IOLog ("NATIVE_UFS_HB_TIMER: stall cmds=%llu last=%llx/%llu "
             "req=%llx/%llu winFlight=%llu ufsFlight=%llu loops=%llu\n",
             (unsigned long long) gUfsCommands,
             (unsigned long long) gUfsLastLba,
             (unsigned long long) gUfsLastBytes,
             (unsigned long long) gWindowLastReqBlock,
             (unsigned long long) gWindowLastReqCount,
             (unsigned long long) gWindowInFlight,
             (unsigned long long) gUfsInFlight,
             (unsigned long long) gUfsLastLoops);
    }
  } else {
    stallLogged = 0;
  }
  fHbNext += 19200000ULL;
  if (fHbNext <= now) {
    fHbNext = now + 19200000ULL;
  }
  if (sender) {
    sender->wakeAtTime (fHbNext);
  }
}

void
NativeUfsProbe::h103WindowRead (void)
{
  // The real-time slot writer in the heartbeat owns the persistent log now.
  // Do not write a 1 MiB body from the storage completion path.
}

static uint8_t gH103Test[4096] __attribute__((aligned(4096)));

void
NativeUfsProbe::h103SingleWrite (void)
{
  if (!fLogFirst || !fLogBlocks || !fReady || !fLock ||
      !fLogData || !fLogDataPhys) {
    IOLog ("NATIVE_UFS_LOG: single write skipped first=%llx ready=%u "
           "lock=%u dma=%u\n", (unsigned long long) fLogFirst,
           fReady ? 1 : 0, fLock ? 1 : 0,
           (fLogData && fLogDataPhys) ? 1 : 0);
    return;
  }
  memset (gH103Test, 0, sizeof gH103Test);
  memcpy (gH103Test, "H103TEST", 8);
  snprintf (reinterpret_cast<char *> (gH103Test + 16), 128,
            "single lba=%llx", (unsigned long long) fLogFirst);
  IOLog ("NATIVE_UFS_LOG: single write begin lba=%llx\n",
         (unsigned long long) fLogFirst);
  bool ok = logWrite (0, 1, gH103Test);
  IOLog ("NATIVE_UFS_LOG: single write result=%u lba=%llx\n",
         ok ? 1 : 0, (unsigned long long) fLogFirst);
  IOSleep (30000);
}

bool
NativeUfsWindow::configure (NativeUfsProbe *parent, const char *name,
                            uint64_t absoluteByte, uint64_t length)
{
  host = parent;
  absolute = absoluteByte;
  bytes = length;
  bounce = static_cast<uint8_t *> (IOMalloc (UFS_DATA_MAX));
  readAhead = static_cast<uint8_t *> (IOMalloc (UFS_DATA_MAX));
  readAheadLba = 0;
  readAheadBytes = 0;
  lock = IOLockAlloc ();
  if (name) {
    strlcpy (label, name, sizeof (label));
  }
  setProperty ("B5Window", label);
  setProperty ("B5Offset", absolute, 64);
  setProperty ("B5Bytes", bytes, 64);
  setProperty ("B5ReadOnly", true);
  if (readAhead) {
    IOLog ("NATIVE_UFS_CACHE: %s read-ahead=%u bytes\n", label, UFS_DATA_MAX);
  }
  return host && absolute && bytes && !(absolute & 4095) && !(bytes & 511) &&
         bounce && readAhead && lock;
}

IOReturn
NativeUfsWindow::doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block,
                                   UInt64 count, IOStorageAttributes *,
                                   IOStorageCompletion *completion)
{
  if (!buffer || !completion || !completion->action || !count ||
      block >= bytes / 512 || count > bytes / 512 - block ||
      buffer->getLength () < count * 512) {
    return kIOReturnBadArgument;
  }
  if (buffer->getDirection () != kIODirectionIn) {
    return kIOReturnNotWritable;
  }
  if (!online || !bounce || !readAhead || !lock) {
    return kIOReturnNotReady;
  }
  uint64_t request = gWindowRequests;
  gWindowRequests = request + 1;
  gWindowBytes = gWindowBytes + count * 512;
  gWindowLastBlock = block;
  gWindowLastCount = count;
  gWindowLastReqBlock = block;
  gWindowLastReqCount = count;
  gWindowStartTicks = mach_absolute_time ();
  gWindowInFlight = 1;
  if (!gH103Quiet && (request < 8 || (request & 1023) == 0)) {
    IOLog ("NATIVE_UFS_WINDOW: %s req=%llu block=%llx count=%llu\n",
           label, request, block, count);
  }
  uint64_t requestStart = mach_absolute_time ();
  uint64_t cursor = absolute + block * 512;
  uint64_t end = cursor + count * 512;
  uint64_t done = 0;
  uint64_t readTicks = 0;
  uint64_t writeTicks = 0;
  bool ok = true;
  uint64_t lockStart = mach_absolute_time ();
  if (!lockWithTimeout (lock, label, 38400000ULL)) {
    IOLog ("NATIVE_UFS_LOCK: window %s blocked req=%llu block=%llx count=%llu\n",
           label, request, block, count);
    gWindowInFlight = 0;
    return kIOReturnTimeout;
  }
  uint64_t lockTicks = mach_absolute_time () - lockStart;
  while (cursor < end) {
    uint64_t page = cursor & ~4095ULL;
    unsigned inPage = static_cast<unsigned> (cursor - page);
    // APFS issues many sequential 4 KiB reads. Serve them from a page-aligned
    // 128 KiB read-ahead window so one UFS command covers up to 32 requests.
    if (readAheadBytes && page >= readAheadLba &&
        page + 4096 <= readAheadLba + readAheadBytes) {
      uint64_t offset = cursor - readAheadLba;
      uint64_t available = readAheadBytes - offset;
      unsigned wanted = static_cast<unsigned> (end - cursor);
      if (wanted > available) {
        wanted = static_cast<unsigned> (available);
      }
      uint64_t phase = mach_absolute_time ();
      IOByteCount copied = buffer->writeBytes (done, readAhead + offset, wanted);
      writeTicks += mach_absolute_time () - phase;
      if (copied != wanted) {
        ok = false;
        break;
      }
      cursor += wanted;
      done += wanted;
      continue;
    }
    uint64_t windowEnd = absolute + bytes;
    uint64_t available = windowEnd > page ? windowEnd - page : 0;
    if (available > UFS_DATA_MAX) {
      available = UFS_DATA_MAX;
    }
    uint32_t blocks = static_cast<uint32_t> ((available + 4095) / 4096);
    if (!blocks) {
      blocks = 1;
    }
    if (blocks > UFS_DATA_MAX / 4096) {
      blocks = UFS_DATA_MAX / 4096;
    }
    uint64_t phase = mach_absolute_time ();
    bool readOk = host->readWindowBulk (page / 4096, blocks, readAhead);
    readTicks += mach_absolute_time () - phase;
    if (!readOk) {
      ok = false;
      readAheadBytes = 0;
      break;
    }
    readAheadLba = page;
    readAheadBytes = static_cast<uint64_t> (blocks) * 4096;
    unsigned wanted = static_cast<unsigned> (end - cursor);
    uint64_t cached = readAheadBytes - inPage;
    if (wanted > cached) {
      wanted = static_cast<unsigned> (cached);
    }
    phase = mach_absolute_time ();
    IOByteCount copied = buffer->writeBytes (done, readAhead + inPage, wanted);
    writeTicks += mach_absolute_time () - phase;
    if (copied != wanted) {
      ok = false;
      break;
    }
    cursor += wanted;
    done += wanted;
  }
  uint64_t unlockStart = mach_absolute_time ();
  IOLockUnlock (lock);
  uint64_t unlockTicks = mach_absolute_time () - unlockStart;
  gWindowInFlight = 0;
  gWindowStartTicks = 0;
  uint64_t requestTicks = mach_absolute_time () - requestStart;
  if (!gH103Quiet && (request < 8 || (request & 1023) == 0 || requestTicks > 19200000ULL)) {
    IOLog ("NATIVE_UFS_WINDOW: %s req=%llu done=%llu ok=%u ticks=%llu "
           "read=%llu write=%llu\n",
           label, request, done, ok ? 1 : 0,
           (unsigned long long) requestTicks,
           (unsigned long long) readTicks,
           (unsigned long long) writeTicks);
  }
  if (!gH103Quiet && request < 4) {
    IOLog ("NATIVE_UFS_PHASE: %s req=%llu lock=%llu read=%llu write=%llu unlock=%llu\n",
           label, request, (unsigned long long) lockTicks,
           (unsigned long long) readTicks, (unsigned long long) writeTicks,
           (unsigned long long) unlockTicks);
  }
  completion->action (completion->target, completion->parameter,
                      ok ? kIOReturnSuccess : kIOReturnIOError, done);
  if (ok && host) {
    host->h103WindowRead ();
  }
  return kIOReturnSuccess;
}

bool
NativeUfsLun::configure (NativeUfsProbe *parent, uint8_t unit, uint64_t lastLba,
                         uint32_t blockSize)
{
  host = parent;
  lun = unit;
  last = lastLba;
  sector = blockSize;
  setProperty ("B5Lun", unit, 32);
  setProperty ("B5ReadOnly", true);
  return true;
}

static unsigned gIoRequestLog;
static unsigned gIoRequests;
static unsigned gBlock0Requests;
static bool gMediaDumped;

IOReturn
NativeUfsLun::doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block,
                                UInt64 count, IOStorageAttributes *,
                                IOStorageCompletion *completion)
{
  if (!buffer || buffer->getDirection () != kIODirectionIn) {
    return kIOReturnNotWritable;
  }
  if (!host || !blockRange (block, count, last, sector) ||
      count > ~uint64_t (0) / sector ||
      buffer->getLength () != count * sector) {
    return kIOReturnBadArgument;
  }
  gIoRequests++;
  if (block == 0) {
    gBlock0Requests++;
  }
  if (!gH103Quiet && gIoRequestLog < 16) {
    IOLog ("NATIVE_UFS_IO: req block=%llx count=%llu sector=%x len=%llx\n",
           block, count, sector, buffer->getLength ());
    gIoRequestLog++;
  }
  uint64_t started = mach_absolute_time ();
  uint64_t done = 0;
  bool ok = host->readBlocks (lun, block, count, sector, buffer, &done);
  uint64_t finished = mach_absolute_time ();
  IOReturn status = ok ? kIOReturnSuccess : kIOReturnIOError;
  if (!gH103Quiet && gIoRequestLog < 32) {
    IOLog ("NATIVE_UFS_IO: done block=%llx count=%llu ok=%u done=%llx status=%x\n",
           block, count, ok ? 1 : 0, done, status);
    gIoRequestLog++;
  }
  if (!gH103Quiet && gIoRequests <= 8) {
    IOLog ("NATIVE_UFS_IO: timing block=%llx ufs_ticks=%llu total_ticks=%llu\n",
           block, finished - started, mach_absolute_time () - started);
  }
  if (gIoRequests >= 4 && !gMediaDumped) {
    gMediaDumped = true;
    IOLog ("NATIVE_UFS_IO: one-shot IOMedia snapshot before completion at request %u\n",
           gIoRequests);
    dumpMediaServices ();
  }
  if (completion && completion->action) {
    if (!gH103Quiet && gIoRequests <= 8) {
      IOLog ("NATIVE_UFS_IO: completion enter block=%llx\n", block);
    }
    completion->action (completion->target, completion->parameter, status, done);
    if (!gH103Quiet && gIoRequests <= 8) {
      IOLog ("NATIVE_UFS_IO: completion leave block=%llx\n", block);
    }
  }
  if (!gH103Quiet && (gIoRequests & 31) == 0) {
    IOLog ("NATIVE_UFS_IO: requests=%u block0=%u last_block=%llx\n",
           gIoRequests, gBlock0Requests, block);
  }
  return kIOReturnSuccess;
}

bool
NativeUfsProbe::start (IOService *provider)
{
  if (!IOService::start (provider)) {
    return false;
  }

  OSData *compatible = OSDynamicCast (OSData, provider->getProperty ("compatible"));
  OSData *profile = OSDynamicCast (OSData, provider->getProperty ("p8,msm8998-direct-dma"));
  if (!compatibleContains (compatible, "qcom,ufshc")) {
    panic ("NATIVE_UFS_GPT: qcom,ufshc ADT node not matched");
  }
  if (!profile || profile->getLength () != sizeof (uint32_t) ||
      *static_cast<const uint32_t *> (profile->getBytesNoCopy ()) != 1) {
    panic ("NATIVE_UFS_GPT: missing p8,msm8998-direct-dma profile");
  }
  IOLog ("NATIVE_UFS_GPT: warm-link probe start\n");

  IOMemoryDescriptor *gccDesc = IOMemoryDescriptor::withPhysicalAddress (
    MSM8998_GCC_BASE, MSM8998_GCC_SIZE, kIODirectionInOut);
  if (!gccDesc) {
    panic ("NATIVE_UFS_GPT: GCC map descriptor failed");
  }
  IOMemoryMap *gccMap = gccDesc->map ();
  gccDesc->release ();
  if (!gccMap) {
    panic ("NATIVE_UFS_GPT: GCC map failed");
  }
  uintptr_t gcc = static_cast<uintptr_t> (gccMap->getVirtualAddress ());
  const unsigned clockOffsets[] = {
    0x75004, // UFS GDSC
    0x7500c, // gcc_ufs_ahb_clk
    0x75008, // gcc_ufs_axi_clk
    0x76008, // gcc_ufs_unipro_core_clk
    0x82028, // gcc_aggre1_ufs_axi_clk
    0x7600c, // gcc_ufs_ice_core_clk
    0x76040, // gcc_ufs_phy_aux_clk
    0x75010, // gcc_ufs_tx_symbol_0_clk
    0x75014, // gcc_ufs_rx_symbol_0_clk
    0x7605c, // gcc_ufs_rx_symbol_1_clk
  };
  const unsigned clockCount = sizeof (clockOffsets) / sizeof (clockOffsets[0]);
  for (unsigned index = 0; index < clockCount; index++) {
    uint32_t value = read32 (gcc, clockOffsets[index]);
    IOLog ("NATIVE_UFS_GPT: gcc %x=%08x\n", clockOffsets[index], value);
  }
  // The three branches checked by P8/P11 are not the whole UFS AXI path on
  // MSM8998: gcc_aggre1_ufs_axi_clk (0x82028) gates the UFS bus master's
  // access to DDR.  If the hand-off left that branch disabled, the CPU can
  // still read the host registers while every DMA command hangs forever.
  // Enable only the branch bit (BIT0), preserve all other fields, and require
  // the branch to report enabled and not halted (BIT31 clear).
  const unsigned criticalClocks[] = { 0x7500c, 0x75008, 0x76008, 0x82028 };
  for (unsigned index = 0; index < sizeof (criticalClocks) / sizeof (criticalClocks[0]); index++) {
    unsigned offset = criticalClocks[index];
    uint32_t value = read32 (gcc, offset);
    if ((value & 0x80000001U) == 0x00000001U) {
      continue;
    }
    IOLog ("NATIVE_UFS_GPT: enabling gcc %x old=%08x\n", offset, value);
    write32 (gcc, offset, value | 0x00000001U);
    if (!waitRegister (gcc, offset, 0x80000001U, 0x00000001U, 20000)) {
      panic ("NATIVE_UFS_GPT: UFS clock not running at %x=%08x",
             offset, read32 (gcc, offset));
    }
    IOLog ("NATIVE_UFS_GPT: gcc %x now=%08x\n", offset, read32 (gcc, offset));
  }

  IOMemoryDescriptor *hcDesc = IOMemoryDescriptor::withPhysicalAddress (
    MSM8998_UFS_HC_BASE, MSM8998_UFS_HC_SIZE, kIODirectionInOut);
  if (!hcDesc) {
    panic ("NATIVE_UFS_GPT: HC descriptor failed");
  }
  IOMemoryMap *hcMap = hcDesc->map ();
  hcDesc->release ();
  if (!hcMap) {
    panic ("NATIVE_UFS_GPT: HC map failed");
  }
  uintptr_t hc = static_cast<uintptr_t> (hcMap->getVirtualAddress ());
  uint32_t cap = read32 (hc, 0x00);
  uint32_t version = read32 (hc, 0x08);
  uint32_t is = read32 (hc, UFS_REG_IS);
  uint32_t hcs = read32 (hc, UFS_REG_HCS);
  uint32_t hce = read32 (hc, UFS_REG_HCE);
  uint32_t doorbell = read32 (hc, UFS_REG_UTRLDBR);
  uint32_t taskDoorbell = read32 (hc, UFS_REG_TASK_DOORBELL);
  IOLog ("NATIVE_UFS_GPT: HC cap=%08x ver=%08x is=%08x hcs=%08x hce=%08x\n",
         cap, version, is, hcs, hce);
  if (!(hce & 1) || ((hcs & 3) != 3) || (is & UFS_FATAL_STATUS) ||
      doorbell || taskDoorbell) {
    panic ("NATIVE_UFS_GPT: controller not idle hce=%08x hcs=%08x is=%08x db=%08x tdb=%08x",
           hce, hcs, is, doorbell, taskDoorbell);
  }

  uint32_t ahit = read32 (hc, UFS_REG_AHIT);
  IOLog ("NATIVE_UFS_GPT: uic probe ahit=%08x is=%08x hcs=%08x\n",
         ahit, read32 (hc, UFS_REG_IS), read32 (hc, UFS_REG_HCS));
  uint32_t powerMode = 0;
  bool gotPowerMode = uicCommand (hc, UFS_UIC_DME_GET, 0x15710000U, 0, &powerMode);
  IOLog ("NATIVE_UFS_GPT: uic ahit=%08x get=%u mode=%08x is=%08x hcs=%08x\n",
         ahit, gotPowerMode, powerMode, read32 (hc, UFS_REG_IS),
         read32 (hc, UFS_REG_HCS));
  if (!gotPowerMode) {
    uint32_t wakeResult = 0;
    bool woke = uicCommand (hc, UFS_UIC_DME_HIBERNATE_EXIT, 0, 0, &wakeResult);
    IOLog ("NATIVE_UFS_GPT: uic hibernate-exit=%u result=%08x is=%08x hcs=%08x\n",
           woke, wakeResult, read32 (hc, UFS_REG_IS), read32 (hc, UFS_REG_HCS));
    powerMode = 0;
    gotPowerMode = uicCommand (hc, UFS_UIC_DME_GET, 0x15710000U, 0, &powerMode);
    IOLog ("NATIVE_UFS_GPT: uic after-wake get=%u mode=%08x is=%08x hcs=%08x\n",
           gotPowerMode, powerMode, read32 (hc, UFS_REG_IS),
           read32 (hc, UFS_REG_HCS));
  }

  OSSharedPtr<IOMemoryMap> smmu1Map, smmu2Map;
  uintptr_t smmu1 = mapPhys (0x1680000ULL, 0x10000ULL, smmu1Map);
  if (smmu1) {
    dumpSmmu (smmu1, "anoc1");
  } else {
    IOLog ("NATIVE_UFS_GPT: smmu anoc1 map failed\n");
  }
  uintptr_t smmu2 = mapPhys (0x16c0000ULL, 0x40000ULL, smmu2Map);
  if (smmu2) {
    dumpSmmu (smmu2, "anoc2");
  } else {
    IOLog ("NATIVE_UFS_GPT: smmu anoc2 map failed\n");
  }

  // The firmware's own UTRL/UCD page is the only low physical DMA region that
  // the controller is known to have used.  Reuse that page (not the firmware
  // high data buffer): list at +0, UCD at +0x100, response at +0x200, PRDT at
  // +0x400 and a small low scratch at +0x800.  The page must be cache-cleaned
  // because MSM8998 UFS is not coherent (ACPI _CCA=0); writing it through a
  // cacheable alias without cleaning leaves the controller reading the old
  // firmware descriptors.
  uint64_t listPhys = (uint64_t (read32 (hc, UFS_REG_UTRLBAU)) << 32) |
                      read32 (hc, UFS_REG_UTRLBA);
  if (!listPhys || (listPhys & 0x3ff)) {
    panic ("NATIVE_UFS_GPT: no usable firmware list base=%llx", listPhys);
  }
  OSSharedPtr<IOMemoryMap> listMap, ucdMap;
  uintptr_t listVirt = mapPhys (listPhys, 4096, listMap);
  if (!listVirt) {
    panic ("NATIVE_UFS_GPT: firmware UTRL map failed base=%llx", listPhys);
  }
  uint64_t ucdPhys = listPhys + UFS_UCD_OFFSET;
  uintptr_t ucdVirt = mapPhys (ucdPhys, UFS_FW_UCD_SIZE, ucdMap);
  if (!ucdVirt) {
    panic ("NATIVE_UFS_GPT: firmware UCD map failed base=%llx", ucdPhys);
  }
  uint8_t *list = reinterpret_cast<uint8_t *> (listVirt);
  uint8_t *ucd = reinterpret_cast<uint8_t *> (ucdVirt);
  uint8_t *data = ucd + UFS_FW_DATA_OFFSET;
  uint64_t dataPhys = ucdPhys + UFS_FW_DATA_OFFSET;
  uint32_t dataCapacity = UFS_FW_DATA_MAX;
  uint32_t fw0 = read32 (listVirt, 0);
  uint32_t fw2 = read32 (listVirt, 8);
  cacheInvalidate (list, 4096);
  cacheInvalidate (ucd, UFS_FW_UCD_SIZE);
  zero32 (list, 4096);
  zero32 (ucd, UFS_FW_UCD_SIZE);
  cacheClean (list, 4096);
  cacheClean (ucd, UFS_FW_UCD_SIZE);
  IOLog ("NATIVE_UFS_GPT: firmware page zero list0=%08x list2=%08x ucd0=%08x\n",
         read32 (listVirt, 0), read32 (listVirt, 8), read32 (ucdVirt, 0));
  IOLog ("NATIVE_UFS_GPT: firmware page list=%llx ucd=%llx data=%llx cap=%x fw=%08x/%08x\n",
         listPhys, ucdPhys, dataPhys, dataCapacity, fw0, fw2);

  // Install the private queue, preserving the firmware values only for a
  // diagnostic rollback-free log.  The controller is not reset.
  uint32_t oldRun = read32 (hc, UFS_REG_UTRLRSR);
  uint32_t oldLo = read32 (hc, UFS_REG_UTRLBA);
  uint32_t oldHi = read32 (hc, UFS_REG_UTRLBAU);
  uint32_t oldIE = read32 (hc, UFS_REG_IE);
  IOLog ("NATIVE_UFS_GPT: old queue run=%x ba=%x/%x ie=%x\n", oldRun, oldLo, oldHi, oldIE);
  write32 (hc, UFS_REG_UTRLRSR, 0);
  if (!waitRegister (hc, UFS_REG_UTRLRSR, 1, 0, 10000) || read32 (hc, UFS_REG_UTRLDBR)) {
    panic ("NATIVE_UFS_GPT: queue did not stop rsr=%x db=%x",
           read32 (hc, UFS_REG_UTRLRSR), read32 (hc, UFS_REG_UTRLDBR));
  }
  write32 (hc, UFS_REG_IE, 0);
  write32 (hc, UFS_REG_UTRLBA, static_cast<uint32_t> (listPhys));
  write32 (hc, UFS_REG_UTRLBAU, static_cast<uint32_t> (listPhys >> 32));
  write32 (hc, UFS_REG_IS, 0xffffffffU);
  write32 (hc, UFS_REG_UTRLRSR, 1);
  if (!waitRegister (hc, UFS_REG_UTRLRSR, 1, 1, 10000) ||
      !waitRegister (hc, UFS_REG_HCS, 2, 2, 10000)) {
    panic ("NATIVE_UFS_GPT: queue did not start rsr=%x hcs=%x",
           read32 (hc, UFS_REG_UTRLRSR), read32 (hc, UFS_REG_HCS));
  }
  if (read32 (hc, UFS_REG_UTRLBA) != static_cast<uint32_t> (listPhys) ||
      read32 (hc, UFS_REG_UTRLBAU) != static_cast<uint32_t> (listPhys >> 32)) {
    panic ("NATIVE_UFS_GPT: queue base readback ba=%x/%x want=%llx",
           read32 (hc, UFS_REG_UTRLBA), read32 (hc, UFS_REG_UTRLBAU),
           listPhys);
  }
  IOLog ("NATIVE_UFS_GPT: private queue installed ba=%llx\n", listPhys);

  UfsQueue queue = { hc, list, ucd, data, ucdPhys, dataPhys, dataCapacity, false };

  IOLog ("NATIVE_UFS_NOP: settle spin before first command\n");
  spinTicks (192000);
  IOLog ("NATIVE_UFS_NOP: settle complete\n");
  if (!ufsNop (&queue)) {
    panic ("NATIVE_UFS_GPT: firmware-page NOP failed; see OCS/type log");
  }
  IOLog ("NATIVE_UFS_GPT: firmware-page NOP completed\n");

  const unsigned maxLun = 6;
  uint64_t lastLba[maxLun];
  uint32_t blockSize[maxLun];
  uint32_t lunStatus[maxLun];
  for (unsigned lun = 0; lun < maxLun; lun++) {
    lastLba[lun] = 0;
    blockSize[lun] = 0;
    lunStatus[lun] = 0xee;
    uint8_t cap16[16] = { 0x9e, 0x10 };
    putBe32 (cap16 + 10, 32);
    if (ufsCommand (&queue, static_cast<uint8_t> (lun), cap16, 32, data)) {
      lastLba[lun] = getBe64 (data);
      blockSize[lun] = getBe32 (data + 8);
      lunStatus[lun] = 0;
      continue;
    }
    uint8_t cap10[16] = { 0x25 };
    if (ufsCommand (&queue, static_cast<uint8_t> (lun), cap10, 8, data)) {
      lastLba[lun] = getBe32 (data);
      blockSize[lun] = getBe32 (data + 4);
      lunStatus[lun] = 0;
    }
  }
  for (unsigned lun = 0; lun < maxLun; lun++) {
    IOLog ("NATIVE_UFS_GPT: L%u st=%02x last=%llx bs=%x\n", lun,
           lunStatus[lun], lastLba[lun], blockSize[lun]);
  }
  if (lunStatus[0] || blockSize[0] != 4096) {
    panic ("NATIVE_UFS_GPT: LUN0 capacity failed status=%x last=%llx bs=%x",
           lunStatus[0], lastLba[0], blockSize[0]);
  }
  IOOptionBits gptOptions = static_cast<IOOptionBits> (kIODirectionInOut) |
                             static_cast<IOOptionBits> (kIOMemoryPhysicallyContiguous) |
                             static_cast<IOOptionBits> (kIOMemoryMapperNone);
  IOBufferMemoryDescriptor *gptDma = IOBufferMemoryDescriptor::inTaskWithPhysicalMask (
    kernel_task, gptOptions, blockSize[0], 0x00000000ffffffffULL);
  if (!gptDma) {
    IOLog ("NATIVE_UFS_GPT: GPT low DMA failed; high fallback\n");
    gptDma = IOBufferMemoryDescriptor::inTaskWithOptions (
      kernel_task, gptOptions, blockSize[0], 4096);
  }
  if (!gptDma) {
    panic ("NATIVE_UFS_GPT: GPT DMA allocation failed");
  }
  uint8_t *gptData = static_cast<uint8_t *> (gptDma->getBytesNoCopy ());
  IOByteCount gptContiguous = 0;
  uint64_t gptPhys = gptDma->getPhysicalSegment (0, &gptContiguous, kIOMemoryMapperNone);
  if (!gptData || !gptPhys || (gptPhys & 0xfff) || gptContiguous < blockSize[0]) {
    panic ("NATIVE_UFS_GPT: GPT DMA invalid phys=%llx cont=%llx", gptPhys,
           uint64_t (gptContiguous));
  }
  data = gptData;
  dataPhys = gptPhys;
  dataCapacity = blockSize[0];
  queue.data = data;
  queue.dataPhys = dataPhys;
  queue.dataCapacity = dataCapacity;
  IOLog ("NATIVE_UFS_GPT: GPT data phys=%llx size=%x\n", gptPhys, dataCapacity);

  uint8_t read10[16] = { 0x28 };
  putBe32 (read10 + 2, 1);
  read10[7] = 0;
  read10[8] = 1;
  if (!ufsCommand (&queue, 0, read10, blockSize[0], data)) {
    panic ("NATIVE_UFS_GPT: LBA1 header READ10 failed");
  }
  const uint8_t *header = data;
  uint32_t signature0 = getLe32 (header);
  uint32_t signature1 = getLe32 (header + 4);
  uint32_t headerCrc = getLe32 (header + 16);
  uint32_t entriesCrc = getLe32 (header + 88);
  uint64_t currentLba = getLe64 (header + 24);
  uint64_t backupLba = getLe64 (header + 32);
  uint64_t firstUsable = getLe64 (header + 40);
  uint64_t lastUsable = getLe64 (header + 48);
  uint64_t entriesLba = getLe64 (header + 72);
  uint32_t entryCount = getLe32 (header + 80);
  uint32_t entrySize = getLe32 (header + 84);
  if (signature0 != 0x20494645U || signature1 != 0x54524150U ||
      entrySize < 128 || entrySize > 512 || !entryCount || entryCount > 128 ||
      entriesLba > 0xffffffffULL) {
    panic ("NATIVE_UFS_GPT: bad GPT sig=%08x/%08x n=%u esz=%u elba=%llx",
           signature0, signature1, entryCount, entrySize, entriesLba);
  }
  uint32_t entryBytes = entryCount * entrySize;
  uint32_t entryBlocks = (entryBytes + blockSize[0] - 1) / blockSize[0];
  if (entryBlocks * blockSize[0] > dataCapacity) {
    panic ("NATIVE_UFS_GPT: GPT entries too large n=%u esz=%u", entryCount, entrySize);
  }
  memset (read10, 0, sizeof (read10));
  read10[0] = 0x28;
  putBe32 (read10 + 2, static_cast<uint32_t> (entriesLba));
  read10[7] = static_cast<uint8_t> (entryBlocks >> 8);
  read10[8] = static_cast<uint8_t> (entryBlocks);
  if (!ufsCommand (&queue, 0, read10, entryBlocks * blockSize[0], data)) {
    panic ("NATIVE_UFS_GPT: GPT entries READ10 failed");
  }

  char text[4096];
  int used = 0;
#define ADD(...) do { \
  if (used < int (sizeof (text))) { \
    int _n = snprintf (text + used, size_t (sizeof (text) - size_t (used)), __VA_ARGS__); \
    if (_n > 0) used += _n; \
    if (used >= int (sizeof (text))) used = int (sizeof (text)) - 1; \
  } \
} while (0)
  ADD ("NATIVE_UFS_GPT warm-link read-only\n");
  ADD ("cap=%08x ver=%08x is=%08x hcs=%08x hce=%08x\n",
       cap, version, is, hcs, hce);
  ADD ("queue=%llx ucd=%llx data=%llx cap=%x\n", listPhys, ucdPhys, dataPhys, dataCapacity);
  for (unsigned lun = 0; lun < maxLun; lun++) {
    ADD ("L%u st=%02x last=%llx bs=%x\n", lun, lunStatus[lun],
         lastLba[lun], blockSize[lun]);
  }
  ADD ("GPT hdr=%08x ent=%08x n=%u esz=%u\n",
       headerCrc, entriesCrc, entryCount, entrySize);
  ADD ("cur=%llx bak=%llx usable=%llx..%llx\n",
       currentLba, backupLba, firstUsable, lastUsable);
  const char *const keys[] = {
    "userdata_ios", "persist", "cache", "misc", "metadata", "system", "vendor",
    "boot", "recovery", "modem", "modemst1", "modemst2", "fsg", "fsc",
    "keymaster", "abl", "xbl", "tz", "devcfg", "devinfo"
  };
  uint64_t rootFirst = 0, rootLast = 0;
  unsigned listed = 0;
  for (uint32_t index = 0; index < entryCount; index++) {
    const uint8_t *entry = data + index * entrySize;
    uint64_t first = getLe64 (entry + 32);
    uint64_t last = getLe64 (entry + 40);
    if (!first) {
      continue;
    }
    char name[40];
    unsigned length = 0;
    for (unsigned c = 0; c < 36 && c < entrySize - 56; c += 2) {
      uint8_t lo = entry[56 + c];
      uint8_t hi = entry[57 + c];
      if (!lo && !hi) {
        break;
      }
      name[length++] = (hi == 0 && lo >= 32 && lo < 127) ? char (lo) : '?';
    }
    name[length] = 0;
    if (!__builtin_strcmp (name, "userdata_ios")) {
      rootFirst = first;
      rootLast = last;
    }
    bool key = false;
    for (unsigned k = 0; k < sizeof (keys) / sizeof (keys[0]); k++) {
      if (!__builtin_strcmp (name, keys[k])) {
        key = true;
        break;
      }
    }
    if (!key) {
      continue;
    }
    uint64_t mib = ((last - first + 1) * blockSize[0]) >> 20;
    ADD ("%s %llx-%llx %lluM\n", name, first, last, mib);
    listed++;
  }
  ADD ("partitions=%u listed=%u\n", entryCount, listed);
  IOLog ("%s", text);
  IOLog ("NATIVE_UFS_ROOT: userdata_ios LBA %llx..%llx\n", rootFirst, rootLast);

  IOOptionBits readOptions = static_cast<IOOptionBits> (kIODirectionInOut) |
                             static_cast<IOOptionBits> (kIOMemoryPhysicallyContiguous) |
                             static_cast<IOOptionBits> (kIOMemoryMapperNone);
  IOBufferMemoryDescriptor *readDma = IOBufferMemoryDescriptor::inTaskWithPhysicalMask (
    kernel_task, readOptions, UFS_DATA_MAX, 0x00000000ffffffffULL);
  if (!readDma) {
    IOLog ("NATIVE_UFS_GPT: read DMA low failed; high fallback\n");
    readDma = IOBufferMemoryDescriptor::inTaskWithOptions (
      kernel_task, readOptions, UFS_DATA_MAX, 4096);
  }
  if (!readDma) {
    panic ("NATIVE_UFS_GPT: read DMA allocation failed");
  }
  uint8_t *readData = static_cast<uint8_t *> (readDma->getBytesNoCopy ());
  IOByteCount readContiguous = 0;
  uint64_t readPhys = readDma->getPhysicalSegment (0, &readContiguous, kIOMemoryMapperNone);
  if (!readData || !readPhys || (readPhys & 0xfff) ||
      readContiguous < UFS_DATA_MAX) {
    panic ("NATIVE_UFS_GPT: read DMA invalid phys=%llx cont=%llx", readPhys,
           uint64_t (readContiguous));
  }
  queue.data = readData;
  queue.dataPhys = readPhys;
  queue.dataCapacity = UFS_DATA_MAX;
  queue.poisoned = false;
  fQueue = queue;

  IOOptionBits logOptions = static_cast<IOOptionBits> (kIODirectionIn) |
                            static_cast<IOOptionBits> (kIOMemoryPhysicallyContiguous) |
                            static_cast<IOOptionBits> (kIOMemoryMapperNone);
  IOBufferMemoryDescriptor *logDma = IOBufferMemoryDescriptor::inTaskWithPhysicalMask (
    kernel_task, logOptions, UFS_DATA_MAX, 0x00000000ffffffffULL);
  if (!logDma) {
    logDma = IOBufferMemoryDescriptor::inTaskWithOptions (
      kernel_task, logOptions, UFS_DATA_MAX, 4096);
  }
  if (logDma) {
    fLogDma = logDma;
    fLogData = static_cast<uint8_t *> (logDma->getBytesNoCopy ());
    IOByteCount logContiguous = 0;
    fLogDataPhys = logDma->getPhysicalSegment (0, &logContiguous, kIOMemoryMapperNone);
    if (!fLogData || !fLogDataPhys || (fLogDataPhys & 0xfff) ||
        logContiguous < UFS_DATA_MAX) {
      IOLog ("NATIVE_UFS_LOG: log DMA invalid data=%p phys=%llx cont=%llx\n",
             fLogData, (unsigned long long) fLogDataPhys,
             (unsigned long long) logContiguous);
      fLogData = nullptr;
      fLogDataPhys = 0;
    }
  } else {
    IOLog ("NATIVE_UFS_LOG: log DMA allocation failed\n");
  }
  fListMap = listMap;
  fUcdMap = ucdMap;
  fLock = IOLockAlloc ();
  if (!fLock) {
    panic ("NATIVE_UFS_GPT: read lock allocation failed");
  }
  fReady = true;
  char rootName[4][33] = {};
  uint64_t rootOffset[4] = {};
  uint64_t rootSize[4] = {};
  uint64_t rootImageBytes = 0;
  unsigned rootWindows = 0;
  {
    uint8_t header[4096] = {};
    uint8_t readUserdata[16] = { 0x28 };
    putBe32 (readUserdata + 2, static_cast<uint32_t> (rootFirst));
    readUserdata[8] = 1;
    if (ufsCommand (&queue, 0, readUserdata, sizeof (header), header)) {
      uint32_t magic = getLe32 (header);
      uint32_t magic2 = getLe32 (header + 4);
      uint32_t version = getLe32 (header + 8);
      uint32_t windows = getLe32 (header + 12);
      uint64_t imageBytes = getLe64 (header + 16);
      IOLog ("NATIVE_UFS_ROOT: userdata_ios magic=%08x/%08x ver=%u windows=%u image=%llx\n",
             magic, magic2, version, windows, imageBytes);
      if (magic == 0x474e4853U && magic2 == 0x544f4f52U && windows <= 4) {
        rootImageBytes = imageBytes;
        for (uint32_t index = 0; index < windows; index++) {
          const uint8_t *entry = header + 24 + index * 128;
          char name[33];
          memcpy (name, entry, 32);
          name[32] = 0;
          uint64_t offset = getLe64 (entry + 32);
          uint64_t size = getLe64 (entry + 40);
          strlcpy (rootName[index], name, sizeof (rootName[index]));
          rootOffset[index] = offset;
          rootSize[index] = size;
          rootWindows++;
          IOLog ("NATIVE_UFS_ROOT: window[%u]=%s off=%llx size=%llx\n",
                 index, name, offset, size);
        }
      } else {
        IOLog ("NATIVE_UFS_ROOT: SHNGROOT header not recognized\n");
      }
    } else {
      IOLog ("NATIVE_UFS_ROOT: userdata first-block READ10 failed\n");
    }
  }
  IOLog ("NATIVE_UFS_GPT: read DMA phys=%llx size=%x; publishing LUN0\n",
         readPhys, UFS_DATA_MAX);

  bool published = false;
  NativeUfsLun *child = new NativeUfsLun;
  if (child && child->init (nullptr) &&
      child->configure (this, 0, lastLba[0], blockSize[0]) &&
      child->attach (this) && child->start (this)) {
    IOLog ("NATIVE_UFS_PUB: before LUN0 register\n");
    child->registerService ();
    IOLog ("NATIVE_UFS_PUB: after LUN0 register\n");
    published = true;
    IOLog ("NATIVE_UFS_GPT: published LUN0 last=%llx bs=%x read-only\n",
           lastLba[0], blockSize[0]);
  } else {
    IOLog ("NATIVE_UFS_GPT: LUN0 publication failed\n");
  }
  if (child) {
    child->release ();
  }

  unsigned windowsPublished = 0;
  for (unsigned index = 0; index < rootWindows; index++) {
    uint64_t absolute = rootFirst * 4096ULL + rootOffset[index];
    IOLog ("NATIVE_UFS_ROOT: preparing window[%u]=%s abs=%llx bytes=%llx\n",
           index, rootName[index], absolute, rootSize[index]);
    NativeUfsWindow *window = new NativeUfsWindow;
    bool started = window && window->init (nullptr) &&
                   window->configure (this, rootName[index], absolute,
                                      rootSize[index]) &&
                   window->attach (this) && window->start (this);
    if (started) {
      IOLog ("NATIVE_UFS_PUB: before window[%u]=%s register\n",
             index, rootName[index]);
      window->registerService ();
      IOLog ("NATIVE_UFS_PUB: after window[%u]=%s register\n",
             index, rootName[index]);
      windowsPublished++;
      IOLog ("NATIVE_UFS_ROOT: published window[%u]=%s abs=%llx bytes=%llx\n",
             index, rootName[index], absolute, rootSize[index]);
      // Keep the reference: the window owns its bounce buffer and lock.
    } else {
      IOLog ("NATIVE_UFS_ROOT: window[%u]=%s publication failed\n",
             index, rootName[index]);
      if (window) {
        window->release ();
      }
    }
  }
  IOLog ("NATIVE_UFS_ROOT_MEDIA: post-publication snapshot\n");
  dumpRootMedia ();
  IOLog ("NATIVE_UFS_GPT: start returning published=%u windows=%u\n",
         published ? 1 : 0, windowsPublished);
  gH103Quiet = 1;
  gH103Probe = this;
  uint32_t skipPattern = 0;
  PE_parse_boot_argn ("h103-skip-pattern", &skipPattern, sizeof skipPattern);
  gH103SkipPattern = skipPattern ? 1 : 0;
  uint32_t slotMode = 1;
  PE_parse_boot_argn ("h103-slot", &slotMode, sizeof slotMode);
  gH103SlotMode = slotMode <= 2 ? static_cast<int> (slotMode) : 1;
  // H106: ios-sweet 0069's flush interval (seconds), 1..60.
  uint32_t logSecs = 10;
  PE_parse_boot_argn ("h103-log-secs", &logSecs, sizeof logSecs);
  gH103LogSecs = (logSecs >= 1 && logSecs <= 60) ? logSecs : 10;
  // H104f: do not touch userdata_ios until backboardd has actually exec'd.
  // The early slot writes contend with driverkitd/backboardd's UFS reads and
  // make the launchd ordering race lose; slot0 boots are stable. The message
  // buffer keeps accumulating, so the first post-backboardd write still dumps
  // the early boot log. H106 keeps that gate and adds the ios-sweet 0069
  // cadence: one delta write per h103-log-secs, and none while quiet.
  uint32_t hbTimer = 0;
  PE_parse_boot_argn ("h103-hb-timer", &hbTimer, sizeof hbTimer);
  gH103HbTimerOn = hbTimer <= 2 ? static_cast<int> (hbTimer) : 1;
  uint32_t loadIogpu = 0;
  PE_parse_boot_argn ("h103-load-iogpu", &loadIogpu, sizeof loadIogpu);
  gH103LoadIogpu = loadIogpu ? 1 : 0;
  gH103StartTicks = mach_absolute_time ();
  IOLog ("NATIVE_UFS_HB: slotMode=%d logSecs=%u hbTimer=%d loadIogpu=%d\n",
         gH103SlotMode, gH103LogSecs, gH103HbTimerOn, gH103LoadIogpu);
  thread_t heartbeat = nullptr;
  kern_return_t heartbeatRet = kernel_thread_start (heartbeatThread, nullptr, &heartbeat);
  IOLog ("NATIVE_UFS_HB: start rc=%x skipPattern=%u\n",
         heartbeatRet, gH103SkipPattern);
  if (heartbeatRet == KERN_SUCCESS) {
    thread_deallocate (heartbeat);
  }
  if (gH103SlotMode) {
    thread_t slotThread = nullptr;
    IOLog ("NATIVE_UFS_HB: slot start\n");
    kern_return_t slotRet = kernel_thread_start (h103SlotThread, nullptr, &slotThread);
    IOLog ("NATIVE_UFS_HB: slot rc=%x\n", slotRet);
    if (slotRet == KERN_SUCCESS) {
      thread_deallocate (slotThread);
    }
  } else {
    IOLog ("NATIVE_UFS_HB: slot disabled\n");
  }
  // H109: the 1 Hz heartbeat and the dump wakeup come from a timer_call (timer
  // interrupt), because thread timers never fire on this port. The worker
  // thread only sleeps on an explicit wakeup.
  thread_t dumpThread = nullptr;
  kern_return_t dumpRet = kernel_thread_start (h109WorkerThread, nullptr, &dumpThread);
  IOLog ("NATIVE_TICK: worker rc=%x\n", dumpRet);
  if (dumpRet == KERN_SUCCESS) {
    thread_deallocate (dumpThread);
  }
  uint32_t tickEnabled = 1;
  PE_parse_boot_argn ("h109-tick", &tickEnabled, sizeof tickEnabled);
  gH110TickEnabled = tickEnabled ? 1 : 0;
  uint32_t gicProbe = 1;
  PE_parse_boot_argn ("h114-gic", &gicProbe, sizeof gicProbe);
  gH114GicProbe = gicProbe ? 1 : 0;
  if (!gH110TickEnabled) {
    IOLog ("NATIVE_TICK: timer disabled\n");
  } else {
    gH109TimerCall = timer_call_alloc (h109TimerFunc, nullptr);
    if (gH109TimerCall) {
      timer_call_enter (gH109TimerCall, mach_absolute_time () + 19200000ULL, 0);
      IOLog ("NATIVE_TICK: timer armed\n");
    } else {
      IOLog ("NATIVE_TICK: timer alloc failed\n");
    }
  }
  if (gH103HbTimerOn == 1) {
    fHbLoop = IOWorkLoop::workLoop ();
    if (fHbLoop) {
      fHbTimer = IOTimerEventSource::timerEventSource (this,
                                                       &NativeUfsProbe::hbTimerFired);
      if (fHbTimer && fHbLoop->addEventSource (fHbTimer) == kIOReturnSuccess) {
        fHbNext = mach_absolute_time () + 19200000ULL;
        fHbTimer->wakeAtTime (fHbNext);
        IOLog ("NATIVE_UFS_HB_TIMER: start loop=%p timer=%p\n", fHbLoop, fHbTimer);
      } else {
        IOLog ("NATIVE_UFS_HB_TIMER: add failed loop=%p timer=%p\n",
               fHbLoop, fHbTimer);
        if (fHbTimer) {
          fHbTimer->release ();
          fHbTimer = nullptr;
        }
      }
    } else {
      IOLog ("NATIVE_UFS_HB_TIMER: no workloop\n");
    }
  } else if (gH103HbTimerOn == 2) {
    gH103TimerCall = timer_call_alloc (h103TimerCallFunc, nullptr);
    if (gH103TimerCall) {
      timer_call_enter (gH103TimerCall, mach_absolute_time () + 19200000ULL, 0);
      IOLog ("NATIVE_UFS_HB_TIMER: timer_call start %p\n", gH103TimerCall);
    } else {
      IOLog ("NATIVE_UFS_HB_TIMER: timer_call alloc failed\n");
    }
  }

  // Persistent log window: the last 32 MiB of userdata_ios. Write one
  // snapshot synchronously from start (the root path is already proven) and
  // another from the first 64 MiB of window reads. Neither depends on the
  // heartbeat thread or on a separately created log thread.
  fLogBlocks = H103_LOG_BLOCKS;
  fLogFirst = rootLast >= H103_LOG_BLOCKS ? rootLast + 1 - H103_LOG_BLOCKS : 0;
  fLogImageBytes = rootImageBytes;
  uint32_t logMode = 0;
  PE_parse_boot_argn ("sheng-log", &logMode, sizeof logMode);
  bool logSafe = logMode == 1 && fLogFirst && rootFirst &&
                 fLogFirst >= rootFirst &&
                 fLogFirst + fLogBlocks <= rootLast + 1;
  IOLog ("NATIVE_UFS_LOG: setup mode=%u first=%llx blocks=%u root=%llx..%llx "
         "image=%llx safe=%u\n",
         logMode, (unsigned long long) fLogFirst, fLogBlocks,
         (unsigned long long) rootFirst, (unsigned long long) rootLast,
         (unsigned long long) rootImageBytes, logSafe ? 1 : 0);
  gH103LogMode = logMode;
  gH103LogFirst = fLogFirst;
  gH103RootFirst = rootFirst;
  gH103RootLast = rootLast;
  if (logSafe) {
    IOLog ("NATIVE_UFS_LOG: window lba=%llx blocks=%u image=%llx\n",
           (unsigned long long) fLogFirst, fLogBlocks,
           (unsigned long long) fLogImageBytes);
    // Do not write from start(): the synchronous 1 MiB snapshot raced the
    // early LUN/root reads and faulted in readBlocks. The first window read
    // snapshot runs after root, on the storage request path.
    IOLog ("NATIVE_UFS_LOG: deferred snapshot to window read\n");
    gH103LogFirst = fLogFirst;
  } else {
    IOLog ("NATIVE_UFS_LOG: disabled mode=%u first=%llx root=%llx..%llx image=%llx\n",
           logMode, (unsigned long long) fLogFirst,
           (unsigned long long) rootFirst, (unsigned long long) rootLast,
           (unsigned long long) rootImageBytes);
    fLogFirst = 0;
    fLogBlocks = 0;
    gH103LogFirst = 0;
  }
  return published;
}
