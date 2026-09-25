# shot.ps1 - capture the LlamaLauncher window to a PNG.
#   -Out <png>        where to write the screenshot
#   -Log <txt>        where to write a short status line (bash can read this)
#   -Hwnd <int>       capture a specific window handle instead of searching
param(
    [Parameter(Mandatory=$true)][string]$Out,
    [Parameter(Mandatory=$true)][string]$Log,
    [int]$Hwnd = 0,
    [switch]$BringToFront
)
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WinCap {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@

$lines = @()
try {
    $h = [IntPtr]$Hwnd
    if ($h -eq [IntPtr]::Zero -or -not [WinCap]::IsWindow($h)) {
        $h = [WinCap]::FindWindowW("LlamaLauncherMainWindow", $null)
    }
    if ($h -eq [IntPtr]::Zero) {
        throw "window not found (hwnd=$Hwnd)"
    }

    $sb = New-Object System.Text.StringBuilder 256
    [WinCap]::GetClassNameW($h, $sb, 256) | Out-Null
    $lines += "hwnd=$($h) class=$($sb.ToString())"

    if ($BringToFront) {
        [WinCap]::ShowWindow($h, 5) | Out-Null
        [WinCap]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 1500
    }

    $r = New-Object WinCap+RECT
    if (-not [WinCap]::GetWindowRect($h, [ref]$r)) { throw "GetWindowRect failed" }
    $w = $r.R - $r.L
    $ht = $r.B - $r.T
    $lines += "rect=$($r.L),$($r.T) size=${w}x${ht}"
    if ($w -le 0 -or $ht -le 0) { throw "degenerate window size" }

    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $lines += "SAVED $Out"
    $lines += "OK"
    $lines | Set-Content -Path $Log -Encoding UTF8
    exit 0
}
catch {
    $lines += "FAIL: $($_.Exception.Message)"
    $lines | Set-Content -Path $Log -Encoding UTF8
    exit 1
}