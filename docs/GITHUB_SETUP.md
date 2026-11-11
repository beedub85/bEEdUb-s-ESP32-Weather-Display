# Put this project on GitHub — Windows / VS Code

This is a **new repository** for the display app, separate from your existing `weatherstation` fork. Nothing here needs to be committed to or uploaded onto the working weather station.

## 1. Extract and open the project

1. Extract the supplied ZIP.
2. Move its `weather-display` folder beside your other Arduino projects. You can rename it `esp32-weather-display`, for example:

   `C:\Users\brian\Documents\Arduino\esp32-weather-display`

3. In VS Code, choose **File → Open Folder** and select that folder. You should see `platformio.ini`, `README.md`, `src`, `include`, `lib`, and `docs` directly inside it.
4. Choose **Terminal → New Terminal**. It should open in the project folder. Run `git --version` to confirm Git is available. You already used Git for the station fork; if this command fails, install Git for Windows and reopen VS Code.

## 2. Create an empty repository on GitHub

1. Sign in and open https://github.com/new .
2. Choose your account as owner; your previous fork used `beedub85`.
3. Enter **esp32-weather-display** as the repository name.
4. Paste this into **Description**:

   `Swipe-controlled weather and Google Calendar touchscreen for the Waveshare ESP32-S3 7-inch display, built with PlatformIO and LVGL.`

5. Choose **Public** to share the code, or **Private** to keep it restricted.
6. Leave **Add a README**, **Add .gitignore**, and **Choose a license** unselected. The extracted project already has a README and gitignore; starting empty avoids a conflicting initial commit.
7. Click **Create repository**. Keep the resulting page open and copy its HTTPS repository URL.

## 3. Commit the local project

In the VS Code terminal, run these commands one at a time:

```powershell
git init -b main
git add .
git status
git commit -m "Add ESP32 touchscreen weather and calendar display"
```

The supplied `.gitignore` excludes PlatformIO build output and compiled binaries. Your Wi-Fi password and calendar token are entered on the device and are not in the source files.

If the commit asks for your identity, set it **for this repository**, then repeat the commit command:

```powershell
git config user.name "Your Name"
git config user.email "Your GitHub commit email"
git commit -m "Add ESP32 touchscreen weather and calendar display"
```

Replace the name/email examples with your own values. You can use the GitHub-provided private commit email from GitHub Settings → Emails.

## 4. Connect and push

For the account and repository name above:

```powershell
git remote add origin https://github.com/beedub85/esp32-weather-display.git
git remote -v
git push -u origin main
```

Use the actual HTTPS URL copied in step 2 if you selected a different account or name. If Git opens a browser or Git Credential Manager, sign into your account and complete that flow. GitHub HTTPS authentication does not use your ordinary account password at a Git terminal password prompt.

Refresh the repository page. You should now see the source, the README with rendered screen images, the bundled driver libraries, and the calendar bridge.

## 5. Add the project write-up

- **Main page:** `README.md` is already included and becomes the project write-up automatically.
- **About description:** use the sentence in step 2, or copy it from `docs/GITHUB_WRITEUP.md`.
- **About topics:** optional suggestions are `esp32`, `esp32-s3`, `weather-station`, `lvgl`, `platformio`, `waveshare`, `google-calendar`, and `touchscreen`.
- **First release:** if you choose to create one later, `docs/GITHUB_WRITEUP.md` includes release-note text. The current version is a first hardware-test release; compiled and rendered does not mean tested on the physical board.

Keep `THIRD_PARTY.md` and the bundled libraries' license files with the repository.

## 6. Build, upload, and enter settings

1. Open PlatformIO in VS Code and select **waveshare_7_8mb**.
2. Click Build, then Upload with the Waveshare display connected.
3. After boot, swipe **left** from Weather to Settings.
4. Enter Wi-Fi details using the touchscreen keyboard and Save.
5. Enter the weather station's base address, then Save.
6. Follow `docs/GOOGLE_CALENDAR_SETUP.md` to deploy your own read-only bridge. Enter its `/exec` URL and token on the touchscreen.
7. Choose your time zone, units, and dimming level on screen. Saved settings survive a restart. No configuration file is needed.

## Later updates

After changing code or replacing it with a later release, review and push the update:

```powershell
git status
git add .
git commit -m "Update weather display"
git push
```

If Git says `remote origin already exists`, run `git remote -v` first. If it already points to the new display repository, continue with `git push -u origin main`. If it points somewhere else, use `git remote set-url origin YOUR-COPIED-REPOSITORY-URL` before pushing. Do not force-push if Git reports a conflicting remote history; stop and resolve that specific message.

Official references:
- https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-new-repository
- https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github
- https://docs.github.com/en/get-started/git-basics/set-up-git
