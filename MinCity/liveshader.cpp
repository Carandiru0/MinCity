#include "pch.h"

#ifndef NDEBUG
#include "liveshader.hpp"
#include "cVoxelWorld.h"

namespace liveshader
{
	CachedShader CachedGLSLShaders[2];
	
	void ErrorOut(LPTSTR lpszFunction)
	{
		// Retrieve the system error message for the last-error code

		LPVOID lpMsgBuf;
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)& lpMsgBuf,
			0, NULL);

		// Display the error message and exit the process

		lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
			(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
		StringCchPrintf((LPTSTR)lpDisplayBuf,
			LocalSize(lpDisplayBuf) / sizeof(TCHAR),
			TEXT("%s failed with error %d: %s"),
			lpszFunction, dw, lpMsgBuf);

		FMT_LOG_DEBUG("ERROR: {:s}\n", stringconv::ws2s((LPCTSTR)lpDisplayBuf));

		LocalFree(lpMsgBuf);
		LocalFree(lpDisplayBuf);
	}

	static vku::PipelineMaker cached_pipeline_creation{0,0};

	void cache_pipeline_creation(vku::PipelineMaker const& pipeline)
	{
		cached_pipeline_creation = pipeline;
	}

	void recreate_pipeline(vk::Pipeline& pipeline_out,
		vk::Device const& device,
		vku::Framework const& fw,
		vk::UniquePipelineLayout const& pipelineLayout,
		vk::RenderPass const& renderPass,
		milliseconds const interval)
	{
		tTime const tNow(high_resolution_clock::now());
		static tTime tLast(tNow);
		static bool bPipelinePending(false);

		if (bPipelinePending) {
			//######################### load compiled shader binaries //
			// *********** must update shader reference with new module and specialization constants
			std::vector< vku::SpecializationConstant > constantsVS/*optional*/, constantsFS;

// EDIT HERE //
#if (LIVE_SHADER == LIVE_SHADER_VOLUMETRIC)
			MinCity::VoxelWorld->SetSpecializationConstants_VolumetricLight_VS(constantsVS);
			MinCity::VoxelWorld->SetSpecializationConstants_VolumetricLight_FS(constantsFS);
#endif
#if (LIVE_SHADER == LIVE_SHADER_UPSAMPLE)
			MinCity::VoxelWorld->SetSpecializationConstants_Upsample(constantsFS);
#endif
			vku::ShaderModule vert_{ device, SHADER_BINARY_DIR LIVE_SHADER_VERT, constantsVS };
			vku::ShaderModule frag_{ device, SHADER_BINARY_DIR LIVE_SHADER_FRAG, constantsFS };

			//########################## do pipeline creation now:
			vku::PipelineMaker pm(cached_pipeline_creation);
			
			// ************ order must match as added in original code for the actual pipeline creation in cVulkan that was chosen for live shader mode

			pm.replace_shader(0, vk::ShaderStageFlagBits::eVertex, vert_);		

			pm.replace_shader(1, vk::ShaderStageFlagBits::eFragment, frag_);	
			/*******/
			
			MinCity::Vulkan->WaitDeviceIdle();
			auto& cache = fw.pipelineCache();
			pipeline_out = std::move(pm.create(device, cache, *pipelineLayout, renderPass));

			/*******/

			bPipelinePending = false;
			tLast = high_resolution_clock::now(); // get latest time

			MinCity::Pause(false);
			
			return;
		}

		if (tNow - tLast > interval) {

			static PROCESS_INFORMATION	compiler_instance[2] = { {}, {} }; // zero out important

			int iExitCode(0);

			//######################### compile shaders //
			{
#ifdef LIVE_SHADER_VERT
				bool bNext(false);
				iExitCode = compile_shader(LIVE_SHADER_VERT, CACHED_SHADER_VERT, compiler_instance[0]);
				if (SUCCESS_COMPILE == iExitCode || NOCHANGE_COMPILE == iExitCode) {
					bNext = true;
				}

				if (bNext) {
#endif
					int const iExitPartCode = compile_shader(LIVE_SHADER_FRAG, CACHED_SHADER_FRAG, compiler_instance[1]);

					if (NOCHANGE_COMPILE != iExitPartCode) {
						iExitCode = iExitPartCode;
					}
#ifdef LIVE_SHADER_VERT
				}
#endif

			}
						
			/*
			#define ONGOING_COMPILE (-9)
			#define INITIAL_COMPILE (9)
			#define SUCCESS_COMPILE (0)
			#define UNKNOWN_COMPILE (-11)
			#define FAILED_CREATE_COMPILER_PROCESS (-1001)
			*/
			switch (iExitCode) {
			case NOCHANGE_COMPILE:
				break;
			case ONGOING_COMPILE:
			case INITIAL_COMPILE:
				FMT_NUKLEAR_DEBUG_OFF();
				break;
			case SUCCESS_COMPILE:
				bPipelinePending = true;
				FMT_NUKLEAR_DEBUG_OFF();
				break;
			case FAILED_CREATE_COMPILER_PROCESS:
				FMT_NUKLEAR_DEBUG(false, "failed to create compiler process!");
				break;
			default:
				FMT_NUKLEAR_DEBUG(false, "compile FAIL.");
				break;

			}
			tLast = high_resolution_clock::now(); // get latest time
		}
	}


}; // end ns

#endif