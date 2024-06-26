/*!\file connect.c
 * BSD connect().
 */

/*  BSD sockets functionality for Watt-32 TCP/IP
 *
 *  Copyright (c) 1997-2002 Gisle Vanem <gvanem@yahoo.no>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. All advertising materials mentioning features or use of this software
 *     must display the following acknowledgement:
 *       This product includes software developed by Gisle Vanem
 *       Bergen, Norway.
 *
 *  THIS SOFTWARE IS PROVIDED BY ME (Gisle Vanem) AND CONTRIBUTORS ``AS IS''
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL I OR CONTRIBUTORS BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  Version
 *
 *  0.5 : Dec 18, 1997 : G. Vanem - created
 *  0.6 : Aug 06, 2002 : G. Vanem - added AF_INET6 support
 */

#include "socket.h"

#if defined(USE_BSD_API)

static int tcp_connect  (Socket *socket);
static int udp_connect  (Socket *socket);
static int raw_connect  (Socket *socket);

/*
 * connect()
 *  "connect" will attempt to open a connection on a foreign IP address and
 *  foreign port address.  This is achieved by specifying the foreign IP
 *  address and foreign port number in the "servaddr".
 */
int W32_CALL connect (int s, const struct sockaddr *servaddr, socklen_t addrlen)
{
  struct   sockaddr_in  *addr   = (struct sockaddr_in*) servaddr;
  struct   sockaddr_in6 *addr6  = (struct sockaddr_in6*) servaddr;
  struct   Socket       *socket = _socklist_find (s);
  volatile int rc, sa_len;
  BOOL     is_ip6;

  SOCK_PROLOGUE (socket, "\nconnect:%d", s);

  is_ip6 = (socket->so_family == AF_INET6);
  sa_len = is_ip6 ? sizeof(*addr6) : sizeof(*addr);

  if (_sock_chk_sockaddr(socket, servaddr, addrlen) < 0)
     return (-1);

  if (socket->so_type == SOCK_STREAM)
  {
    if (socket->so_state & SS_ISCONNECTED)
    {
      SOCK_DEBUGF ((", EISCONN"));
      SOCK_ERRNO (EISCONN);
      return (-1);
    }
    if (socket->so_options & SO_ACCEPTCONN)
    {
      SOCK_DEBUGF ((", EOPNOTSUPP (listen sock)"));
      SOCK_ERRNO (EOPNOTSUPP);
      return (-1);
    }
    if (!is_ip6 && IN_MULTICAST(ntohl(addr->sin_addr.s_addr)))
    {
      SOCK_DEBUGF ((", EINVAL (mcast)"));
      SOCK_ERRNO (EINVAL);
      return (-1);
    }
    else if (is_ip6 && IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr))
    {
      SOCK_DEBUGF ((", EINVAL (mcast)"));
      SOCK_ERRNO (EINVAL);
      return (-1);
    }
  }

  if (socket->remote_addr)
  {
    if (socket->so_type == SOCK_STREAM)
    {
      if (socket->so_state & SS_ISCONNECTING)
      {
        tcp_tick (NULL);

        if (_sock_pending_connect (socket))
        {
          SOCK_DEBUGF ((", EALREADY"));
          SOCK_ERRNO (EALREADY);
          return (-1);
        }

        if (socket->so_error != 0)
        {
          SOCK_DEBUGF ((", SO_ERROR: %s", short_strerror(socket->so_error)));
          SOCK_ERRNO (socket->so_error);
          socket->so_error = 0;
          return (-1);
        }
      }

      SOCK_DEBUGF ((", connect already done!"));
      SOCK_ERRNO (EISCONN);
      return (-1);
    }

    SOCK_DEBUGF ((", reconnecting"));

    /* No need to reconnect if same peer address/port.
     */
    if (!is_ip6)
    {
      const struct sockaddr_in *ra = (const struct sockaddr_in*)socket->remote_addr;

      if (addr->sin_addr.s_addr == ra->sin_addr.s_addr &&
          addr->sin_port        == ra->sin_port)
        return (0);
    }
#if defined(USE_IPV6)
    else
    {
      const struct sockaddr_in6 *ra    = (const struct sockaddr_in6*)socket->remote_addr;

      if (!memcmp(&addr6->sin6_addr, &ra->sin6_addr, sizeof(addr6->sin6_addr)) &&
          addr6->sin6_port == ra->sin6_port)
        return (0);
    }
#endif

    /* Clear any effect of previous ICMP errors etc.
     */
    socket->so_state &= ~(SS_CONN_REFUSED | SS_CANTSENDMORE | SS_CANTRCVMORE);
    socket->so_error  = 0;
  }
  else
  {
    socket->remote_addr = SOCK_CALLOC (sa_len);
    if (!socket->remote_addr)
    {
      SOCK_DEBUGF ((", ENOMEM (rem)"));
      SOCK_ERRNO (ENOMEM);
      return (-1);
    }
  }

#if defined(USE_IPV6)
  if (is_ip6)
  {
    struct sockaddr_in6 *ra = (struct sockaddr_in6*)socket->remote_addr;

    ra->sin6_family = AF_INET6;
    ra->sin6_port   = addr6->sin6_port;
    memcpy (&ra->sin6_addr, &addr6->sin6_addr, sizeof(ra->sin6_addr));
  }
  else
#endif
  {
    socket->remote_addr->sin_family = AF_INET;
    socket->remote_addr->sin_port   = addr->sin_port;
    socket->remote_addr->sin_addr   = addr->sin_addr;
  }

  if (!socket->local_addr)
  {
    SOCK_DEBUGF ((", auto-binding"));

    socket->local_addr = (struct sockaddr_in*) SOCK_CALLOC (sa_len);
    if (!socket->local_addr)
    {
      free (socket->remote_addr);
      socket->remote_addr = NULL;
      SOCK_DEBUGF ((", ENOMEM (loc)"));
      SOCK_ERRNO (ENOMEM);
      return (-1);
    }

#if defined(USE_IPV6)
    if (is_ip6)
    {
      struct sockaddr_in6 *la = (struct sockaddr_in6*)socket->local_addr;

      la->sin6_family = AF_INET6;
      la->sin6_port   = htons (find_free_port(0,TRUE));
      memcpy (&la->sin6_addr, &in6addr_my_ip, sizeof(la->sin6_addr));
    }
    else
#endif
    {
      socket->local_addr->sin_family      = AF_INET;
      socket->local_addr->sin_port        = htons (find_free_port(0,TRUE));
      socket->local_addr->sin_addr.s_addr = htonl (my_ip_addr);
    }
  }

  SOCK_DEBUGF ((", src/dest ports: %u/%u",
                ntohs(socket->local_addr->sin_port),
                ntohs(socket->remote_addr->sin_port)));


  /* Not safe to run sock_daemon() now
   */
  _sock_crit_start();

  /* Setup SIGINT handler now.
   */
  if (_sock_sig_setup() < 0)
  {
    SOCK_DEBUGF ((", EINTR"));
    SOCK_ERRNO (EINTR);
    _sock_crit_stop();
    return (-1);
  }

  switch (socket->so_type)
  {
    case SOCK_STREAM:
         rc = tcp_connect (socket);
         break;

    case SOCK_DGRAM:
         rc = udp_connect (socket);
         break;

    case SOCK_RAW:
         rc = raw_connect (socket);
         break;

    default:
         SOCK_ERRNO (EPROTONOSUPPORT);
         rc = -1;
         break;
  }

  _sock_sig_restore();
  _sock_crit_stop();

  return (rc);
}

/*
 * Handle SOCK_DGRAM connection. Always blocking in _arp_resolve()
 */
static int udp_connect (Socket *socket)
{
#if defined(USE_IPV6)
  if (socket->so_family == AF_INET6)
  {
    const struct sockaddr_in6 *la = (const struct sockaddr_in6*) socket->local_addr;
    const struct sockaddr_in6 *ra = (const struct sockaddr_in6*) socket->remote_addr;

    if (!_UDP6_open (socket, &ra->sin6_addr, la->sin6_port, ra->sin6_port))
    {
      SOCK_DEBUGF ((", no route (udp6)"));
      SOCK_ERRNO (EHOSTUNREACH);
      STAT (ip6stats.ip6s_noroute++);
      return (-1);
    }
  }
  else
#endif
  if (!_UDP_open (socket,
                  socket->remote_addr->sin_addr,
                  socket->local_addr->sin_port,
                  socket->remote_addr->sin_port))
  {
    /* errno already set in udp_open() */
    SOCK_DEBUGF ((", %s", socket->udp_sock->err_msg));
    return (-1);
  }

  socket->so_state &= ~SS_UNCONNECTED;
  socket->so_state |=  SS_ISCONNECTED;
  return (0);
}


/*
 * SOCK_RAW "connect" is very simple.
 */
static int raw_connect (Socket *socket)
{
  socket->so_state |= SS_ISCONNECTED;

  /* Note: _arp_resolve() is done in ip_transmit()
   */
  return (0);
}

/*
 * Check for catched signals
 */
static int chk_signals (void)
{
  if (_sock_sig_pending())
  {
    SOCK_DEBUGF ((", EINTR"));
    SOCK_ERRNO (EINTR);
    return (-1);
  }
  return (0);
}

/*
 * Handle SOCK_STREAM blocking connection. Or non-blocking connect
 * the first time connect() is called.
 */
static int tcp_connect (Socket *socket)
{
  DWORD timeout;
  int   status;

#if defined(USE_IPV6)
  if (socket->so_family == AF_INET6)
  {
    const struct sockaddr_in6 *la = (const struct sockaddr_in6*) socket->local_addr;
    const struct sockaddr_in6 *ra = (const struct sockaddr_in6*) socket->remote_addr;

    if (!_TCP6_open (socket, &ra->sin6_addr, la->sin6_port, ra->sin6_port))
    {
      /* errno already set in _TCP6_open() */
      SOCK_DEBUGF ((", %s", socket->tcp_sock->err_msg));
      return (-1);
    }
  }
  else
#endif
  if (!_TCP_open (socket,
                  socket->remote_addr->sin_addr,
                  socket->local_addr->sin_port,
                  socket->remote_addr->sin_port))
  {
    /* errno already set in tcp_open() */
    SOCK_DEBUGF ((", %s", socket->tcp_sock->err_msg));
    return (-1);
  }

  /* Don't let tcp_Retransmitter() kill this socket
   * before our `socket->timeout' expires
   */
  socket->tcp_sock->locflags |= LF_RCVTIMEO;

  /* We're here only when connect() is called the 1st time
   * (blocking or non-blocking socket).
   */
  socket->so_state |= SS_ISCONNECTING;
  socket->so_error = 0;

  if (socket->so_state & SS_NBIO)
  {
    /* Use default 'sock_delay' for timeout, since 'socket->timeout'
     * will be 0 for non-blocking sockets.
     */
    socket->nb_timer = set_timeout (1000 * sock_delay);

    SOCK_DEBUGF ((", EINPROGRESS (-1)"));
    SOCK_ERRNO (EINPROGRESS);
    return (-1);
  }

  timeout = set_timeout (1000 * socket->timeout);

  /* Handle blocking stream socket connect.
   * Maybe we should use select_s() instead ?
   * Maybe set LF_NOCLOSE for all BSD sockets?
   *
   * And make gcc 8+ shut up about this warning:
   *   connect.c:343:41: warning: cast between incompatible function types from
   *   'int (*)(void)' to 'int (*)(void *)' [-Wcast-function-type]
   *     343 |                        socket->timeout, (UserHandler)chk_signals,
   *         |                                         ^
   */
  W32_GCC_FUNC_TYPE_WARN_OFF()

  status = _ip_delay0 ((sock_type*)socket->tcp_sock,
                       socket->timeout, (UserHandler)chk_signals,
                       NULL);

  W32_GCC_FUNC_TYPE_WARN_DEF()

  if (socket->so_error != 0)
  {
    SOCK_DEBUGF ((", SO_ERROR: %s", socket->tcp_sock->err_msg ?
                  socket->tcp_sock->err_msg :
                  short_strerror (socket->so_error)));
    SOCK_ERRNO (socket->so_error);
    socket->so_error = 0;
    return (-1);
  }

  if (status < 0 && chk_timeout(timeout))
  {
    socket->so_state &= ~SS_ISCONNECTING;
    SOCK_DEBUGF ((", ETIMEDOUT"));
    SOCK_ERRNO (ETIMEDOUT);
    return (-1);
  }

  if (status < 0)
  {
    socket->so_state &= ~SS_ISCONNECTING;
    SOCK_DEBUGF ((", ECONNRESET"));
    SOCK_ERRNO (ECONNRESET);
    return (-1);
  }

  socket->so_state &= ~(SS_UNCONNECTED | SS_ISCONNECTING);
  socket->so_state |=  SS_ISCONNECTED;
  return (0);
}

#endif /* USE_BSD_API */
