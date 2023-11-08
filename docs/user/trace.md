# How to enable adb traces for bug reports

> :warning: **This will enable tracing permanently**. These instructions are
 well suited for tools managing adb lifecycle (like Android Studio).
Once done, it is recommended to undoing the changes made here and then
restarting adb via `adb kill-server ; adb server`.

## 1. Set the environment variable

### On MacOS/Linux

Add the following line to `~/.bashrc` (.zshrc on MacOS 10.15+:W
).

```
ADB_TRACE=all
```

### On Windows

Add the global variable via the `System Properties` window.
In the `Advanced` tab, click on `Environment Variables`. Add the Variable/
Value to the `User variables` list. Alternatively, you can bring up the same
window by searching for "Edit Environment Variables".

## 2. Cycle adb server

Shutdown adb server via command `adb kill-server`. Close the current terminal,
open a new one, and start adb server via `adb server`.

## 3. Locate the log files

### On MacOS/Linux

The log files are located in `$TMPDIR` which is almost always `/tmp`. Log files
are created on a per uid basis, `adb.<UID>.log`.

### On Windows

The log files are located in `%TEMP%` which is often `C:\Users\<USERNAME>\AppData\Local\Temp`.
The filename is always `adb.log`.
