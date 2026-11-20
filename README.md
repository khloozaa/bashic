# termsnake

A colorful Snake game for your terminal, written in C++17 with no libraries.
![termsnake demo](screenshots/demo.gif)
## Requirements
A C++17 compiler and `make`.

- Ubuntu/Debian: `sudo apt install build-essential`
- macOS: `xcode-select --install`
- Windows: use WSL (Ubuntu) and follow the Linux steps

Your terminal needs 256-color support and should be at least 60 columns by 22 rows.

## Play
```bash
git clone https://github.com/YOURNAME/termsnake.git
cd termsnake
make run
```

## Install (optional, then just type `termsnake`)
```bash
make install
```
This copies the game to `~/.local/bin`. Make sure that folder is on your `PATH`.
Remove it with `make uninstall`.

## Controls
| Key | Action |
|---|---|
| Arrows / WASD | Move |
| Space or P | Pause |
| Enter | Play again (after game over) |
| Q or Esc | Quit |

Your best score is saved in `~/.termsnake_highscore`.
