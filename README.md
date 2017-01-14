# serialfwd - simple tool for serial protocol debugging

*serialfwd* is a simple tool to debug (binary) serial protocols. It allows sending/receiving bytes, and can also act as a proxy to access a serial connection on a (embedded) target host via TCP/IP from a remote (development) host.

serialfwd's proxy mode is compatible with the *SerialComm* class in [p44utils](https://github.com/plan44/p44utils), which allows either connecting to a local serial port directly, but also can connect to a remote serialfwd runmning in proxy mode.

See "serialfwd -h" for how to use the tool.