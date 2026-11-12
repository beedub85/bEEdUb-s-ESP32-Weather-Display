# Connect your Google Calendar

This bridge runs in **your** Google account. It reads events using the Calendar API and serves only the selected week to your display over verified HTTPS. The ESP32 never needs your Google password.

## Create the bridge

1. On your computer, open https://script.google.com/ and create a **New project**, called Weather Display Calendar.
2. Replace the contents of `Code.gs` with the supplied `calendar-bridge/Code.gs`.
3. In the editor, open **Project Settings** and enable **Show appsscript.json manifest file in editor**.
4. Replace that manifest with `calendar-bridge/appsscript.json`. Its explicit OAuth scope is `calendar.readonly`. Its time zone is `America/Winnipeg`; change it if needed. Calendar all-day dates are kept as date strings, not shifted through UTC.
5. Save. If **Calendar API** is not already shown in **Services**, click the plus sign next to Services and add **Google Calendar API**, version v3, identifier `Calendar`. The supplied manifest normally adds it for you. If you use a custom Google Cloud project, also enable the Google Calendar API in that project's Cloud Console.
6. Select **setup** from the function selector and click **Run**.
7. Authorize the project to **view/read calendars** in your account. This is your own script. Google may show an unverified-app screen for a personal script; review the code and requested permission before authorizing.
8. After it completes, open the **Execution log** and copy the generated **Display token** locally. It is a 64-character secret. You can also find it under Project Settings → Script Properties → `DISPLAY_TOKEN`.

## Choose calendars

The default is your primary calendar. For several calendars, set the `CALENDAR_IDS` Script Property to a comma-separated list:

```text
primary,your-shared-calendar-id@group.calendar.google.com
```

Find an ID in Google Calendar → Settings → the calendar → Integrate calendar → Calendar ID. Your Google account must have read access to each chosen calendar. Up to eight are used. The bridge preserves Google's event colors, falling back to the calendar color. Shared calendars remain private in Google Calendar; do not make them publicly visible.

## Deploy the bridge

1. Click **Deploy → New deployment**.
2. Choose **Web app**.
3. Set **Execute as: Me**.
4. Set **Who has access: Anyone**, so the ESP32 can call it without an interactive Google sign-in. This is the anonymous web-app endpoint, protected separately by your generated token. 
5. Click Deploy and copy the **Web app URL** ending in `/exec`. Use this URL, not the test `/dev` URL.
## Enter the URL and token over USB on Windows

Flash the updated firmware with **Set up over USB** in the Google Calendar settings first. If that button is missing, update the firmware or use the touchscreen method below.

1. Connect the display to your Windows PC with the same USB data cable and programming port you use for PlatformIO uploads. Close the PlatformIO Serial Monitor so the helper can open the port.
2. In PowerShell, open the `weather-display` project folder and run the helper. Replace `COM4` if your upload uses a different port.

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\set-calendar-usb.ps1 -Port COM4
   ```

3. Paste the deployed **Web app URL** at the first prompt and press Enter. Paste only the **Display token** at the second prompt and press Enter. PowerShell masks the token.
4. When the helper prompts you, swipe left from Weather on the display and tap **Google Calendar → Set up over USB**. Press Enter on the PC within two minutes. Use **Close** on the display to cancel.
5. Wait for **Calendar settings saved on the display**. The helper transfers the URL and token over the cable and the display saves them for future boots. Swipe right from Weather to see Calendar after Wi-Fi and internet time sync. Use the refresh icon to retry manually.

The USB helper configures the display only. Set `CALENDAR_IDS` under **Project Settings → Script Properties** in Apps Script as described above.

## Enter the URL and token on the touchscreen

Swipe left from Weather and tap **Google Calendar**. Enter the URL and token in their separate fields, then tap **Save**. Scroll the form to reach the token field. Swipe back to Weather, then right to Calendar.


The token is a bearer secret: anyone who obtains both it and the endpoint can read the selected event fields. Keep it private, do not post a token-bearing URL, and do not paste it into this conversation. It is sent in an HTTPS query because Apps Script's standard `doGet` interface provides query parameters rather than arbitrary request headers. The bridge checks the token before making any Calendar API calls and never logs request URLs or event data. The script does not grant unauthenticated callers your Google OAuth token.

## Updates and removal

- After editing script code or the manifest, use Deploy → Manage deployments → Edit → **New version**, then Deploy. Editing code alone does not update an existing production deployment.
- Script Properties such as calendar IDs/token apply without creating a new script version.
- To rotate the token, replace `DISPLAY_TOKEN` with a new random 64-character hexadecimal secret and update the display. Do not use a short password.
- To disable access, archive/delete the deployment or clear its token. Revoke the script's Google account access if you stop using it.

## Troubleshooting

- **Calendar token rejected:** device token differs from the Script Property, or setup was not run.
- **Invalid range:** verify device time sync, time zone, and the week selection. Navigation is limited to 12 weeks backward/forward.
- **Calendar read failed:** check the Calendar API service, readonly authorization, and IDs/read access for every calendar.
- **Calendar response invalid:** often a Google sign-in HTML page from a non-anonymous deployment or wrong `/dev` URL. Confirm deployment settings.
- **Connection failed:** confirm internet access and a correct device clock. HTTPS certificate validation needs accurate time.
- **Showing first 100 events:** the selected week exceeded the embedded display's cap. Reduce selected calendars to show fewer events.
- **No events today:** the agenda includes only events intersecting the display's local day. Confirm the screen's time zone.

This bridge uses the Google-hosted Apps Script and Calendar APIs and is subject to their quotas and service availability. Five-minute polling is the default. It is not a Google Calendar web embed or a paid cloud subscription.

Official references:
- https://developers.google.com/apps-script/guides/web
- https://developers.google.com/apps-script/guides/services/advanced
- https://developers.google.com/calendar/api/v3/reference/events/list
- https://developers.google.com/apps-script/guides/content
