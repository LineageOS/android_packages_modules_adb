# Architecture of *ADB Trade-In Mode*

ADB can run in a specialized "trade-in mode" (TIM). This is a highly restricted ADB designed to
faciliate automated diagnostics. It is only activated during the SetUp Wizard (SUW) on user builds.

## Activation flow

The DeviceDiagnostics apk has a `BOOT_COMPLETE` broadcast receiver, which it uses to call into the
tradeinmode service (`ITradeInMode.start`). The service activates trade-in mode if the following
conditions are true:

1. ADB is disabled.
2. `ro.debuggable` is 0 (to avoid breaking userdebug testing).
3. The `USER_SETUP_COMPLETE` setting is 0.
4. The `DEVICE_PROVISIONED` setting is 0.
5. There is no active wifi connection.

If all of these conditions hold, `persist.adb.tradeinmode` is set to `1` and the `ADB_ENABLED`
setting is set to `1`.

When adbd subsequentily starts, it sees `persist.adb.tradeinmode` is set and lowers its SELinux
context to a highly restricted policy (`adb_tradeinmode`).  This policy restricts adbd to
effectively one command: `adb shell tradeinmode`. It also disables authorization.

`ITradeInMode` monitors conditions 3, 4, and 5 above and turns off ADB as soon as any become true.

If the device is rebooted, the persist property ensures that ADB will stay in trade-in mode.

## userdebug testing

On userdebug builds, TIM is not enabled by default since adb is already available. This means the
authorization dialog is still present. However, TIM can still be manually tested with the following
command sequence:
1. `adb root`
2. `adb shell setprop service.adb.tradeinmode 1`
3. `adb unroot`

Unlike user builds, if entering TIM fails, then userdebug adbd will simply restart without TIM
enabled.

## Trade-In Mode commands

When ADB is in trade-in mode (the default in SUW when ro.debuggable is 0), the only allowed command
is `adb shell tradeinmode` plus arguments. On userdebug or eng builds, `adb root` is also allowed.

The tradeinmode shell command has two arguments:
 - `getstatus [-challenge CHALLENGE]`: Returns diagnostic information about the device, optionally
   with an attestation challenge.
 - `evaluate`: Bypasses setup and enters Android in an evaluation mode. A factory reset is forced
   on next boot.

## Evaluation mode

Evaluation mode is entered via `adb shell tradeinmode evaluate`. This changes
`persist.adb.tradeinmode` to `2` and restarts adbd. adbd then starts normally, without trade-in
mode restrictions. However, authorization is disabled. The device is factory reset on next boot.
This mode allows further diagnostics via normal adb commands (such as adb install).

## Factory reset

The factory reset is guaranteed by `ITradeInModeService.enterEvaluationMode` which writes a marker
to `/metadata/tradeinmode/wipe`. If first-stage init sees this file, it immediately reboots into
recovery to issue an unprompted wipe.

## persist.adb.tradeinmode values
 - `-1`: Failed to start TIM.
 - `0`: TIM is not enabled.
 - `1`: TIM is enabled.
 - `2`: "adb shell tradeinmode evaluate" was used, which enables adbd past SUW but
        also guarantees a factory reset on reboot.
