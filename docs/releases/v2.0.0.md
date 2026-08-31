## mote v2.0.0

### Что нового

- **Поиск и замена** — обычный текст и **regex**
- **Quick open** — быстрый переход к файлу (`Ctrl+P`)
- **Закладки на строках** — F8 / Alt+B поставить, F9 / Ctrl+B / Alt+J перейти
- **Console TTY** — корректный разбор escape-последовательностей, modifyOtherKeys, защита строки статуса
- **Multi-arch релиз** — Linux (amd64, i686, arm64, armhf, riscv64), Windows (amd64, i686), DOS, WebAssembly

Сайт с фильтром по OS и CPU: **https://syfaren.github.io/mote-site/**

---

### Рекомендуемая загрузка

**[`mote-all-platforms.zip`](https://github.com/SYFaren/mote/releases/download/v2.0.0/mote-all-platforms.zip)** — все бинарники по папкам + `flat/` + `SHA256SUMS`.

#### Структура `by-platform/`

```
linux/amd64/{console,x11,sdl2,wayland,fbdev}/
linux/arm64|armhf|i686/{console,x11,sdl2}/
linux/riscv64/console/
windows/amd64|i686/{gui,console}/
dos/i686/
web/wasm/
```

В каждой папке: `mote` (или `mote.exe`), при наличии — `mote.upx`, `README.txt`.

---

### Linux

| CPU | Бэкенды | Flat-имена (примеры) |
|-----|---------|----------------------|
| **amd64** | console, x11, sdl2, wayland, fbdev | `mote-linux-console`, `mote-linux-x11`, … (legacy) и `mote-linux-amd64-*` |
| **arm64** | console, x11, sdl2 | `mote-linux-arm64-console`, … |
| **armhf** | console, x11, sdl2 | `mote-linux-armhf-console`, … |
| **i686** | console, x11, sdl2 | `mote-linux-i686-console`, … |
| **riscv64** | console | `mote-linux-riscv64-console` |

### Windows

| CPU | Бэкенды | Flat-имена |
|-----|---------|------------|
| **amd64** | gui, console | `mote-windows-gui.exe`, `mote-windows-console.exe` |
| **i686** | gui, console | `mote-windows-i686-gui.exe`, `mote-windows-i686-console.exe` |

### Прочее

| Файл | Описание |
|------|----------|
| `mote-dos.exe` | FreeDOS / DOSBox (DJGPP); рядом нужен `CWSDPMI.EXE` из zip |
| `mote-web.zip` | WebAssembly — распаковать, открыть `mote.html` через локальный HTTP |
| `*.upx` / `*.upx.exe` | UPX-сжатые варианты |
| `SHA256SUMS` | контрольные суммы всех flat-файлов |

---

**Важно:** Linux ELF не запускается на BSD/macOS. BSD-сборки — `make release-bsd` на FreeBSD/OpenBSD/NetBSD (пока не в этом zip).

ANSI C89 core; overlay выбирается при сборке.
