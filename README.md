## Watt-32 TCP/IP

[![Build Status](https://ci.appveyor.com/api/projects/status/github/gvanem/watt-32?branch=master&svg=true)](https://ci.appveyor.com/project/gvanem/watt-32)

Until I manage to create a proper `README.md` file, here is the
original [`README`](README) file.

And also the [`README.TOO`](README.TOO) file.

Refer the [`INSTALL`](INSTALL) file for how to build and use the Watt-32 library.

## Thanks

The Watt-32 project wouldn't be where it is today without the support
from people providing patches and bug-fixes. Here follows a (incomplete)
list of people that have contributed:

| Who                   | Where                         | What |
| :-------------------- | :---------------------------- | :----------------------------- |
| Erick Engelke         | <erick@engmail.uwaterloo.ca>  | Originator of WatTcp           |
| Greg Bredthauer       | <grb4@duke.edu>               | DHCP bug-fixes                 |
| Yves Ferrant          | <Yves-Ferrant@t-online.de>    | Reverse-resolve bug-fix        |
| Mike                  | <mikedos386@yahoo.co.uk>      | IPv4 fragment bug-fix          |
| Gundolf von Bachhaus  | <GBachhaus@gmx.net>           | A new ARP-handler, timers etc. |
| Robert Gentz          | <rgentz@asdis.de>             | Token-Ring support             |
| Vlad Erochine         | <vlad@paragon.ru>             | BSD-socket multicast           |
| Andreas Fisher        | <a.fischer@aicoss.de>         | TCP tx-buffer bug-fix          |
| Lars Brinkhoff        | <lars@nocrew.org>             | poll() implementation          |
| Steven Lawson         | <stevel@sdl.continet.com>     | tcp state-machine fixes        |
| Francisco Pastor      | <fpastor.etra-id@etra.es>     | memory leak in accept()        |
| Doug Kaufmann         | <dkaufman@rahul.net>          | Break handling, testing        |
| Ken Yap               | <ken@nlc.net.au>              | Ported ttcp/syslogd/rdate etc. |
| Riccardo De Agostini  | <riccardo.de.agostini@isielettronica.it> | Fixes and enhancement of pcconfig.c parser. DHCP transient config-hook. DHCP config to battery RAM.               |
| Jiří Malák            | ??                     | Many fixes for Watcom version. |
| J.W. Jagersma         | jwjagersma@gmail.com   | Many Pull Requests for fixes of the BSD-socket API, DHCP-client, fixes for djgpp and even MacOS! |
| Andrew Wu:            | Taiwan                      | The djgpp cross-compiler for Windows: https://github.com/andrewwutw/build-djgpp/releases/ |
