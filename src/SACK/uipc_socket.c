/* $NetBSD: uipc_socket.c,v 1.21.4.2 1996/12/11 03:27:58 mycroft Exp $ */

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * This product includes software developed by the University of
 * California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)uipc_socket.c 8.3 (Berkeley) 4/15/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/resourcevar.h>

#ifdef TCP_AUTO
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

/*
 * Socket operation routines.
 * These routines are called by the routines in
 * sys_socket.c or from a system process, and
 * implement the semantics of socket operations by
 * switching out to the protocol specific routines.
 */
int socreate (int dom, struct socket **aso, int type, int proto)
{
  struct proc    *p = curproc;     /* XXX */
  struct protosw *prp;
  struct socket  *so;
  int    error;

  if (proto)
       prp = pffindproto (dom, proto, type);
  else prp = pffindtype (dom, type);

  if (prp == 0 || prp->pr_usrreq == 0)
     return (EPROTONOSUPPORT);

  if (prp->pr_type != type)
     return (EPROTOTYPE);

  MALLOC (so, struct socket *, sizeof (*so), M_SOCKET, M_WAIT);
  bzero ((caddr_t) so, sizeof (*so));
  so->so_type  = type;
  so->so_proto = prp;

  error = (*prp->pr_usrreq) (so, PRU_ATTACH, NULL,
                             (struct mbuf*)proto, NULL, p);
  if (error)
  {
    so->so_state |= SS_NOFDREF;
    sofree (so);
    return (error);
  }
  *aso = so;
  return (0);
}

int sobind (struct socket *so, struct mbuf *nam)
{
  struct proc *p = curproc;     /* XXX */
  int    s       = splsoftnet();
  int    error   = (*so->so_proto->pr_usrreq) (so, PRU_BIND, NULL,
                                               nam, NULL, p);
  splx (s);
  return (error);
}

int solisten (struct socket *so, int backlog)
{
  int s = splsoftnet();
  int error = (*so->so_proto->pr_usrreq) (so, PRU_LISTEN, NULL,
                                          NULL, NULL, NULL);
  if (error)
  {
    splx (s);
    return (error);
  }
  if (so->so_q == 0)
     so->so_options |= SO_ACCEPTCONN;
  if (backlog < 0)
     backlog = 0;
  so->so_qlimit = min (backlog, SOMAXCONN);
  splx (s);
  return (0);
}

void sofree (struct socket *so)
{
  if (so->so_pcb || (so->so_state & SS_NOFDREF) == 0)
     return;

  if (so->so_head)
  {
    if (!soqremque (so, 0) && !soqremque (so, 1))
       panic ("sofree dq");
    so->so_head = 0;
  }
  sbrelease (&so->so_snd);
  sorflush (so);
  FREE (so, M_SOCKET);
}

/*
 * Close a socket on last file table reference removal.
 * Initiate disconnect if connected.
 * Free socket when disconnect complete.
 */
int soclose (struct socket *so)
{
  int s = splsoftnet();        /* conservative */
  int error = 0;

  if (so->so_options & SO_ACCEPTCONN)
  {
    while (so->so_q0)
        soabort (so->so_q0);
    while (so->so_q)
        soabort (so->so_q);
  }
  if (so->so_pcb == 0)
     goto discard;

  if (so->so_state & SS_ISCONNECTED)
  {
    if ((so->so_state & SS_ISDISCONNECTING) == 0)
    {
      error = sodisconnect (so);
      if (error)
         goto drop;
    }
    if (so->so_options & SO_LINGER)
    {
      if ((so->so_state & SS_ISDISCONNECTING) && (so->so_state & SS_NBIO))
         goto drop;

      while (so->so_state & SS_ISCONNECTED)
      {
        error = tsleep ((caddr_t)&so->so_timeo, PSOCK | PCATCH,
                        netcls, so->so_linger);
        if (error)
           break;
      }
    }
  }
drop:
  if (so->so_pcb)
  {
    int error2 = (*so->so_proto->pr_usrreq) (so, PRU_DETACH,
                                             NULL, NULL, NULL, NULL);

    if (error == 0)
      error = error2;
  }
discard:
  if (so->so_state & SS_NOFDREF)
     panic ("soclose: NOFDREF");
  so->so_state |= SS_NOFDREF;
  sofree (so);
  splx (s);
  return (error);
}

/*
 * Must be called at splsoftnet...
 */
int soabort (struct socket *so)
{
  return (*so->so_proto->pr_usrreq) (so, PRU_ABORT, NULL,
                                     NULL, NULL, NULL);
}

int soaccept (struct socket *so, struct mbuf *nam)
{
  int s = splsoftnet();
  int error;

  if ((so->so_state & SS_NOFDREF) == 0)
    panic ("soaccept: !NOFDREF");
  so->so_state &= ~SS_NOFDREF;
  error = (*so->so_proto->pr_usrreq) (so, PRU_ACCEPT, NULL, nam, NULL, NULL);
  splx (s);
  return (error);
}

int soconnect (struct socket *so, struct mbuf *nam)
{
  struct proc *p = curproc;     /* XXX */
  int    s, error;

  if (so->so_options & SO_ACCEPTCONN)
     return (EOPNOTSUPP);

  s = splsoftnet();

  /*
   * If protocol is connection-based, can only connect once.
   * Otherwise, if connected, try to disconnect first.
   * This allows user to disconnect by connecting to, e.g.,
   * a null address.
   */
  if (so->so_state & (SS_ISCONNECTED | SS_ISCONNECTING) &&
      ((so->so_proto->pr_flags & PR_CONNREQUIRED) || /* connection oriented proto */
       ((error = sodisconnect (so))) != 0))          /* connectionless proto */
       error = EISCONN;
  else error = (*so->so_proto->pr_usrreq) (so,PRU_CONNECT,NULL,nam,NULL,p);
  splx (s);
  return (error);
}

int soconnect2 (struct socket *so1, struct socket *so2)
{
  int s     = splsoftnet();
  int error = (*so1->so_proto->pr_usrreq) (so1, PRU_CONNECT2,
                                           NULL, (struct mbuf*)so2,
                                           NULL, NULL);
  splx (s);
  return (error);
}

int sodisconnect (struct socket *so)
{
  int s = splsoftnet();
  int error;

  if ((so->so_state & SS_ISCONNECTED) == 0)
  {
    error = ENOTCONN;
    goto bad;
  }
  if (so->so_state & SS_ISDISCONNECTING)
  {
    error = EALREADY;
    goto bad;
  }
  error = (*so->so_proto->pr_usrreq) (so, PRU_DISCONNECT,
                                      NULL, NULL, NULL, NULL);
bad:
  splx (s);
  return (error);
}

#define	SBLOCKWAIT(f)	(((f) & MSG_DONTWAIT) ? M_NOWAIT : M_WAITOK)
/*
 * Send on a socket.
 * If send must go all at once and message is larger than
 * send buffering, then hard error.
 * Lock against other senders.
 * If must go all at once and not enough room now, then
 * inform user that this would block and do nothing.
 * Otherwise, if nonblocking, send as much as possible.
 * The data to be sent is described by "uio" if nonzero,
 * otherwise by the mbuf chain "top" (which must be null
 * if uio is not).  Data provided in mbuf chain must be small
 * enough to send all at once.
 *
 * Returns nonzero on error, timeout or signal; callers
 * must check for short counts if EINTR/ERESTART are returned.
 * Data and control buffers are freed on return.
 */
int sosend (struct socket *so,  struct mbuf *addr, struct uio *uio,
            struct mbuf   *top, struct mbuf *control, int flags)
{
  struct proc  *p = curproc;     /* XXX */
  struct mbuf **mp;
  struct mbuf  *m;
  long   space, len, resid;
  int    clen = 0, error, s, dontroute, mlen;
  int    atomic = sosendallatonce (so) || top;

  if (uio)
       resid = uio->uio_resid;
  else resid = top->m_pkthdr.len;

  /*
   * In theory resid should be unsigned.
   * However, space must be signed, as it might be less than 0
   * if we over-committed, and we must use a signed comparison
   * of space and resid.  On the other hand, a negative resid
   * causes us to loop sending 0-length segments to the protocol.
   */
  if (resid < 0)
     return (EINVAL);

  dontroute = (flags & MSG_DONTROUTE)              &&
              (so->so_options & SO_DONTROUTE) == 0 &&
              (so->so_proto->pr_flags & PR_ATOMIC);
  p->p_stats->p_ru.ru_msgsnd++;
  if (control)
     clen = control->m_len;

#define snderr(errno)   { error = errno; \
                          splx(s);       \
                          goto release;  \
                        }

restart:
  if ((error = sblock (&so->so_snd, SBLOCKWAIT (flags))) != 0)
     goto out;

  do
  {
    s = splsoftnet();
    if (so->so_state & SS_CANTSENDMORE)
       snderr (EPIPE);

    if (so->so_error)
       snderr (so->so_error);

    if ((so->so_state & SS_ISCONNECTED) == 0)
    {
      if (so->so_proto->pr_flags & PR_CONNREQUIRED)
      {
        if ((so->so_state & SS_ISCONFIRMING) == 0 && !(resid == 0 && clen))
           snderr (ENOTCONN);
      }
      else if (addr == 0)
        snderr (EDESTADDRREQ);
    }
    space = sbspace (&so->so_snd);
    if (flags & MSG_OOB)
       space += 1024;

    if ((atomic && resid > so->so_snd.sb_hiwat) || clen > so->so_snd.sb_hiwat)
       snderr (EMSGSIZE);

    if (space < resid + clen && uio &&
        (atomic || space < so->so_snd.sb_lowat || space < clen))
    {
      if (so->so_state & SS_NBIO)
         snderr (EWOULDBLOCK);

      sbunlock (&so->so_snd);
      error = sbwait (&so->so_snd);
      splx (s);
      if (error)
         goto out;
      goto restart;
    }
    splx (s);
    mp = &top;
    space -= clen;
    do
    {
      if (uio == NULL)
      {
        /* Data is prepackaged in "top".
         */
        resid = 0;
        if (flags & MSG_EOR)
           top->m_flags |= M_EOR;
      }
      else
        do
        {
          if (top == 0)
          {
            MGETHDR (m, M_WAIT, MT_DATA);
            mlen = MHLEN;
            m->m_pkthdr.len = 0;
            m->m_pkthdr.rcvif = (struct ifnet *) 0;
          }
          else
          {
            MGET (m, M_WAIT, MT_DATA);
            mlen = MLEN;
          }
          if (resid >= MINCLSIZE && space >= MCLBYTES)
          {
            MCLGET (m, M_WAIT);
            if ((m->m_flags & M_EXT) == 0)
              goto nopages;
            mlen = MCLBYTES;
#ifdef MAPPED_MBUFS
            len = min (MCLBYTES, resid);
#else
            if (atomic && top == 0)
            {
              len = min (MCLBYTES - max_hdr, resid);
              m->m_data += max_hdr;
            }
            else
              len = min (MCLBYTES, resid);
#endif
            space -= len;
          }
          else
          {
          nopages:
            len = min (min (mlen, resid), space);
            space -= len;
            /*
             * For datagram protocols, leave room
             * for protocol headers in first mbuf.
             */
            if (atomic && top == 0 && len < mlen)
              MH_ALIGN (m, len);
          }
          error = uiomove (mtod (m, caddr_t), (int) len, uio);
          resid = uio->uio_resid;
          m->m_len = len;
          *mp = m;
          top->m_pkthdr.len += len;
          if (error)
            goto release;
          mp = &m->m_next;
          if (resid <= 0)
          {
            if (flags & MSG_EOR)
              top->m_flags |= M_EOR;
            break;
          }
        }
        while (space > 0 && atomic);

      if (dontroute)
        so->so_options |= SO_DONTROUTE;
      s = splsoftnet();        /* XXX */
      error = (*so->so_proto->pr_usrreq) (so, (flags & MSG_OOB) ?
                                          PRU_SENDOOB : PRU_SEND,
                                          top, addr, control, p);
      splx (s);
      if (dontroute)
        so->so_options &= ~SO_DONTROUTE;
      clen = 0;
      control = 0;
      top = 0;
      mp = &top;
      if (error)
        goto release;
    }
    while (resid && space > 0);
  }
  while (resid);

release:
  sbunlock (&so->so_snd);
out:
  if (top)
    m_freem (top);
  if (control)
    m_freem (control);
  return (error);
}

/*
 * Implement receive operations on a socket.
 * We depend on the way that records are added to the sockbuf
 * by sbappend*.  In particular, each record (mbufs linked through m_next)
 * must begin with an address if the protocol so specifies,
 * followed by an optional mbuf or mbufs containing ancillary data,
 * and then zero or more mbufs of data.
 * In order to avoid blocking network interrupts for the entire time here,
 * we splx() while doing the actual copy to user space.
 * Although the sockbuf is locked, new data may still be appended,
 * and thus we must maintain consistency of the sockbuf during that time.
 *
 * The caller may receive the data as a single mbuf chain by supplying
 * an mbuf **mp0 for use in returning the chain.  The uio is then used
 * only for the count in uio_resid.
 */
int soreceive (struct socket *so, struct mbuf **paddr, struct uio *uio,
               struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{
  struct mbuf    *m, **mp;
  struct protosw *pr = so->so_proto;
  struct mbuf    *nextrecord;
  int    flags, len, error, s, offset;
  int    moff, type = 0;
  int    orig_resid = uio->uio_resid;

  mp = mp0;
  if (paddr)
     *paddr = 0;

  if (controlp)
     *controlp = 0;

  if (flagsp)
       flags = *flagsp & ~MSG_EOR;
  else flags = 0;

  if (flags & MSG_OOB)
  {
    m = m_get (M_WAIT, MT_DATA);
    error = (*pr->pr_usrreq) (so, PRU_RCVOOB, m,
                              (struct mbuf*)(long)(flags & MSG_PEEK),
                              NULL, NULL);
    if (error)
      goto bad;
    do
    {
      error = uiomove (mtod(m, caddr_t), min (uio->uio_resid, m->m_len), uio);
      m = m_free (m);
    }
    while (uio->uio_resid && error == 0 && m);
  bad:
    if (m)
      m_freem (m);
    return (error);
  }
  if (mp)
     *mp = NULL;

  if (so->so_state & SS_ISCONFIRMING && uio->uio_resid)
    (*pr->pr_usrreq) (so, PRU_RCVD, NULL, NULL, NULL, NULL);

restart:
  if ((error = sblock (&so->so_rcv, SBLOCKWAIT (flags))) != 0)
     return (error);

  s = splsoftnet();

  m = so->so_rcv.sb_mb;
  /*
   * If we have less data than requested, block awaiting more
   * (subject to any timeout) if:
   *   1. the current count is less than the low water mark,
   *   2. MSG_WAITALL is set, and it is possible to do the entire
   * receive operation at once if we block (resid <= hiwat), or
   *   3. MSG_DONTWAIT is not set.
   * If MSG_WAITALL is set but resid is larger than the receive buffer,
   * we have to do the receive in sections, and thus risk returning
   * a short count if a timeout or signal occurs after we start.
   */
  if (m == 0 || (((flags & MSG_DONTWAIT) == 0 &&
                  so->so_rcv.sb_cc < uio->uio_resid) &&
                 (so->so_rcv.sb_cc < so->so_rcv.sb_lowat ||
                  ((flags & MSG_WAITALL) && uio->uio_resid <= so->so_rcv.sb_hiwat)) &&
                 m->m_nextpkt == 0 && (pr->pr_flags & PR_ATOMIC) == 0))
  {
#ifdef DIAGNOSTIC
    if (m == 0 && so->so_rcv.sb_cc)
      panic ("receive 1");
#endif
    if (so->so_error)
    {
      if (m)
        goto dontblock;
      error = so->so_error;
      if ((flags & MSG_PEEK) == 0)
        so->so_error = 0;
      goto release;
    }
    if (so->so_state & SS_CANTRCVMORE)
    {
      if (m)
        goto dontblock;
      else
        goto release;
    }
    for (; m; m = m->m_next)
      if (m->m_type == MT_OOBDATA || (m->m_flags & M_EOR))
      {
        m = so->so_rcv.sb_mb;
        goto dontblock;
      }
    if ((so->so_state & (SS_ISCONNECTED | SS_ISCONNECTING)) == 0 && (so->so_proto->pr_flags & PR_CONNREQUIRED))
    {
      error = ENOTCONN;
      goto release;
    }
    if (uio->uio_resid == 0)
      goto release;
    if ((so->so_state & SS_NBIO) || (flags & MSG_DONTWAIT))
    {
      error = EWOULDBLOCK;
      goto release;
    }
    sbunlock (&so->so_rcv);
    error = sbwait (&so->so_rcv);
    splx (s);
    if (error)
      return (error);
    goto restart;
  }

dontblock:
  nextrecord = m->m_nextpkt;
  if (pr->pr_flags & PR_ADDR)
  {
    orig_resid = 0;
    if (flags & MSG_PEEK)
    {
      if (paddr)
        *paddr = m_copy (m, 0, m->m_len);
      m = m->m_next;
    }
    else
    {
      sbfree (&so->so_rcv, m);
      if (paddr)
      {
        *paddr = m;
        so->so_rcv.sb_mb = m->m_next;
        m->m_next = 0;
        m = so->so_rcv.sb_mb;
      }
      else
      {
        MFREE (m, so->so_rcv.sb_mb);
        m = so->so_rcv.sb_mb;
      }
    }
  }
  while (m && m->m_type == MT_CONTROL && error == 0)
  {
    if (flags & MSG_PEEK)
    {
      if (controlp)
        *controlp = m_copy (m, 0, m->m_len);
      m = m->m_next;
    }
    else
    {
      sbfree (&so->so_rcv, m);
      if (controlp)
      {
        if (pr->pr_domain->dom_externalize &&
            mtod (m, struct cmsghdr *)->cmsg_type == SCM_RIGHTS)
           error = (*pr->pr_domain->dom_externalize) (m);

        *controlp = m;
        so->so_rcv.sb_mb = m->m_next;
        m->m_next = 0;
        m = so->so_rcv.sb_mb;
      }
      else
      {
        MFREE (m, so->so_rcv.sb_mb);
        m = so->so_rcv.sb_mb;
      }
    }
    if (controlp)
    {
      orig_resid = 0;
      controlp = &(*controlp)->m_next;
    }
  }
  if (m)
  {
    if ((flags & MSG_PEEK) == 0)
      m->m_nextpkt = nextrecord;
    type = m->m_type;
    if (type == MT_OOBDATA)
      flags |= MSG_OOB;
  }
  moff = 0;
  offset = 0;

  while (m && uio->uio_resid > 0 && error == 0)
  {
    if (m->m_type == MT_OOBDATA)
    {
      if (type != MT_OOBDATA)
        break;
    }
    else if (type == MT_OOBDATA)
      break;

    so->so_state &= ~SS_RCVATMARK;
    len = uio->uio_resid;
    if (so->so_oobmark && len > so->so_oobmark - offset)
       len = so->so_oobmark - offset;
    if (len > m->m_len - moff)
       len = m->m_len - moff;

    /*
     * If mp is set, just pass back the mbufs.
     * Otherwise copy them out via the uio, then free.
     * Sockbuf must be consistent here (points to current mbuf,
     * it points to next record) when we drop priority;
     * we must note any additions to the sockbuf when we
     * block interrupts again.
     */
    if (mp == 0)
    {
      splx (s);
      error = uiomove (mtod (m, caddr_t) + moff, (int) len, uio);
      s = splsoftnet();
    }
    else
      uio->uio_resid -= len;

    if (len == m->m_len - moff)
    {
      if (m->m_flags & M_EOR)
        flags |= MSG_EOR;
      if (flags & MSG_PEEK)
      {
        m = m->m_next;
        moff = 0;
      }
      else
      {
        nextrecord = m->m_nextpkt;
        sbfree (&so->so_rcv, m);
        if (mp)
        {
          *mp = m;
          mp = &m->m_next;
          so->so_rcv.sb_mb = m = m->m_next;
          *mp = (struct mbuf *) 0;
        }
        else
        {
          MFREE (m, so->so_rcv.sb_mb);
          m = so->so_rcv.sb_mb;
        }
        if (m)
          m->m_nextpkt = nextrecord;
      }
    }
    else
    {
      if (flags & MSG_PEEK)
        moff += len;
      else
      {
        if (mp)
          *mp = m_copym (m, 0, len, M_WAIT);
        m->m_data += len;
        m->m_len -= len;
        so->so_rcv.sb_cc -= len;
      }
    }
    if (so->so_oobmark)
    {
      if ((flags & MSG_PEEK) == 0)
      {
        so->so_oobmark -= len;
        if (so->so_oobmark == 0)
        {
          so->so_state |= SS_RCVATMARK;
          break;
        }
      }
      else
      {
        offset += len;
        if (offset == so->so_oobmark)
          break;
      }
    }
    if (flags & MSG_EOR)
       break;

    /*
     * If the MSG_WAITALL flag is set (for non-atomic socket),
     * we must not quit until "uio->uio_resid == 0" or an error
     * termination.  If a signal/timeout occurs, return
     * with a short count but without error.
     * Keep sockbuf locked against other readers.
     */
    while (flags & MSG_WAITALL && m == 0 && uio->uio_resid > 0 &&
           !sosendallatonce (so) && !nextrecord)
    {
      if (so->so_error || so->so_state & SS_CANTRCVMORE)
         break;
      error = sbwait (&so->so_rcv);
      if (error)
      {
        sbunlock (&so->so_rcv);
        splx (s);
        return (0);
      }
      if ((m = so->so_rcv.sb_mb) != NULL)
        nextrecord = m->m_nextpkt;
    }
  }

  if (m && pr->pr_flags & PR_ATOMIC)
  {
    flags |= MSG_TRUNC;
    if ((flags & MSG_PEEK) == 0)
       sbdroprecord (&so->so_rcv);
  }
  if ((flags & MSG_PEEK) == 0)
  {
    if (m == 0)
      so->so_rcv.sb_mb = nextrecord;
    if (pr->pr_flags & PR_WANTRCVD && so->so_pcb)
      (*pr->pr_usrreq) (so, PRU_RCVD, NULL,
                        (struct mbuf*)(long)flags, NULL, NULL);
  }
  if (orig_resid == uio->uio_resid && orig_resid &&
      (flags & MSG_EOR) == 0 && (so->so_state & SS_CANTRCVMORE) == 0)
  {
    sbunlock (&so->so_rcv);
    splx (s);
    goto restart;
  }

  if (flagsp)
     *flagsp |= flags;
release:
  sbunlock (&so->so_rcv);
  splx (s);
  return (error);
}

int soshutdown (struct socket *so, int how)
{
  struct protosw *pr = so->so_proto;

  how++;
  if (how & FREAD)
     sorflush (so);
  if (how & FWRITE)
     return (*pr->pr_usrreq) (so, PRU_SHUTDOWN, NULL, NULL, NULL, NULL);
  return (0);
}

void sorflush (struct socket *so)
{
  struct sockbuf *sb = &so->so_rcv;
  struct protosw *pr = so->so_proto;
  struct sockbuf  asb;
  int    s;

  sb->sb_flags |= SB_NOINTR;
  sblock (sb, M_WAITOK);
  s = splimp();
  socantrcvmore (so);
  sbunlock (sb);
  asb = *sb;
  bzero ((caddr_t) sb, sizeof (*sb));
  splx (s);
  if (pr->pr_flags & PR_RIGHTS && pr->pr_domain->dom_dispose)
    (*pr->pr_domain->dom_dispose) (asb.sb_mb);
  sbrelease (&asb);
}

int sosetopt (struct socket *so, int level, int optname, struct mbuf *m0)
{
  struct mbuf *m = m0;
  int    error = 0;

  if (level != SOL_SOCKET)
  {
    if (so->so_proto && so->so_proto->pr_ctloutput)
       return (*so->so_proto->pr_ctloutput) (PRCO_SETOPT, so, level,
                                             optname, &m0);
    error = ENOPROTOOPT;
  }
  else
  {
    switch (optname)
    {
      case SO_LINGER:
           if (m == NULL || m->m_len != sizeof (struct linger))
           {
             error = EINVAL;
             goto bad;
           }
           so->so_linger = mtod (m, struct linger *)->l_linger;

           /* fall thru... */

      case SO_DEBUG:
      case SO_KEEPALIVE:
      case SO_DONTROUTE:
      case SO_USELOOPBACK:
      case SO_BROADCAST:
      case SO_REUSEADDR:
      case SO_REUSEPORT:
      case SO_OOBINLINE:
           if (m == NULL || m->m_len < sizeof (int))
           {
             error = EINVAL;
             goto bad;
           }
           if (*mtod (m, int *))
                 so->so_options |= optname;

           else
             so->so_options &= ~optname;
           break;

      case SO_SNDBUF:
      case SO_RCVBUF:
      case SO_SNDLOWAT:
      case SO_RCVLOWAT:
           if (m == NULL || m->m_len < sizeof (int))
           {
             error = EINVAL;
             goto bad;
           }
           switch (optname)
           {
             case SO_SNDBUF:
             case SO_RCVBUF:
                  if (sbreserve (optname == SO_SNDBUF ? &so->so_snd :
                                 &so->so_rcv, (u_long) *mtod (m, int*)) == 0)
                  {
                    error = ENOBUFS;
                    goto bad;
                  }
#ifdef TCP_AUTO
                  /* turn off autotuning */
                  if (so->so_proto->pr_protocol == IPPROTO_TCP)
                  {
                    struct mbuf *mdata;

                    MGET (mdata, M_WAIT, MT_SOOPTS);
                    *mtod (mdata, int *) = (int) 0;
                    mdata->m_len = sizeof (int);

                    (*so->so_proto->pr_ctloutput) (PRCO_SETOPT, so,
                                                   IPPROTO_TCP, TCP_AUTO_OP,
                                                   &mdata);
                  }
#endif
                  break;

             case SO_SNDLOWAT:
                  so->so_snd.sb_lowat = *mtod (m, int *);
                  break;

             case SO_RCVLOWAT:
                  so->so_rcv.sb_lowat = *mtod (m, int *);
                  break;
           }
           break;

      case SO_SNDTIMEO:
      case SO_RCVTIMEO:
           {
             struct timeval *tv;
             short  val;

             if (m == NULL || m->m_len < sizeof (*tv))
             {
               error = EINVAL;
               goto bad;
             }
             tv = mtod (m, struct timeval *);

             if (tv->tv_sec * hz + tv->tv_usec / tick > SHRT_MAX)
             {
               error = EDOM;
               goto bad;
             }
             val = tv->tv_sec * hz + tv->tv_usec / tick;

             switch (optname)
             {
               case SO_SNDTIMEO:
                    so->so_snd.sb_timeo = val;
                    break;
               case SO_RCVTIMEO:
                    so->so_rcv.sb_timeo = val;
                    break;
             }
             break;
           }

      default:
           error = ENOPROTOOPT;
           break;
    }

    if (error == 0 && so->so_proto && so->so_proto->pr_ctloutput)
    {
      ((*so->so_proto->pr_ctloutput) (PRCO_SETOPT, so, level, optname, &m0));
      m = NULL;                 /* freed by protocol */
    }
  }
bad:
  if (m)
     m_free (m);
  return (error);
}

int sogetopt (struct socket *so, int level, int optname, struct mbuf **mp)
{
  struct mbuf *m;

  if (level != SOL_SOCKET)
  {
    if (so->so_proto && so->so_proto->pr_ctloutput)
       return (*so->so_proto->pr_ctloutput) (PRCO_GETOPT, so, level,
                                             optname, mp);
    return (ENOPROTOOPT);
  }

  m = m_get (M_WAIT, MT_SOOPTS);
  m->m_len = sizeof (int);

  switch (optname)
  {
    case SO_LINGER:
         m->m_len = sizeof (struct linger);
         mtod (m, struct linger *)->l_onoff = so->so_options & SO_LINGER;
         mtod (m, struct linger *)->l_linger = so->so_linger;
         break;

    case SO_USELOOPBACK:
    case SO_DONTROUTE:
    case SO_DEBUG:
    case SO_KEEPALIVE:
    case SO_REUSEADDR:
    case SO_REUSEPORT:
    case SO_BROADCAST:
    case SO_OOBINLINE:
         *mtod (m, int *) = so->so_options & optname;
         break;

    case SO_TYPE:
         *mtod (m, int *) = so->so_type;
         break;

    case SO_ERROR:
         *mtod (m, int *) = so->so_error;
         so->so_error = 0;
         break;

    case SO_SNDBUF:
         *mtod (m, int *) = so->so_snd.sb_hiwat;
         break;

    case SO_RCVBUF:
         *mtod (m, int *) = so->so_rcv.sb_hiwat;
         break;

    case SO_SNDLOWAT:
         *mtod (m, int *) = so->so_snd.sb_lowat;
         break;

    case SO_RCVLOWAT:
         *mtod (m, int *) = so->so_rcv.sb_lowat;
         break;

    case SO_SNDTIMEO:
    case SO_RCVTIMEO:
         {
           int val = (optname == SO_SNDTIMEO ? so->so_snd.sb_timeo :
                     so->so_rcv.sb_timeo);

           m->m_len = sizeof (struct timeval);
           mtod (m, struct timeval *)->tv_sec = val / hz;
           mtod (m, struct timeval *)->tv_usec = (val % hz) / tick;
           break;
         }

    default:
         m_free (m);
         return (ENOPROTOOPT);
  }
  *mp = m;
  return (0);
}

void sohasoutofband (struct socket *so)
{
  struct proc *p;

  if (so->so_pgid < 0)
     gsignal (-so->so_pgid, SIGURG);
  else if (so->so_pgid > 0 && (p = pfind (so->so_pgid)) != 0)
     psignal (p, SIGURG);
  selwakeup (&so->so_rcv.sb_sel);
}
