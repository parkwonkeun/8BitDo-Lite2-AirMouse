param(
    [Parameter(Mandatory=$true)][string]$Jdk,
    [Parameter(Mandatory=$true)][string]$Sdk
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$appSource = Join-Path $PSScriptRoot 'app/src/main'
$artifactDir = Join-Path $PSScriptRoot 'build'
$stagingDir = Join-Path $projectRoot 'work/android-build'
$platform = Get-ChildItem -Path $Sdk -Recurse -Filter android.jar | Select-Object -First 1
$tools = (Get-ChildItem -Path $Sdk -Recurse -Filter aapt2.exe | Select-Object -First 1).Directory.FullName
New-Item -ItemType Directory -Force $artifactDir,$stagingDir,"$stagingDir/classes" | Out-Null
function Run([string]$program,[string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program exited with $LASTEXITCODE" }
}
$env:JAVA_HOME = $Jdk
$env:Path = "$(Join-Path $Jdk 'bin');$env:Path"
$jar = $platform.FullName
$words = "$appSource/java/local/lite2/control"
New-Item -ItemType Directory -Force "$stagingDir/checks" | Out-Null
Run "$Jdk/bin/javac.exe" @('-encoding','UTF-8','-d',"$stagingDir/checks","$words/Words.java","$words/ExtraWords.java","$PSScriptRoot/checks/WordsCheck.java")
Run "$Jdk/bin/java.exe" @('-cp',"$stagingDir/checks",'local.lite2.control.WordsCheck')
Run "$tools/aapt2.exe" @('compile','--dir',"$appSource/res",'-o',"$stagingDir/resources.zip")
Run "$tools/aapt2.exe" @('link','-o',"$stagingDir/base.apk",'-I',$jar,'--manifest',"$appSource/AndroidManifest.xml",'-A',"$appSource/assets","$stagingDir/resources.zip")
$javaSources = @(Get-ChildItem "$appSource/java" -Recurse -Filter '*.java' | ForEach-Object FullName)
Run "$Jdk/bin/javac.exe" (@('-encoding','UTF-8','-source','8','-target','8','-Xlint:-options','-classpath',$jar,'-d',"$stagingDir/classes") + $javaSources)
Run "$Jdk/bin/jar.exe" @('cf',"$stagingDir/classes.jar",'-C',"$stagingDir/classes",'.')
Run "$tools/d8.bat" @('--min-api','26','--lib',$jar,'--output',"$stagingDir","$stagingDir/classes.jar")
$adbBundle=Join-Path $projectRoot 'work/tablet-adb/android-sdk-tools-static-aarch64.zip'
if (!(Test-Path -LiteralPath $adbBundle)) {
    New-Item -ItemType Directory -Force (Split-Path $adbBundle) | Out-Null
    Invoke-WebRequest 'https://github.com/lzhiyong/android-sdk-tools/releases/download/35.0.2/android-sdk-tools-static-aarch64.zip' -OutFile $adbBundle
}
if ((Get-FileHash $adbBundle).Hash -ne 'DB1CEA2C4454D5F9C5A802646B2D1CF560B4EE7BADBE23E51AB8E1881BB50FC2') {throw 'Unexpected Android ADB distribution'}
$native=Join-Path $stagingDir 'lib/arm64-v8a'
New-Item -ItemType Directory -Force $native | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($adbBundle)
try {
    [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry('platform-tools/adb'),(Join-Path $native 'liblite2adb.so'),$true)
} finally {$archive.Dispose()}
Copy-Item (Join-Path $projectRoot 'work/android-bridge/lite2-pointer-bridge') (Join-Path $native 'liblite2input.so') -Force
Push-Location $stagingDir
try {
    Run "$Jdk/bin/jar.exe" @('uf','base.apk','classes.dex','lib/arm64-v8a/liblite2adb.so','lib/arm64-v8a/liblite2input.so')
} finally { Pop-Location }
Run "$tools/zipalign.exe" @('-f','4',"$stagingDir/base.apk","$stagingDir/aligned.apk")
$keyStore = "$stagingDir/debug.jks"
if (!(Test-Path $keyStore)) {
    Run "$Jdk/bin/keytool.exe" @('-genkeypair','-keystore',$keyStore,'-storepass','android','-keypass','android','-alias','localdebug','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Lite2 Local Development')
}
$apk = "$artifactDir/Lite2Control-debug.apk"
Run "$tools/apksigner.bat" @('sign','--ks',$keyStore,'--ks-pass','pass:android','--key-pass','pass:android','--out',$apk,"$stagingDir/aligned.apk")
Run "$tools/apksigner.bat" @('verify','--verbose',$apk)
Get-FileHash -LiteralPath $apk
