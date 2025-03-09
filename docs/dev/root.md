# How does ADB root/unroot work?

Every couple of months the question is asked to the OWNERS: "How does adb root/unroot work?". Every time, we have to
dig out the code to remember. Here is a doc to hopefully solve this problem.

## shell uid vs root uid

`adbd` always starts running as user `root`.  One of the first things the daemon does is to check
if it should drop its privileges to run as `shell` user. There are a few read-only properties involved in the decision.

```
ro.secure
ro.debuggable
```

On a `user` debug, these properties will never allow `adbd` to remain `root`. However, on `eng` and `userdebug` builds
they will.

## From CLI to restart

If adbd can remain `root`, it doesn't mean that it should. There is a second level decision dictated by the property
`service.adb.root`. If set to `1`, adbd remains `root`. Otherwise, it drops to `shell`.

The command `adb root` and `adb unroot` triggers adbd to write `service.adb.root` and restart.

The one catch is that `adbd` cannot call `exit(3)` right away since it must make sure the "success" message makes
it back to the caller on the host.

The trick is done by tagging any asocket associated with a `root`/`unroot` command to call `exit(3)` when the
asocket they run upon is closed (see `exit_on_close`).


## How adb restarts upon root/unroot

If `adbd` calls `exit(3)`, how does it restart itself? Since it is a critical process, `initd` notices that it is
gone and restarts it.

