// Portability shims for the BSD-socket code.
//
// Linux spells two things as flags that macOS/BSD spell as separate calls:
//   SOCK_CLOEXEC / SOCK_NONBLOCK in socket()  -> fcntl() after the fact
//   MSG_NOSIGNAL in send()                    -> SO_NOSIGPIPE on the socket
// socketFd() and sendFlags() hide the difference; call noSigPipe() on sockets
// obtained some other way (accept()).
#pragma once
#include <fcntl.h>
#include <sys/socket.h>

namespace gtty::net {

// Suppress SIGPIPE for writes to this socket on platforms without MSG_NOSIGNAL.
inline void noSigPipe(int fd) {
#ifdef SO_NOSIGPIPE
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
  (void)fd;
#endif
}

// Flags for send() that keep a write to a closed peer from raising SIGPIPE.
inline constexpr int sendFlags() {
#ifdef MSG_NOSIGNAL
  return MSG_NOSIGNAL;
#else
  return 0;  // handled by SO_NOSIGPIPE in socketFd()/noSigPipe()
#endif
}

// socket() with close-on-exec and, optionally, O_NONBLOCK already set.
inline int socketFd(int family, int type, int protocol, bool nonblock = false) {
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
  int fd = ::socket(family, type | SOCK_CLOEXEC | (nonblock ? SOCK_NONBLOCK : 0), protocol);
  if (fd < 0) return fd;
#else
  int fd = ::socket(family, type, protocol);
  if (fd < 0) return fd;
  int fdFlags = ::fcntl(fd, F_GETFD);
  if (fdFlags >= 0) ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC);
  if (nonblock) {
    int flags = ::fcntl(fd, F_GETFL);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
#endif
  noSigPipe(fd);
  return fd;
}

}  // namespace gtty::net
