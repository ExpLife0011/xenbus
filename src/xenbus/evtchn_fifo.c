/* Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, 
 * with or without modification, are permitted provided 
 * that the following conditions are met:
 * 
 * *   Redistributions of source code must retain the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer.
 * *   Redistributions in binary form must reproduce the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer in the documentation and/or other 
 *     materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#include <ntddk.h>
#include <stdarg.h>
#include <xen.h>
#include <util.h>

#include "evtchn_fifo.h"
#include "shared_info.h"
#include "fdo.h"
#include "dbg_print.h"
#include "assert.h"

#define MAX_HVM_VCPUS   128

typedef struct _XENBUS_EVTCHN_FIFO_CONTEXT {
    PXENBUS_FDO                     Fdo;
    KSPIN_LOCK                      Lock;
    LONG                            References;
    PMDL                            ControlBlockMdl[MAX_HVM_VCPUS];
    PMDL                            *EventPageMdl;
    ULONG                           EventPageCount;
    ULONG                           Head[EVTCHN_FIFO_MAX_QUEUES];
} XENBUS_EVTCHN_FIFO_CONTEXT, *PXENBUS_EVTCHN_FIFO_CONTEXT;

#define EVENT_WORDS_PER_PAGE    (PAGE_SIZE / sizeof (event_word_t))

#define XENBUS_EVTCHN_FIFO_TAG  'OFIF'

static FORCEINLINE PVOID
__EvtchnFifoAllocate(
    IN  ULONG   Length
    )
{
    return __AllocatePoolWithTag(NonPagedPool, Length, XENBUS_EVTCHN_FIFO_TAG);
}

static FORCEINLINE VOID
__EvtchnFifoFree(
    IN  PVOID   Buffer
    )
{
    ExFreePoolWithTag(Buffer, XENBUS_EVTCHN_FIFO_TAG);
}

static event_word_t *
EvtchnFifoEventWord(
    IN  PXENBUS_EVTCHN_FIFO_CONTEXT Context,
    IN  ULONG                       Port
    )
{
    ULONG                           Index;
    PMDL                            Mdl;
    event_word_t                    *EventWord;

    Index = Port / EVENT_WORDS_PER_PAGE;
    ASSERT3U(Index, <, Context->EventPageCount);

    Mdl = Context->EventPageMdl[Index];

    EventWord = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    ASSERT(EventWord != NULL);

    ASSERT3U(Port, >=, Index * EVENT_WORDS_PER_PAGE);
    Port -= Index * EVENT_WORDS_PER_PAGE;

    return &EventWord[Port];
}

static FORCEINLINE BOOLEAN
__EvtchnFifoTestFlag(
    IN  event_word_t    *EventWord,
    IN  ULONG           Flag
    )
{
    return (*EventWord & (1 << Flag)) ? TRUE : FALSE;
}

static FORCEINLINE BOOLEAN
__EvtchnFifoTestAndSetFlag(
    IN  event_word_t    *EventWord,
    IN  ULONG           Flag
    )
{
    return (InterlockedBitTestAndSet((LONG *)EventWord, Flag) != 0) ? TRUE : FALSE;
}

static FORCEINLINE BOOLEAN
__EvtchnFifoTestAndClearFlag(
    IN  event_word_t    *EventWord,
    IN  ULONG           Flag
    )
{
    return (InterlockedBitTestAndReset((LONG *)EventWord, Flag) != 0) ? TRUE : FALSE;
}

static FORCEINLINE VOID
__EvtchnFifoSetFlag(
    IN  event_word_t    *EventWord,
    IN  ULONG           Flag
    )
{
    (VOID) InterlockedBitTestAndSet((LONG *)EventWord, Flag);
}

static FORCEINLINE VOID
__EvtchnFifoClearFlag(
    IN  event_word_t    *EventWord,
    IN  ULONG           Flag
    )
{
    (VOID) InterlockedBitTestAndReset((LONG *)EventWord, Flag);
}

static FORCEINLINE ULONG
__EvtchnFifoUnlink(
    IN  event_word_t    *EventWord
    )
{
    LONG                Old;
    LONG                New;

    do {
        Old = *EventWord;

        // Clear linked bit and link value
        New = Old & ~((1 << EVTCHN_FIFO_LINKED) | EVTCHN_FIFO_LINK_MASK);
    } while (InterlockedCompareExchange((LONG *)EventWord, New, Old) != Old);

    return Old & EVTCHN_FIFO_LINK_MASK;
}

static NTSTATUS
EvtchnFifoExpand(
    IN  PXENBUS_EVTCHN_FIFO_CONTEXT Context,
    IN  ULONG                       Port
    )
{
    LONG                            Index;
    ULONG                           EventPageCount;
    PMDL                            *EventPageMdl;
    PMDL                            Mdl;
    ULONG                           Start;
    ULONG                           End;
    NTSTATUS                        status;

    Index = Port / EVENT_WORDS_PER_PAGE;
    ASSERT3U(Index, >=, (LONG)Context->EventPageCount);

    EventPageCount = Index + 1;
    EventPageMdl = __EvtchnFifoAllocate(sizeof (PMDL) * EventPageCount);

    status = STATUS_NO_MEMORY;
    if (EventPageMdl == NULL)
        goto fail1;

    for (Index = 0; Index < (LONG)Context->EventPageCount; Index++)
        EventPageMdl[Index] = Context->EventPageMdl[Index];

    Index = Context->EventPageCount;
    while (Index < (LONG)EventPageCount) {
        event_word_t        *EventWord;
        PFN_NUMBER          Pfn;
        PHYSICAL_ADDRESS    Address;

        Mdl = __AllocatePage();

        status = STATUS_NO_MEMORY;
        if (Mdl == NULL)
            goto fail2;

        EventWord = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        ASSERT(EventWord != NULL);

        for (Port = 0; Port < EVENT_WORDS_PER_PAGE; Port++)
            __EvtchnFifoSetFlag(&EventWord[Port], EVTCHN_FIFO_MASKED);

        Pfn = MmGetMdlPfnArray(Mdl)[0];

        status = EventChannelExpandArray(Pfn);
        if (!NT_SUCCESS(status))
            goto fail3;

        Address.QuadPart = (ULONGLONG)Pfn << PAGE_SHIFT;

        LogPrintf(LOG_LEVEL_INFO,
                  "EVTCHN_FIFO: EVENTARRAY[%u] @ %08x.%08x\n",
                  Index,
                  Address.HighPart,
                  Address.LowPart);

        EventPageMdl[Index++] = Mdl;
    }

    Start = Context->EventPageCount * EVENT_WORDS_PER_PAGE;
    End = (EventPageCount * EVENT_WORDS_PER_PAGE) - 1;

    Info("added ports [%08x - %08x]\n", Start, End);

    if (Context->EventPageMdl != NULL)
        __EvtchnFifoFree(Context->EventPageMdl);

    Context->EventPageMdl = EventPageMdl;
    Context->EventPageCount = EventPageCount;

    return STATUS_SUCCESS;

fail3:
    Error("fail3\n");

    __FreePage(Mdl);

fail2:
    Error("fail2\n");

    while (--Index >= (LONG)Context->EventPageCount) {
        Mdl = EventPageMdl[Index];

        __FreePage(Mdl);
    }

    __EvtchnFifoFree(EventPageMdl);

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

static VOID
EvtchnFifoContract(
    IN  PXENBUS_EVTCHN_FIFO_CONTEXT Context
    )
{
    LONG                            Index;

    Index = Context->EventPageCount;
    while (--Index >= 0) {
        PMDL    Mdl;

        Mdl = Context->EventPageMdl[Index];

        __FreePage(Mdl);
    }

    __EvtchnFifoFree(Context->EventPageMdl);

    Context->EventPageMdl = NULL;
    Context->EventPageCount = 0;
}

static BOOLEAN
EvtchnFifoPollPriority(
    IN  PXENBUS_EVTCHN_FIFO_CONTEXT Context,
    IN  evtchn_fifo_control_block_t *ControlBlock,
    IN  ULONG                       Priority,
    IN  PULONG                      Ready,
    IN  XENBUS_EVTCHN_ABI_EVENT     Event,
    IN  PVOID                       Argument
    )
{
    ULONG                           Head;
    ULONG                           Port;
    event_word_t                    *EventWord;
    BOOLEAN                         DoneSomething;

    Head = Context->Head[Priority];

    if (Head == 0) {
        KeMemoryBarrier();
        Head = ControlBlock->head[Priority];
    }

    Port = Head;
    EventWord = EvtchnFifoEventWord(Context, Port);

    Head = __EvtchnFifoUnlink(EventWord);

    if (Head == 0)
        *Ready &= ~(1ull << Priority);

    DoneSomething = FALSE;

    if (!__EvtchnFifoTestFlag(EventWord, EVTCHN_FIFO_MASKED) &&
        __EvtchnFifoTestFlag(EventWord, EVTCHN_FIFO_PENDING))
        DoneSomething = Event(Argument, Port);

    Context->Head[Priority] = Head;

    return DoneSomething;
}

static BOOLEAN
EvtchnFifoPoll(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context,
    IN  ULONG                       Cpu,
    IN  XENBUS_EVTCHN_ABI_EVENT     Event,
    IN  PVOID                       Argument
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    unsigned int                    vcpu_id = SystemVirtualCpuIndex(Cpu);
    PMDL                            Mdl;
    evtchn_fifo_control_block_t     *ControlBlock;
    ULONG                           Ready;
    ULONG                           Priority;
    BOOLEAN                         DoneSomething;

    Mdl = Context->ControlBlockMdl[vcpu_id];

    ControlBlock = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    ASSERT(ControlBlock != NULL);

    Ready = InterlockedExchange((LONG *)&ControlBlock->ready, 0);
    DoneSomething = FALSE;

    while (_BitScanReverse(&Priority, Ready)) {
        DoneSomething |= EvtchnFifoPollPriority(Context,
                                                ControlBlock,
                                                Priority,
                                                &Ready,
                                                Event,
                                                Argument);
        Ready |= InterlockedExchange((LONG *)&ControlBlock->ready, 0);
    }

    return DoneSomething;
}

static NTSTATUS
EvtchnFifoPortEnable(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context,
    IN  ULONG                       Port
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    KIRQL                           Irql;
    NTSTATUS                        status;

    KeAcquireSpinLock(&Context->Lock, &Irql);

    if (Port / EVENT_WORDS_PER_PAGE >= Context->EventPageCount) {
        status = EvtchnFifoExpand(Context, Port);

        if (!NT_SUCCESS(status))
            goto fail1;
    }

    KeReleaseSpinLock(&Context->Lock, Irql);

    return STATUS_SUCCESS;

fail1:
    Error("fail1 (%08x)\n", status);

    KeReleaseSpinLock(&Context->Lock, Irql);

    return status;
}

static VOID
EvtchnFifoPortAck(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context,
    IN  ULONG                       Port
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    event_word_t                    *EventWord;

    EventWord = EvtchnFifoEventWord(Context, Port);
    __EvtchnFifoClearFlag(&EventWord[Port], EVTCHN_FIFO_PENDING);
}

static VOID
EvtchnFifoPortMask(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context,
    IN  ULONG                       Port
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    event_word_t                    *EventWord;

    EventWord = EvtchnFifoEventWord(Context, Port);
    __EvtchnFifoSetFlag(&EventWord[Port], EVTCHN_FIFO_MASKED);
}

static BOOLEAN
EvtchnFifoPortUnmask(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context,
    IN  ULONG                       Port
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    event_word_t                    *EventWord;
    LONG                            Old;
    LONG                            New;

    EventWord = EvtchnFifoEventWord(Context, Port);

    // Clear masked bit, spinning if busy
    do {
        Old = *EventWord & ~(1 << EVTCHN_FIFO_BUSY);
        New = Old & ~(1 << EVTCHN_FIFO_MASKED);
    } while (InterlockedCompareExchange((LONG *)EventWord, New, Old) != Old);

    // Check whether the port was masked
    if (~Old & (1 << EVTCHN_FIFO_MASKED))
        return FALSE;

    // If we cleared the mask then check whether something is pending
    if (!__EvtchnFifoTestAndClearFlag(EventWord, EVTCHN_FIFO_PENDING))
        return FALSE;

    return TRUE;
}

static VOID
EvtchnFifoPortDisable(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context,
    IN  ULONG                       Port
    )
{
    EvtchnFifoPortMask(_Context, Port);
}

static VOID
EvtchnFifoReset(
    IN  PXENBUS_EVTCHN_FIFO_CONTEXT Context
    )
{
    ULONGLONG                       Value;
    ULONG                           LocalPort;
    ULONG                           RemotePort;
    USHORT                          RemoteDomain;
    NTSTATUS                        status;

    UNREFERENCED_PARAMETER(Context);

    status = HvmGetParam(HVM_PARAM_STORE_EVTCHN, &Value);
    ASSERT(NT_SUCCESS(status));

    LocalPort = (LONG)Value;

    //
    // When we reset the event channel ABI we will lose our
    // binding to the STORE event channel, which was set up
    // by the toolstack during domain build.
    // We need to get the binding back, so we must query the
    // remote domain and port, and then re-bind after the
    // reset.
    //

    status = EventChannelQueryInterDomain(LocalPort,
                                          &RemoteDomain,
                                          &RemotePort);
    ASSERT(NT_SUCCESS(status));

    LogPrintf(LOG_LEVEL_INFO, "EVTCHN_FIFO: RESET\n");
    (VOID) EventChannelReset();

    status = EventChannelBindInterDomain(RemoteDomain,
                                         RemotePort,
                                         &LocalPort);
    ASSERT(NT_SUCCESS(status));

    Value = LocalPort;

    status = HvmSetParam(HVM_PARAM_STORE_EVTCHN, Value);
    ASSERT(NT_SUCCESS(status));
}

static NTSTATUS
EvtchnFifoAcquire(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    KIRQL                           Irql;
    LONG                            Cpu;
    PMDL                            Mdl;
    NTSTATUS                        status;

    KeAcquireSpinLock(&Context->Lock, &Irql);

    if (Context->References++ != 0)
        goto done;

    Trace("====>\n");

    Cpu = 0;
    while (Cpu < KeNumberProcessors) {
        unsigned int        vcpu_id;
        PFN_NUMBER          Pfn;
        PHYSICAL_ADDRESS    Address;

        Mdl = __AllocatePage();

        status = STATUS_NO_MEMORY;
        if (Mdl == NULL)
            goto fail1;

        vcpu_id = SystemVirtualCpuIndex(Cpu);
        Pfn = MmGetMdlPfnArray(Mdl)[0];

        status = EventChannelInitControl(Pfn, vcpu_id);
        if (!NT_SUCCESS(status))
            goto fail2;

        Address.QuadPart = (ULONGLONG)Pfn << PAGE_SHIFT;

        LogPrintf(LOG_LEVEL_INFO,
                  "EVTCHN_FIFO: CONTROLBLOCK[%u] @ %08x.%08x\n",
                  vcpu_id,
                  Address.HighPart,
                  Address.LowPart);

        Context->ControlBlockMdl[vcpu_id] = Mdl;
        Cpu++;
    }

    Trace("<====\n");

done:
    KeReleaseSpinLock(&Context->Lock, Irql);

    return STATUS_SUCCESS;

fail2:
    __FreePage(Mdl);

fail1:
    Error("fail1 (%08x)\n", status);

    (VOID) EventChannelReset();

    while (--Cpu >= 0) {
        unsigned int    vcpu_id;

        vcpu_id = SystemVirtualCpuIndex(Cpu);

        Mdl = Context->ControlBlockMdl[vcpu_id];
        Context->ControlBlockMdl[vcpu_id] = NULL;

        __FreePage(Mdl);
    }

    --Context->References;
    ASSERT3U(Context->References, ==, 0);
    KeReleaseSpinLock(&Context->Lock, Irql);

    return status;
}

VOID
EvtchnFifoRelease(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT  _Context
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT     Context = (PVOID)_Context;
    KIRQL                           Irql;
    LONG                            Cpu;

    KeAcquireSpinLock(&Context->Lock, &Irql);

    if (--Context->References > 0)
        goto done;

    Trace("====>\n");

    EvtchnFifoReset(Context);

    EvtchnFifoContract(Context);

    Cpu = KeNumberProcessors;
    while (--Cpu >= 0) {
        unsigned int    vcpu_id;
        PMDL            Mdl;

        vcpu_id = SystemVirtualCpuIndex(Cpu);

        Mdl = Context->ControlBlockMdl[vcpu_id];
        Context->ControlBlockMdl[vcpu_id] = NULL;

        __FreePage(Mdl);
    }

    Trace("<====\n");

done:
    KeReleaseSpinLock(&Context->Lock, Irql);
}

static XENBUS_EVTCHN_ABI EvtchnAbiFifo = {
    NULL,
    EvtchnFifoAcquire,
    EvtchnFifoRelease,
    EvtchnFifoPoll,
    EvtchnFifoPortEnable,
    EvtchnFifoPortDisable,
    EvtchnFifoPortAck,
    EvtchnFifoPortMask,
    EvtchnFifoPortUnmask
};

NTSTATUS
EvtchnFifoInitialize(
    IN  PXENBUS_FDO                     Fdo,
    OUT PXENBUS_EVTCHN_ABI_CONTEXT      *_Context
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT    Context;
    NTSTATUS                            status;

    Trace("====>\n");

    Context = __EvtchnFifoAllocate(sizeof (XENBUS_EVTCHN_FIFO_CONTEXT));

    status = STATUS_NO_MEMORY;
    if (Context == NULL)
        goto fail1;

    KeInitializeSpinLock(&Context->Lock);

    Context->Fdo = Fdo;

    *_Context = (PVOID)Context;

    Trace("<====\n");

    return STATUS_SUCCESS;

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

VOID
EvtchnFifoGetAbi(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT      _Context,
    OUT PXENBUS_EVTCHN_ABI              Abi)
{
    *Abi = EvtchnAbiFifo;

    Abi->Context = (PVOID)_Context;
}

VOID
EvtchnFifoTeardown(
    IN  PXENBUS_EVTCHN_ABI_CONTEXT      _Context
    )
{
    PXENBUS_EVTCHN_FIFO_CONTEXT    Context = (PVOID)_Context;

    Trace("====>\n");

    Context->Fdo = NULL;

    RtlZeroMemory(&Context->Lock, sizeof (KSPIN_LOCK));

    ASSERT(IsZeroMemory(Context, sizeof (XENBUS_EVTCHN_FIFO_CONTEXT)));
    __EvtchnFifoFree(Context);

    Trace("<====\n");
}