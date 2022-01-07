#pragma once
#ifndef NDEBUG
#include "globals.h"
#include "MinCity.h"
#include "cVulkan.h"
#include <strsafe.h>
#include <Utility/stringconv.h>

#include <Utility/mio/mmap.hpp>
#include <filesystem>
#include <fstream>

// --------- Steps for adding a new shader for live shader usage:

// 0 = reserved
// 1 = Volumetric
// 2 = Upsample
// ...

// 1.) add shader define
#define LIVE_SHADER_RESERVED 0
#define LIVE_SHADER_VOLUMETRIC 1
#define LIVE_SHADER_UPSAMPLE 2

// 2.) set to new shader define or shader to be set to liveshader mode
#define LIVE_SHADER LIVE_SHADER_VOLUMETRIC //LIVE_SHADER_VOLUMETRIC//LIVE_SHADER_UPSAMPLE 

// 3.) add a section for new shader
#if (LIVE_SHADER == LIVE_SHADER_VOLUMETRIC)
#define LIVE_SHADER_VERT L"volumetric.vert.bin" // (vertex shader is NOT optional) 
#define LIVE_SHADER_FRAG L"volumetric.frag.bin"

#define LIVE_PIPELINE (_volData.pipeline)
#define LIVE_PIPELINELAYOUT (_volData.pipelineLayout)
#define LIVE_RENDERPASS (_window->downPass())
#endif
#if (LIVE_SHADER == LIVE_SHADER_UPSAMPLE)
#define LIVE_SHADER_VERT L"postquad.vert.bin" // (vertex shader is NOT optional) 
#define LIVE_SHADER_FRAG L"upsample.frag.bin"

#define LIVE_PIPELINE (_upData[eUpsamplePipeline::UPSAMPLE].pipeline)
#define LIVE_PIPELINELAYOUT (_upData[eUpsamplePipeline::UPSAMPLE].pipelineLayout)
#define LIVE_RENDERPASS (_window->upPass())
#endif

// 4.)
// cVulkan:: Under the Chosen Pipeline Creation (cVulkan->cpp code section for pipelines)
/* eg.)
#if !defined(NDEBUG) && defined(LIVESHADER_MODE) && (LIVE_SHADER == LIVE_SHADER_UPSAMPLE)
	liveshader::cache_pipeline_creation(pm);
#endif
*/

// 5.) If specialization constants are required by new shader
//     edit liveshader.cpp at around line 68 inside recreate_pipeline()

// 6.) Done!

//-----------------------------------------------------------------------------------------------------------------------------------

// Troubleshooting.) **** usage:

// cVulkan::Render() -- should remain the same
/*
#if (!defined(NDEBUG) & defined(LIVE_PIPELINE) & defined(LIVE_PIPELINELAYOUT) & defined(LIVE_RENDERPASS) & defined(LIVE_INTERVAL))
	liveshader::recreate_pipeline(LIVE_PIPELINE, _device, _fw, LIVE_PIPELINELAYOUT, LIVE_RENDERPASS, LIVE_INTERVAL);
#endif
*/
// ***************************************************** Finally edit recreate_pipeline in liveshader.cpp so order matches, shaders are correct etc

#define LIVE_INTERVAL (milliseconds(440))	// ms
#define MINCITY_FULL_PATH L"C:/MinCity/"

#define ONGOING_COMPILE (-9)
#define INITIAL_COMPILE (9)
#define SUCCESS_COMPILE (0)
#define NOCHANGE_COMPILE (1)
#define UNKNOWN_COMPILE (-11)
#define FAILED_CREATE_COMPILER_PROCESS (-1001)
//
namespace liveshader
{
	typedef struct CachedShader
	{
		uint8_t*	glsl;
		size_t		size;

		CachedShader()
			: glsl(nullptr), size(0)
		{}
		~CachedShader()
		{
			SAFE_DELETE_ARRAY(glsl);
		}

	} CachedShader;
	
	extern CachedShader CachedGLSLShaders[2];

#define CACHED_SHADER_VERT 0
#define CACHED_SHADER_FRAG 1

	void ErrorOut(LPTSTR lpszFunction);

	static bool compare_shader(std::wstring const SHADER_GLSL_FILE_PATH, uint32_t const CACHED_SHADER_INDEX)
	{
		std::error_code error{};

		mio::mmap_source mmap = mio::make_mmap_source(SHADER_GLSL_FILE_PATH, error);
		if (!error) {
			if (mmap.is_open() && mmap.is_mapped()) {

				bool bChanged(false);

				size_t const current_size(mmap.size());

				if (!(bChanged = !(CachedGLSLShaders[CACHED_SHADER_INDEX].size == current_size))) {	// check size

					// size is equal check contents
					if (nullptr != CachedGLSLShaders[CACHED_SHADER_INDEX].glsl) {

						bChanged = ( 0 != memcmp(CachedGLSLShaders[CACHED_SHADER_INDEX].glsl, mmap.data(), current_size) );
					}
					else {
						bChanged = true; // just in case
					}
				}

				if (bChanged) {
					// update cache
					if (current_size != CachedGLSLShaders[CACHED_SHADER_INDEX].size) {
						SAFE_DELETE_ARRAY(CachedGLSLShaders[CACHED_SHADER_INDEX].glsl);
						CachedGLSLShaders[CACHED_SHADER_INDEX].size = current_size;
						CachedGLSLShaders[CACHED_SHADER_INDEX].glsl = new uint8_t[current_size];
					}

					// cached...
					memcpy(CachedGLSLShaders[CACHED_SHADER_INDEX].glsl, mmap.data(), current_size);
				}

				mmap.unmap();

				return(bChanged);
			}
		}

		return(true);  // changed or could not detect change
	}
	static int compile_shader(std::wstring const SHADER_BINARY_FILE, uint32_t const CACHED_SHADER_INDEX, PROCESS_INFORMATION& pi)
	{
		// "$(VULKAN_SDK)\bin\glslangValidator.exe" -V "%(FullPath)" -o "%(FullPath)\..\Binary\%(Filename)%(Extension).bin"
		static std::wstring const szPathBinary(MINCITY_FULL_PATH SHADER_BINARY_DIR);
		static std::wstring const szPathGLSL(MINCITY_FULL_PATH SHADER_BINARY_DIR L"../");

		std::wstring const SHADER_GLSL_FILE(SHADER_BINARY_FILE.substr(0, SHADER_BINARY_FILE.find_last_of('.')));

		DWORD lastexitCode(UNKNOWN_COMPILE);

		STARTUPINFO si;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);

		if (nullptr != pi.hProcess) {
			// Test WAIT_OBJECT_0|WAIT_ABANDONED
			if (WAIT_OBJECT_0 == WaitForSingleObject(pi.hProcess, 0)) {

				GetExitCodeProcess(pi.hProcess, &lastexitCode);

				// Close process and thread handles. 
				CloseHandle(pi.hProcess);
				if (nullptr != pi.hThread) {
					CloseHandle(pi.hThread);
					pi.hThread = nullptr;
				}
				pi.hProcess = nullptr;

				return(lastexitCode);
			}
			else {
				lastexitCode = ONGOING_COMPILE;
				return(lastexitCode); // process still ongoing
			}
		}
		else {

			if (!compare_shader(szPathGLSL + SHADER_GLSL_FILE, CACHED_SHADER_INDEX))
				return(NOCHANGE_COMPILE);

			//  new instance of compiling
			lastexitCode = INITIAL_COMPILE;

			// pipeline is only rebuilt when exitcode is 0 (success)
			// need to skip pipeline rebuild on initial instance
		}
		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

		std::wstring szCmd(L"glslangValidator.exe ");

		szCmd += L" --target-env vulkan1.2 \"";
		szCmd += szPathGLSL;
		szCmd += SHADER_GLSL_FILE;
		szCmd += L"\" -o \"";
		szCmd += szPathBinary;
		szCmd += SHADER_BINARY_FILE;
		szCmd += L"\"";

		// must be memory block that can be editted by CreateProcess
		LPWSTR cmdline(nullptr);
		{
			size_t const szLength(szCmd.length());
			cmdline = new WCHAR[szLength + 1];
			wcsncpy_s(cmdline, szLength + 1, szCmd.c_str(), szLength);
		}

		LPWSTR vulkansdk_path(nullptr);
		{
			size_t const szLength = GetEnvironmentVariable(L"VULKAN_SDK", nullptr, 0);
			vulkansdk_path = new WCHAR[szLength + 1];
			GetEnvironmentVariable(L"VULKAN_SDK", vulkansdk_path, (DWORD)(szLength + 1ULL));
		}

		// Start the child process. 
		std::wstring szCompiler(vulkansdk_path);
		szCompiler += L"/bin/glslangValidator.exe";

		if (!CreateProcess(szCompiler.c_str(),   // No module name (use command line)
			cmdline,        // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			0,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi)           // Pointer to PROCESS_INFORMATION structure
			)
		{
			lastexitCode = FAILED_CREATE_COMPILER_PROCESS;
			ErrorOut((LPTSTR)L"CreateProcess");
		}

		if (vulkansdk_path) {
			delete[] vulkansdk_path; vulkansdk_path = nullptr;
		}
		if (cmdline) {
			delete[] cmdline; cmdline = nullptr;
		}

		return(lastexitCode);
	}

	void cache_pipeline_creation(vku::PipelineMaker const& pipeline);

	void recreate_pipeline(vk::Pipeline& pipeline_out,
		vk::Device const& device,
		vku::Framework const& fw,
		vk::UniquePipelineLayout const& pipelineLayout,
		vk::RenderPass const& renderPass,
		milliseconds const interval);
	

} // endnamespace

#endif
