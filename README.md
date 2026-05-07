# Telegram Feed (TGF.C)

`TGF.C` is *A minimalist Telegram channel aggregator and forwarder. Consolidate all your favorite sources into one unified, distraction-free feed.*

`Rewritten In C` From [TGF](https://github.com/Hadi493/tgf)


## Dependencies

- `libtdjson` (TDLib)
- `libuv`
- `sqlite3`
- `cJSON` (included in vendor)
- `toml-c` (included in vendor)
- `sha256` (included in vendor)

## Quick Start:

Configure `.env` and `config.toml` first.

```bash
gcc nob.c -o nob
```
```bash
./nob
# or
./nob run
```
```bash
./tgf
```
