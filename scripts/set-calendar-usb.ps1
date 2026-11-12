param(
    [Parameter(Mandatory = $true)]
    [string]$Port
)

$ErrorActionPreference = 'Stop'
$url = (Read-Host 'Paste the Apps Script web app URL ending in /exec').Trim()
if ($url.Length -gt 400 -or $url -cnotmatch '\Ahttps://script\.google\.com/macros/s/[A-Za-z0-9_-]+/exec\z') {
    throw 'Enter the deployed Google Apps Script URL ending in /exec.'
}

$secret = Read-Host 'Paste the Display token and press Enter' -AsSecureString
$pointer = [IntPtr]::Zero
$serial = $null
try {
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
    $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    if ($token -notmatch '\A[0-9a-fA-F]{32,128}\z') {
        throw 'Enter only the hexadecimal Display token from Apps Script.'
    }

    $serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.ReadTimeout = 500
    $serial.WriteTimeout = 2000
    $serial.NewLine = "`n"
    $serial.Open()

    Read-Host 'On the display, tap Set up over USB, then press Enter here' | Out-Null
    $serial.DiscardInBuffer()
    $serial.WriteLine("CALENDAR $url $token")
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $reply = $serial.ReadLine().Trim()
        } catch [TimeoutException] {
            continue
        }
        if ($reply -eq 'CALENDAR SAVED') {
            Write-Host 'Calendar settings saved on the display.'
            return
        }
        if ($reply -eq 'CALENDAR INVALID') {
            throw 'The display rejected the URL or token. Check both values and try again.'
        }
    }
    throw 'No reply. Check the COM port, firmware version, and USB setup screen.'
} finally {
    if ($serial) { $serial.Dispose() }
    if ($pointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
    $secret.Dispose()
}
