param([switch]$Asan)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'obj\Build'
$arguments=@('-S',$PSScriptRoot,'-G','Visual Studio 17 2022','-A','x64',('-DDISPLAY_ASAN='+$(if($Asan){'ON'}else{'OFF'})))
if($Asan){
 $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
 $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 if(!$installation){throw 'Visual Studio C++ tools were not found.'}
 $compiler=Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory|Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1
 $build=Join-Path $PSScriptRoot ('obj\Asan-'+$compiler.Name)
 $arguments+=@('-T',('version='+$compiler.Name))
}
$arguments+=@('-B',$build)
& cmake @arguments
if($LASTEXITCODE){throw 'Configuration failed.'}
$compile=@('--build',$build,'--config','Release')
if($Asan){$compile+=@('--clean-first','--',('/p:VCToolsVersion='+$compiler.Name))}
& cmake @compile
if($LASTEXITCODE){throw 'Compilation failed.'}
if($Asan){
 $runtime=Join-Path $compiler.FullName 'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll'
 if(Test-Path -LiteralPath $runtime){Copy-Item -LiteralPath $runtime -Destination (Join-Path $build 'Release') -Force}
}
& ctest --test-dir $build -C Release --output-on-failure --timeout 30
if($LASTEXITCODE){throw 'Offline protocol tests failed.'}
