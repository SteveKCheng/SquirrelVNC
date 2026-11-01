# Command-line arguments for SquirrelVNC

*This information was extracted from the embedded help text in the TightVNC viewer.*

SquirrelVNC allows you to view and control remote hosts where a VNC server is running.

To start the viewer and connect to the specified host:

```
SquirrelVNC hostname::port [OPTIONS]
SquirrelVNC -host=hostname [OPTIONS]
SquirrelVNC -optionsfile=FILENAME [OPTIONS]
```

The host may be a name, an IPv4 address, or an IPv6 address in brackets.
After it comes either `:display`, a display number from 0 to 99 added to 5900, 
or `::port`, the port itself:

```
SquirrelVNC myhost:1
SquirrelVNC 192.168.0.5::5901
SquirrelVNC [fd00::5]:1
SquirrelVNC [fe80::1%12]::5901
```

The brackets around an IPv6 address are required in this form, since
`fe80::1` without them means host `fe80`, port 1. A link-local address
(`fe80::…`) needs the number of the network interface after `%`, as
`ipconfig` shows it. The `-host` option names a host only, and does not
need brackets: `-host=fd00::5 -port=5901`.

Listening mode, below, accepts connections over IPv4 only.

Start the viewer in the listening mode:

```
SquirrelVNC -listen
```

Show this help on command line usage:

```
SquirrelVNC -help
```

The following `OPTIONS` are supported.  All of them should be used in the
form `-option=VALUE`, where both option name and value are case-insensitive:

| Option | Description |
| - | - |
| `-optionsfile` | The specified file will be read and the corresponding connection will be restored. Use Save Connection Info to create such files. |
| `-host` | Connect to the specified host name or address (IPv6 with or without brackets). |
| `-port` | Specify the port number. |
| `-password` | Specify the password for authenticating to the server. |
| `-showcontrols` | Show toolbar. |
| `-viewonly` | View only (input ignored). |
| `-useclipboard` | Use clipboard (yes/no). |
| `-scale` | Scale by (auto, 50, 100, 150...). |
| `-fullscreen` | Full-screen mode. |
| `-warnfullscr` | Show full-screen warning. |
| `-encoding` |  (hextile, tight, rre, zrle). |
| `-copyrect` | . |
| `-mousecursor` |  (no, local). |
| `-mouselocal` | (no, snalldot, normal). |
| `-mouseswap` | Swap right and middle buttons. |
| `-jpegimagequality` | Set Jpeg image quality [0..9]. |
| `-compressionlevel` | Set custom compression level [0..9]. |
| `-logpath` | Specify the path to logfile. |

