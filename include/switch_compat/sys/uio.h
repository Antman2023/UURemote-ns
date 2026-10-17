#pragma once
#include <sys/_iovec.h>
#include <sys/types.h>
#ifndef UIO_MAXIOV
#define UIO_MAXIOV 1024
#endif
#ifndef ERESTART
#define ERESTART (-1)
#endif
#ifndef CMSG_ALIGN
#define CMSG_ALIGN(n) (((n) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#endif
