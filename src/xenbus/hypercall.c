/*
 * Copyright (c) 2011 Citrix Systems, Inc.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <ntddk.h>
#include <ntstrsafe.h>
#include <xen.h>
#include "OpenXTV4V\OpenXTV4V.h"
#include "OpenXTV4V\OpenXTV4VKernel.h"

#include "dbg_print.h"

#define	EPERM		 1	/* Operation not permitted */
#define	ENOENT		 2	/* No such file or directory */
#define	ESRCH		 3	/* No such process */
#define	EINTR		 4	/* Interrupted system call */
#define	EIO		     5	/* I/O error */
#define	ENXIO		 6	/* No such device or address */
#define	E2BIG		 7	/* Arg list too long */
#define	ENOEXEC		 8	/* Exec format error */
#define	EBADF		 9	/* Bad file number */
#define	ECHILD		10	/* No child processes */
#define	EAGAIN		11	/* Try again */
#define	ENOMEM		12	/* Out of memory */
#define	EACCES		13	/* Permission denied */
#define	EFAULT		14	/* Bad address */
#define	ENOTBLK		15	/* Block device required */
#define	EBUSY		16	/* Device or resource busy */
#define	EEXIST		17	/* File exists */
#define	EXDEV		18	/* Cross-device link */
#define	ENODEV		19	/* No such device */
#define	ENOTDIR		20	/* Not a directory */
#define	EISDIR		21	/* Is a directory */
//#define	EINVAL		22	/* Invalid argument */
#define	ENFILE		23	/* File table overflow */
#define	EMFILE		24	/* Too many open files */
#define	ENOTTY		25	/* Not a typewriter */
#define	ETXTBSY		26	/* Text file busy */
#define	EFBIG		27	/* File too large */
#define	ENOSPC		28	/* No space left on device */
#define	ESPIPE		29	/* Illegal seek */
#define	EROFS		30	/* Read-only file system */
#define	EMLINK		31	/* Too many links */
#define	EPIPE		32	/* Broken pipe */
#define	EDOM		33	/* Math argument out of domain of func */
#define	ERANGE		34	/* Math result not representable */
#define	EDEADLK		35	/* Resource deadlock would occur */
#define	ENAMETOOLONG	36	/* File name too long */
#define	ENOLCK		37	/* No record locks available */
#define	ENOSYS		38	/* Function not implemented */
#define	ENOTEMPTY	39	/* Directory not empty */
#define	ELOOP		40	/* Too many symbolic links encountered */
#define	EWOULDBLOCK	EAGAIN	/* Operation would block */
#define	ENOMSG		42	/* No message of desired type */
#define	EIDRM		43	/* Identifier removed */
#define	ECHRNG		44	/* Channel number out of range */
#define	EL2NSYNC	45	/* Level 2 not synchronized */
#define	EL3HLT		46	/* Level 3 halted */
#define	EL3RST		47	/* Level 3 reset */
#define	ELNRNG		48	/* Link number out of range */
#define	EUNATCH		49	/* Protocol driver not attached */
#define	ENOCSI		50	/* No CSI structure available */
#define	EL2HLT		51	/* Level 2 halted */
#define	EBADE		52	/* Invalid exchange */
#define	EBADR		53	/* Invalid request descriptor */
#define	EXFULL		54	/* Exchange full */
#define	ENOANO		55	/* No anode */
#define	EBADRQC		56	/* Invalid request code */
#define	EBADSLT		57	/* Invalid slot */

#define	EDEADLOCK	EDEADLK

#define	EBFONT		59	/* Bad font file format */
#define	ENOSTR		60	/* Device not a stream */
#define	ENODATA		61	/* No data available */
#define	ETIME		62	/* Timer expired */
#define	ENOSR		63	/* Out of streams resources */
#define	ENONET		64	/* Machine is not on the network */
#define	ENOPKG		65	/* Package not installed */
#define	EREMOTE		66	/* Object is remote */
#define	ENOLINK		67	/* Link has been severed */
#define	EADV		68	/* Advertise error */
#define	ESRMNT		69	/* Srmount error */
#define	ECOMM		70	/* Communication error on send */
#define	EPROTO		71	/* Protocol error */
#define	EMULTIHOP	72	/* Multihop attempted */
#define	EDOTDOT		73	/* RFS specific error */
#define	EBADMSG		74	/* Not a data message */
#define	EOVERFLOW	75	/* Value too large for defined data type */
#define	ENOTUNIQ	76	/* Name not unique on network */
#define	EBADFD		77	/* File descriptor in bad state */
#define	EREMCHG		78	/* Remote address changed */
#define	ELIBACC		79	/* Can not access a needed shared library */
#define	ELIBBAD		80	/* Accessing a corrupted shared library */
#define	ELIBSCN		81	/* .lib section in a.out corrupted */
#define	ELIBMAX		82	/* Attempting to link in too many shared libraries */
#define	ELIBEXEC	83	/* Cannot exec a shared library directly */
#define	EILSEQ		84	/* Illegal byte sequence */
#define	ERESTART	85	/* Interrupted system call should be restarted */
#define	ESTRPIPE	86	/* Streams pipe error */
#define	EUSERS		87	/* Too many users */
#define	ENOTSOCK	88	/* Socket operation on non-socket */
#define	EDESTADDRREQ	89	/* Destination address required */
#define	EMSGSIZE	90	/* Message too long */
#define	EPROTOTYPE	91	/* Protocol wrong type for socket */
#define	ENOPROTOOPT	92	/* Protocol not available */
#define	EPROTONOSUPPORT	93	/* Protocol not supported */
#define	ESOCKTNOSUPPORT	94	/* Socket type not supported */
#define	EOPNOTSUPP	95	/* Operation not supported on transport endpoint */
#define	EPFNOSUPPORT	96	/* Protocol family not supported */
#define	EAFNOSUPPORT	97	/* Address family not supported by protocol */
#define	EADDRINUSE	98	/* Address already in use */
#define	EADDRNOTAVAIL	99	/* Cannot assign requested address */
#define	ENETDOWN	100	/* Network is down */
#define	ENETUNREACH	101	/* Network is unreachable */
#define	ENETRESET	102	/* Network dropped connection because of reset */
#define	ECONNABORTED	103	/* Software caused connection abort */
#define	ECONNRESET	104	/* Connection reset by peer */
#define	ENOBUFS		105	/* No buffer space available */
#define	EISCONN		106	/* Transport endpoint is already connected */
#define	ENOTCONN	107	/* Transport endpoint is not connected */
#define	ESHUTDOWN	108	/* Cannot send after transport endpoint shutdown */
#define	ETOOMANYREFS	109	/* Too many references: cannot splice */
#define	ETIMEDOUT	110	/* Connection timed out */
#define	ECONNREFUSED	111	/* Connection refused */
#define	EHOSTDOWN	112	/* Host is down */
#define	EHOSTUNREACH	113	/* No route to host */
#define	EALREADY	114	/* Operation already in progress */
#define	EINPROGRESS	115	/* Operation now in progress */
#define	ESTALE		116	/* Stale NFS file handle */
#define	EUCLEAN		117	/* Structure needs cleaning */
#define	ENOTNAM		118	/* Not a XENIX named type file */
#define	ENAVAIL		119	/* No XENIX semaphores available */
#define	EISNAM		120	/* Is a named type file */
#define	EREMOTEIO	121	/* Remote I/O error */
#define	EDQUOT		122	/* Quota exceeded */

#define	ENOMEDIUM	123	/* No medium found */
#define	EMEDIUMTYPE	124	/* Wrong medium type */

static NTSTATUS
V4vFilterErrno(int err)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (err < 0) {
        switch (err) {
        case -EAGAIN:
            status = STATUS_RETRY;
            break;
        case -EINVAL:
            status = STATUS_INVALID_PARAMETER;
            break;
        case -ENOMEM:
            status = STATUS_NO_MEMORY;
            break;
        case -ENOSPC:
        case -EMSGSIZE:
            status = STATUS_BUFFER_OVERFLOW;
            break;
        case -ENOSYS:
            status = STATUS_NOT_IMPLEMENTED;
            break;
        case -ENOTCONN:
        case -ECONNREFUSED:
            status = STATUS_VIRTUAL_CIRCUIT_CLOSED;
            break;
        case -EFAULT:
        default:
            Error("send data fault - hypercall err: %d\n", err);
            status = STATUS_UNSUCCESSFUL;
        };       
    }

    return status;
}

static PVOID
GetHypercallPage(VOID)
{
    ULONG eax, ebx ='fool', ecx = 'beef', edx = 'dead',
        nr_hypercall_pages;
    PVOID res;
    unsigned i;

    if (!CheckXenHypervisor()) {
        TraceError (("cpuid says this isn't really Xen.\n"));
        return NULL;
    }

    XenCpuid(1, &eax, &ebx, &ecx, &edx);
    TraceVerbose (("Xen version %d.%d.\n", eax >> 16, eax & 0xffff));

    //
    // Get the number of hypercall pages and the MSR to use to tell the
    // hypervisor which guest physical pages were assigned.
    //

    XenCpuid(2, &nr_hypercall_pages, &ebx, &ecx, &edx);

    res = XmAllocateMemory(PAGE_SIZE * nr_hypercall_pages);

    if (res == NULL) {
        TraceError (("Cannot allocate %d pages for hypercall trampolines.\n", nr_hypercall_pages));
        return NULL;
    }

    //
    // For each page, get the guest physical address and pass it to
    // the hypervisor.
    //
    // Note: The low 12 bits of the address is used to pass the index
    // of the page within the hypercall area.
    //

    for (i = 0; i < nr_hypercall_pages; i++)
    {
        PHYSICAL_ADDRESS gpa;

        gpa = MmGetPhysicalAddress(((PCHAR)res) + (i << PAGE_SHIFT));
        _wrmsr(ebx, gpa.LowPart | i, gpa.HighPart);
    }

    return res;
}

#pragma warning(push)
#pragma warning(disable: 4731)

// TODO: Implement 64 bit version.

//push rdi
//push rsi
//mov rdi, rdx
//mov rax, qword ptr [hypercall_page]
//shl rcx, 5
//add rax, rcx
//mov rsi, r8
//mov rdx, r9
//mov r10, [rsp+arg_4]
//mov r8, [rsp+arg_5]
//mov r9, [rsp+arg_6]
//call rax
//pop rsi
//pop rdi
//ret

__declspec(inline) ULONG_PTR
__hypercall6(
    unsigned long ordinal,
    ULONG_PTR arg1,
    ULONG_PTR arg2,
    ULONG_PTR arg3,
    ULONG_PTR arg4,
    ULONG_PTR arg5,
    ULONG_PTR arg6)
{
    ULONG_PTR retval;
    ULONG_PTR addr = (ULONG_PTR)&hypercall_page[ordinal];

    _asm
    {
        mov edi, arg5;
        mov esi, arg4;
        mov edx, arg3;
        mov ecx, arg2;
        mov ebx, arg1;
        mov eax, addr;
        /* Handle ebp carefully */
        push ebp;
        push arg6;
        pop ebp;
        call eax;
        pop ebp;
        mov retval, eax;
    }
    return retval;
}
#pragma warning(pop)

#define __HYPERVISOR_v4v_op               39

extern ULONG_PTR __hypercall6(unsigned long ordinal,
                              ULONG_PTR arg1,
                              ULONG_PTR arg2,
                              ULONG_PTR arg3,
                              ULONG_PTR arg4,
                              ULONG_PTR arg5,
                              ULONG_PTR arg6);

#define _hypercall6(type, name, arg1, arg2, arg3, arg4, arg5, arg6) \
    ((type)__hypercall6(__HYPERVISOR_##name, (ULONG_PTR)arg1, (ULONG_PTR)arg2, (ULONG_PTR)arg3, (ULONG_PTR)arg4, (ULONG_PTR)arg5, (ULONG_PTR)arg6))

__declspec(inline) int
HYPERVISOR_v4v_op(
    unsigned int cmd, void *arg2, void *arg3, void *arg4, ULONG32 arg5, ULONG32 arg6)
{
    return _hypercall6(int, v4v_op, cmd, arg2, arg3, arg4, arg5, arg6);
}

NTSTATUS
V4vRegisterRing(XENV4V_RING *robj)
{
    int err;

    err = HYPERVISOR_v4v_op(V4VOP_register_ring, robj->ring, robj->pfnList, 0, 0, 0);
    if (err != 0) {
        Error("register ring failed - hypercall err: %d\n", err);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
V4vUnregisterRing(XENV4V_RING *robj)
{
    int err;

    err = HYPERVISOR_v4v_op(V4VOP_unregister_ring, robj->ring, 0, 0, 0, 0);
    if (err != 0) {
        Error("unregister ring failed - hypercall err: %d\n", err);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
V4vNotify(v4v_ring_data_t *ringData)
{
    int err;

    err = HYPERVISOR_v4v_op(V4VOP_notify, ringData, 0, 0, 0, 0);
    if (err != 0) {
        Error("notify ring data failed - hypercall err: %d\n", err);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
V4vSend(v4v_addr_t *src, v4v_addr_t *dest, ULONG32 protocol, VOID *buf, ULONG32 length, ULONG32 *writtenOut)
{
    int err;

    *writtenOut = 0;

    err = HYPERVISOR_v4v_op(V4VOP_send, src, dest, buf, length, protocol);
    if (err >= 0) {      
        *writtenOut = (ULONG32)err;
    }

    return V4vFilterErrno(err);
}

NTSTATUS
V4vSendVec(v4v_addr_t *src, v4v_addr_t *dest, v4v_iov_t *iovec, ULONG32 nent, ULONG32 protocol, ULONG32 *writtenOut)
{
    int err;

    *writtenOut = 0;

    err = HYPERVISOR_v4v_op(V4VOP_sendv, src, dest, iovec, nent, protocol);
    if (err >= 0) {      
        *writtenOut = (ULONG32)err;
    }

    return V4vFilterErrno(err);
}
