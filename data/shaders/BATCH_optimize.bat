echo off

setlocal EnableDelayedExpansion

set "binary_directory=Binary"

set "spirv-opt=%VULKAN_SDK%\bin\spirv-opt.exe"

cd\
cd %~dp0
cd !binary_directory!
set "ABS_PATH=%CD%"
del /Q *.orig

set "counter=0"
for %%f in (*.bin) do (
	set /A "counter=!counter!+1"
)
echo !counter! .bin to optimize in parallel....

for %%f in (*.bin) do (

	set "vIn=%%~nf"
	set "vOut=!ABS_PATH!\%%~f"

	rename !vIn!.bin !vIn!.bin.orig
	set "vIn=!vOut!.orig"

	rem echo !vIn! | findstr /C:"geom">nul && (
		
	rem	if !counter! LEQ 1 (
	rem		echo last .bin ....
	rem		start /WAIT /B !spirv-opt! --target-env=vulkan1.2 -O !vIn! -o !vOut!
	rem	) else (
	rem		start /B !spirv-opt! --target-env=vulkan1.2 -O !vIn! -o !vOut!
	rem	)
		
	rem	echo optimized w/o memory model:  !vOut!
	rem ) || (
				
		if !counter! LEQ 1 (
			echo last .bin ....
			start /WAIT /B !spirv-opt! --target-env=vulkan1.3 --upgrade-memory-model -O !vIn! -o !vOut!
		) else (
			start /B !spirv-opt! --target-env=vulkan1.3 --upgrade-memory-model -O !vIn! -o !vOut!
		)
		
		echo optimized !vOut!
	rem )

	set /A "counter=!counter!-1"
)

rem timeout /T 1   msvc no likey

for %%f in (*.orig) do (
	set "vIn=%%~nf"
	del /Q !vIn!.orig
)

cd..

echo -COMPLETE-