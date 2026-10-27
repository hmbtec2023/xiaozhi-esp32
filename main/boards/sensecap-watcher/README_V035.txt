HMB|TEC Persona Light V0.3.5 - SSCMA inference stabilization
Replace main/boards/sensecap-watcher/sscma_camera.cc only.
- Keepalive is suppressed during active inference to avoid interfering with stream.
- 10-second watchdog restarts inference only if no 416x416 frames arrive.
- Start command return values are checked; failed starts are retried.
- Existing PTT photo coordination and detection-only display retained.
- Automatic chat start remains disabled.
Not compiled or hardware-tested in this environment.
