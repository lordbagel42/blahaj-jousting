# Device Registry — Blahaj Jousting

Each node firmware prints a standardized identity banner at boot (see
`lib/blahaj_common/self_id.h`). Reset a board and grep its serial for `[ID]`:

```
[ID] role=CLIENT id=1 axis=THROTTLE mac=50:02:91:FC:1D:EC
```

To re-identify every attached board at once:

```bash
for p in /dev/ttyUSB*; do
  python - "$p" <<'PY'
import sys, time, serial, re
s = serial.Serial(sys.argv[1], 115200, timeout=0.3)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); s.reset_input_buffer()
t = time.time(); buf = b""
while time.time() - t < 4: buf += s.read(256)
m = re.search(rb"\[ID\][^\n]*", buf)
print(sys.argv[1], m.group(0).decode() if m else "no [ID]")
PY
done
```

## Known boards

MAC is the stable hardware identifier; the serial port (`/dev/ttyUSB*`) is only
stable while the USB topology is unchanged.

| Role    | Device ID | Axis     | MAC                 | Firmware env      |
|---------|-----------|----------|---------------------|-------------------|
| SERVER  | 254       | –        | `10:52:1C:EA:5D:00` | `server_strip`    |
| REFEREE | 254       | –        | `1C:C3:AB:BF:CC:D4` | `referee`         |
| CAR     | 1         | –        | `F4:CF:A2:75:CF:58` | `car`             |
| CLIENT  | 1         | THROTTLE | `50:02:91:FC:1D:EC` | `client_throttle` |
| CLIENT  | 1         | STEERING | `A4:CF:12:B0:2E:2D` | `client_steering` |

Both CLIENT halves share `DEVICE_ID=1` (the logical controller); the referee
shows them as `Ctrl #1A` (steering) and `Ctrl #1B` (throttle). The two halves
wire to the same joystick module — the THROTTLE unit's A0 to the Y wiper, the
STEERING unit's A0 to the X wiper. If the axes feel swapped, either swap those
two wires or reflash the two units with the opposite `client_*` env.
