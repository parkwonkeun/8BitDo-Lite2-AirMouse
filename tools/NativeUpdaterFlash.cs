using System;
using System.IO;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using System.Threading;

// Runs only the official DLL's update routine, with a caller-pinned file hash.
// Compile x86 beside the official DLL and run from its directory (data/ required).
class NativeUpdaterFlash {
    const string Dll="8BitDoFirmwareUpdaterTools.dll";
    delegate void ErrorCallback();
    delegate void StringCallback(string message);
    delegate void PidCallback(int firmwarePid,int devicePid);
    [DllImport(Dll)] static extern bool FindHid(ushort vid,ushort pid);
    [DllImport(Dll)] static extern void Callback(ErrorCallback callback);
    [DllImport(Dll)] static extern void CallbackString(StringCallback callback);
    [DllImport(Dll)] static extern void CallbackChangeErrorPID(PidCallback callback);
    [DllImport(Dll,EntryPoint="updateGampeWithFile")]
    static extern void Update([MarshalAs(UnmanagedType.LPWStr)] string path);
    [DllImport(Dll)] static extern int dll_getProgress();
    static volatile bool done=false,failed=false;
    static ErrorCallback onError=delegate { failed=true;Console.WriteLine("ERROR: update rejected"); };
    static StringCallback onString=delegate(string s) { failed=true;Console.WriteLine("ERROR: "+s); };
    static PidCallback onPid=delegate(int fw,int dev) { failed=true;Console.WriteLine("PID mismatch: "+fw.ToString("x")+" "+dev.ToString("x")); };

    static int Main(string[] args) {
        if(args.Length!=3 || args[0]!="--flash") {Console.WriteLine("Usage: --flash absolute.dat expected-sha256");return 2;}
        byte[] file=File.ReadAllBytes(args[1]);
        string hash;
        using(var h=SHA256.Create())hash=BitConverter.ToString(h.ComputeHash(file)).Replace("-","").ToLowerInvariant();
        if(hash!=args[2].ToLowerInvariant())throw new Exception("File hash mismatch");
        if(file.Length<28 || BitConverter.ToUInt32(file,4)!=0x1018000 ||
           BitConverter.ToUInt32(file,12)!=0x5112 || BitConverter.ToUInt32(file,8)!=file.Length-28)
            throw new Exception("Not a single Lite2 application section");
        if(!FindHid(0x2dc8,0x3208))throw new Exception("Expected Lite2 boot device missing");
        Callback(onError);CallbackString(onString);CallbackChangeErrorPID(onPid);
        Console.WriteLine("FLASH START sha256="+hash+" bytes="+file.Length);
        Thread worker=new Thread(delegate() {
            try {Update(Path.GetFullPath(args[1]));}
            catch(Exception e) {failed=true;Console.WriteLine("ERROR: "+e.Message);}
            finally {done=true;}
        });
        worker.Start();int previous=-1;
        while(!done) {
            int p=dll_getProgress();
            if(p!=previous){Console.WriteLine("PROGRESS "+p);previous=p;}
            Thread.Sleep(500);
        }
        worker.Join();int final=dll_getProgress();
        Console.WriteLine("FLASH END progress="+final+" error="+failed);
        GC.KeepAlive(onError);GC.KeepAlive(onString);GC.KeepAlive(onPid);
        return !failed && final==100 ? 0:1;
    }
}
