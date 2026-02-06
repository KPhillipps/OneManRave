# VS Code reinstall/restore checklist (macOS)

## Backup (before uninstall)
1) Close VS Code.
2) Copy user settings:
   - `~/Library/Application Support/Code/User/settings.json`
   - `~/Library/Application Support/Code/User/keybindings.json`
   - `~/Library/Application Support/Code/User/snippets/`
   - `~/Library/Application Support/Code/User/tasks.json` (if present)
3) Copy extension list:
   - `code --list-extensions > ~/vscode-extensions.txt`
4) Copy workspace settings in each repo:
   - `.vscode/settings.json`
   - `.vscode/tasks.json`
   - `.vscode/launch.json`
5) Copy VS Code workspace files:
   - `*.code-workspace` files (for multi-folder workspaces)
6) Copy your projects:
   - Example: `/Users/kellyphillipps/Documents/PlatformIO/oneManRave/`
7) Backup secrets/credentials used by VS Code:
   - If you use OS Keychain, export needed items from Keychain Access.
   - If you use `.env` files, copy them from each project (do not commit).
8) Optional: copy VS Code global storage (extensions state):
   - `~/Library/Application Support/Code/User/globalStorage/`
   - `~/Library/Application Support/Code/User/workspaceStorage/`

## Uninstall VS Code (optional)
1) Remove the app: `/Applications/Visual Studio Code.app`
2) Remove supporting files only if you want a clean reset:
   - `~/Library/Application Support/Code/`
   - `~/Library/Caches/com.microsoft.VSCode/`
   - `~/Library/Preferences/com.microsoft.VSCode.plist`

## Restore (after reinstall)
1) Install VS Code and open once, then close it.
2) Restore user settings:
   - Copy back `settings.json`, `keybindings.json`, `snippets/`, `tasks.json`.
3) Restore workspace files and project folders.
4) Restore extensions:
   - `cat ~/vscode-extensions.txt | xargs -n 1 code --install-extension`
5) Restore global/workspace storage if needed:
   - Copy back `globalStorage/` and `workspaceStorage/` (optional, use if settings didn’t stick).
6) Restore secrets:
   - Re-import Keychain items or replace `.env` files.
7) Open your workspace (`*.code-workspace`) and verify settings.

## Notes
- VS Code settings sync can handle most of this automatically, but it may not cover local `.env` files or non-synced workspace settings.
- If you use PlatformIO, keep `.vscode/settings.json` in each project to pin ports and other local configuration.
