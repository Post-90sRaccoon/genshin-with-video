param([Parameter(Mandatory=$true)][int[]]$ProcessIds)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class GvTokenProbe {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr token, int info, IntPtr buffer, int length, out int needed);
    [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
    [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthority(IntPtr sid, uint index);
    public static int[] Read(int id) {
        IntPtr process=OpenProcess(0x1000,false,id), token=IntPtr.Zero, buffer=IntPtr.Zero;
        if(process==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            if(!OpenProcessToken(process,8,out token))throw new Win32Exception(Marshal.GetLastWin32Error());
            int needed;GetTokenInformation(token,25,IntPtr.Zero,0,out needed);
            if(needed<=0)throw new Win32Exception(Marshal.GetLastWin32Error());
            buffer=Marshal.AllocHGlobal(Math.Max(needed,4));
            if(!GetTokenInformation(token,25,buffer,needed,out needed))throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr sid=Marshal.ReadIntPtr(buffer);
            byte count=Marshal.ReadByte(GetSidSubAuthorityCount(sid));
            int integrity=Marshal.ReadInt32(GetSidSubAuthority(sid,(uint)(count-1)));
            if(!GetTokenInformation(token,20,buffer,4,out needed))throw new Win32Exception(Marshal.GetLastWin32Error());
            return new int[]{integrity,Marshal.ReadInt32(buffer)};
        }finally{if(buffer!=IntPtr.Zero)Marshal.FreeHGlobal(buffer);if(token!=IntPtr.Zero)CloseHandle(token);CloseHandle(process);}
    }
}
'@
$rows=foreach($targetId in $ProcessIds) {
    try{$values=[GvTokenProbe]::Read($targetId);[pscustomobject]@{ProcessId=$targetId;IntegrityRid=$values[0];Integrity=if($values[0] -ge 0x4000){'System'}elseif($values[0] -ge 0x3000){'High'}elseif($values[0] -ge 0x2000){'Medium'}else{'Low'};Elevated=($values[1] -ne 0);Error=$null}}
    catch{[pscustomobject]@{ProcessId=$targetId;Error=$_.Exception.InnerException.Message}}
}
$rows|ConvertTo-Json
