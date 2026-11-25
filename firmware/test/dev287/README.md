# DEV-287 bench: RESET_PICO replay (power manager 0.4.1)

Runs on a Pi4 bench host with the brain on USB and the PicoHub J1 on a switchable plug (full power cut =
plug off + the Pi's USB hub off). Copy this folder to `~/dev/pico/sam287/` next to `pico.py`,
`rpc_call.py` and `sam285/sam285_conlog.py`; set `PLUG` (switch) and `IP` (brain) at the top of the scripts.

| file | what |
|---|---|
| `zz_sam287_flash.py` | push `picohub_287.bin` to the power manager over I2C (needs `/bl_x035_bench.py` on the brain) |
| `zz_sam287_w.py` | RESET_PICO **write only** (no result read) to the address the Conductor assigned |
| `zz_sam287_b.py` | same, followed at once by a GET_STATUS write (the reset must still happen) |
| `zz_sam287_api.py` | `hub.reset_pico()`, the product API (write + result read) |
| `sam287_trial.sh LABEL DRIVER` | full cut, wait for `/rpc`, driver, console 60 s, `/rpc` uptime |
| `sam287_api.sh LABELS...` | per label: full cut, `hub.reset_pico()`, console 40 s, wait up to 150 s for `/rpc` |
| `sam287_all.sh` | 5x write-only, 2x back-to-back, 5x `hub.reset_pico()` |

PASS (0.4.1): each trial one boot (one USB port open after the reset, uptime = time since the driver),
`/rpc` back, strikes 0. Before 0.4.1 the write-only trial is a boot loop (~12 USB opens / 60 s, no `/rpc`).
