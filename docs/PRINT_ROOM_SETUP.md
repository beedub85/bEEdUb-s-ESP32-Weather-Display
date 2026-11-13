# Print Room setup

The display reads a local Python bridge. The bridge reads only the Home Assistant sensor entities you configure and serves them to the display. The display needs the bridge address and a separate bridge access key. It never receives the Home Assistant token. Printer controls remain in Bambu Studio or Home Assistant.

1. In Home Assistant, connect the printer using the [Bambu Lab integration](https://github.com/greghesp/ha-bambulab). Confirm that its print status and progress entities change during a print.
2. In Home Assistant, open **Developer tools → States** and find your printer's actual entity IDs. On an always-on computer on your trusted LAN, copy `print-room-bridge/config.example.json` to a private file outside the repo, such as `$HOME/print-room-config.json`. Set `home_assistant` to your Home Assistant URL and replace the example IDs. The `status` mapping is required. Delete mappings for any optional entities you don't have. Chamber temperature and room temperature, humidity, and PM2.5 depend on which sensors are available.
3. Create a Home Assistant long-lived access token from your user profile. Keep it on the bridge computer. In PowerShell, from the repo folder, run the following. Replace `192.168.1.10` with the bridge computer's LAN IPv4 address.

   ```powershell
   $secret = Read-Host 'Home Assistant token' -AsSecureString
   $credential = New-Object System.Management.Automation.PSCredential('home-assistant', $secret)
   $env:HA_TOKEN = $credential.GetNetworkCredential().Password
   $env:PRINT_ROOM_KEY = [guid]::NewGuid().ToString("N")
   Write-Host "Display bridge key: $env:PRINT_ROOM_KEY"
   try {
       py -3 .\print-room-bridge\bridge.py "$HOME/print-room-config.json" --bind 192.168.1.10
   } finally {
       Remove-Item Env:HA_TOKEN, Env:PRINT_ROOM_KEY
       $secret.Dispose()
   }
   ```

   Keep the bridge running. If Windows Firewall prompts, allow the connection on your private network. The bridge requires the separate access key for its read-only endpoint. Local HTTP does not encrypt the readings or bridge key. Keep it on a trusted LAN and avoid exposing port 8787 to the internet. Use an HTTPS Home Assistant URL if your token travels between machines over an untrusted network.

4. Pull the firmware update, build, and upload in PlatformIO. On the display, swipe left from Weather and tap **Settings → Print Room**. Enter `http://192.168.1.10:8787/status` using the same bridge IP from step 3 and the 32-character bridge key from step 3, then tap **Save**. Scroll down in the form to reach the key field. The example generates a new key each time the bridge starts. To keep the display configured across bridge restarts, reuse the same key privately.
5. Return to Weather and tap **Print Room**. The display polls every three seconds while this screen is open. The update speed also depends on the Home Assistant integration. A missing printer status displays **Offline / unavailable** and optional sensors can still report independently. A bridge connection failure or stale response clears the live readings.

If the screen reports that the bridge is unavailable, check that the bridge is running and that the display has the current access key. If the display shows **Offline / unavailable**, check the entity ID, token, and printer connection in Home Assistant.
