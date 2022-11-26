#pragma once
#include "Declarations.h"
#include "globals.h"
#include <tbb/tbb.h>
#include <Math/point2D_t.h>
#include <Utility/class_helper.h>
#include "IsoVoxel.h"
#include "tTime.h"
#include "cVulkan.h"
#include "eVoxelModels.h"
#include "voxelModelInstance.h"
#include "world.h"
#include "data.h"
#include "eInputEnabledBits.h"
#include "StreamingGrid.h"
#include <optional>
#include "volumetricOpacity.h"
#include "volumetricVisibility.h"
#include "cBlueNoise.h"

// forward decls:
struct ImagingMemoryInstance;
struct sRainInstance;
struct CityInfo;

BETTER_ENUM(eMouseButtonState, uint32_t const,
	INACTIVE = (1U << 31U),
	RELEASED = 0U,
	LEFT_PRESSED = (1U << 0U),
	RIGHT_PRESSED = (1U << 1U)
);

namespace world
{
	using mapRootIndex = tbb::concurrent_unordered_map<uint32_t const, point2D_t>;

	struct model_state {
		mapRootIndex const& __restrict					hshVoxelModelRootIndex;
		mapVoxelModelInstancesStatic const& __restrict	hshVoxelModelInstances_Static;
		mapVoxelModelInstancesDynamic const& __restrict	hshVoxelModelInstances_Dynamic;

		explicit model_state(mapRootIndex const& __restrict hshVoxelModelRootIndex_,
							 mapVoxelModelInstancesStatic const& __restrict hshVoxelModelInstances_Static_,
							 mapVoxelModelInstancesDynamic const& __restrict hshVoxelModelInstances_Dynamic_)
			: hshVoxelModelRootIndex(hshVoxelModelRootIndex_), hshVoxelModelInstances_Static(hshVoxelModelInstances_Static_), hshVoxelModelInstances_Dynamic(hshVoxelModelInstances_Dynamic_)
		{}
	};

	// if "condition (true/false)" of equal type, enable tempolate instantion of function
	template< bool cond, typename U >
	using resolvedType  = typename std::enable_if< cond, U >::type;

	class no_vtable cVoxelWorld : no_copy
	{
		static constexpr uint32_t const BLACKBODY_IMAGE_WIDTH = 512;
		
	private:
		typedef struct alignas(16) UniformState
		{
			UniformDecl::VoxelSharedUniform		Uniform;
			
			float							time;
			float							zoom;
		} UniformState;

		typedef struct Buffers
		{
			vku::GenericBuffer		reset_subgroup_layer_count_max,
									reset_shared_buffer;

			vku::double_buffer<vku::StorageBuffer>		subgroup_layer_count_max,
														shared_buffer;
		} Buffers;

	public:
		// Accesssors //
		uint32_t const						getRoadVisibleCount() const; // can be accessed in any function not in the internal RenderGrid()
		rect2D_t const __vectorcall			getVisibleGridBounds() const; // Grid Space (-x,-y) to (x, y) Coordinates Only
		rect2D_t const __vectorcall			getVisibleGridBoundsClamped() const; // Grid Space (-x,-y) to (x, y) Coordinates Only
		point2D_t const __vectorcall		getVisibleGridCenter() const; // Grid Space (-x,-y) to (x, y) Coordinates Only
		point2D_t const	__vectorcall		getHoveredVoxelIndex() const { return(_voxelIndexHover); } // updated only when valid, always contains the last known good voxelIndex that is hovered by the mouse

		v2_rotation_t const&				getYaw() const;
		float const							getZoomFactor() const;
		UniformState const& __vectorcall	getCurrentState() const { return(_currentState); }
		
		// Accessors
		Volumetric::voxelOpacity const& __restrict				getVolumetricOpacity() const { return(_OpacityMap); }
		vku::double_buffer<vku::StorageBuffer> const&			getSharedBuffer() const { return(_buffers.shared_buffer); }
		ImagingMemoryInstance* const& __restrict                getHeightMapImage() const { return(_heightmap); }
		
		// Mutators //
		Volumetric::voxelOpacity& __restrict					getVolumetricOpacity() { return(_OpacityMap); }

		void					    invalidateMotion() { _bMotionInvalidate = true; }
		
		// Accesorry //
		bool const __vectorcall zoomCamera(FXMVECTOR const xmExtents);
		void __vectorcall zoomCamera(float const inout);
		void __vectorcall rotateCamera(float const angle);
		void __vectorcall translateCamera(point2D_t const vDir);
		void XM_CALLCONV translateCamera(FXMVECTOR const xmDisplacement);
		void XM_CALLCONV translateCameraOrient(FXMVECTOR const xmDisplacement);

		void resetCameraAngleZoom(); // gradual over time
		void resetCamera(); // immediatte
		void setCameraTurnTable(bool const enable);
		void __vectorcall updateCameraFollow(XMVECTOR xmPosition, XMVECTOR xmVelocity, Volumetric::voxelModelInstance_Dynamic const* const instance, fp_seconds const& __restrict tDelta); // expects 3D coordinates

		// Main Methods //
		void LoadTextures(); // 1st
		void Initialize(); // 2nd
		void OnLoaded(tTime const& __restrict tNow);
		void Clear();
		void GarbageCollect(bool const bForce = false);

		template<bool const bEnable = true> // eInputEnabledBits
		uint32_t const InputEnable(uint32_t const bits); 

		bool const renderCompute(vku::compute_pass&& __restrict c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data);
		
		void Transfer(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, vku::UniformBuffer& __restrict ubo);
		void Transfer(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, vku::DynamicVertexBuffer* const* const& __restrict vbo);
		void AcquireTransferQueueOwnership(uint32_t const resource_index, vk::CommandBuffer& __restrict cb);

		
		void PreUpdate(bool const bPaused);																																		 //         0.) Order of operations, *each operation is dependent on previous operation*
		bool const UpdateOnce(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, bool const bPaused);															 //			1a.)
		void Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta, bool const bPaused, bool const bJustLoaded);												 //			1b.)
		void __vectorcall UpdateUniformState(float const tRemainder);																											 //			2.)
		void Render(uint32_t const resource_index) const;																														 //			3.)

		// #################
		void NewWorld();
		void ResetWorld();
		void SaveWorld();
		void LoadWorld();
		bool const PreviewWorld(std::string_view const szCityName, CityInfo&& __restrict info, ImagingMemoryInstance* const __restrict load_thumbnail) const; // load_thumbnail is expected to be created already, of BGRA format and equal to thumbnail dimensions
		void RefreshLoadList();
		vector<std::string> const& getLoadList() const;
		// ##################
		
		// input 
		void OnKey(int32_t const key, bool const down, bool const ctrl = false);
		bool const __vectorcall OnMouseMotion(FXMVECTOR xmMousePosition, bool const bIgnore);
		void OnMouseLeft(int32_t const state);
		void OnMouseRight(int32_t const state);
		void OnMouseLeftClick();
		void OnMouseRightClick();
		void OnMouseScroll(float const delta);
		void OnMouseInactive();

		// specialization constants
		void SetSpecializationConstants_ComputeLight(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_DepthResolve_FS(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_VolumetricLight_VS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_VolumetricLight_FS(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_Nuklear_FS(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_Resolve(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_Upsample(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_PostAA(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_PostAA_HDR(std::vector<vku::SpecializationConstant>& __restrict constants);
		
		void SetSpecializationConstants_Voxel_Basic_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, float const voxelSize);
		void SetSpecializationConstants_Voxel_VS_Common(std::vector<vku::SpecializationConstant>& __restrict constants, float const voxelSize);
		void SetSpecializationConstants_Voxel_GS_Common(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_VoxelTerrain_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_VoxelTerrain_VS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_VoxelTerrain_GS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_VoxelTerrain_FS(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_Voxel_Basic_VS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_Voxel_VS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_Voxel_GS(std::vector<vku::SpecializationConstant>& __restrict constants);
		void SetSpecializationConstants_Voxel_FS(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_VoxelRain_VS(std::vector<vku::SpecializationConstant>& __restrict constants);

		void SetSpecializationConstants_Voxel_ClearMask_FS(std::vector<vku::SpecializationConstant>& __restrict constants);
		
		// [[deprecated]] void SetSpecializationConstants_TextureShader(std::vector<vku::SpecializationConstant>& __restrict constants, uint32_t const shader);

		// macros for sampler sets
#define SAMPLER_SET_SINGLE vk::Sampler const& sampler
#define SAMPLER_SET_LINEAR vk::Sampler const& __restrict samplerLinearClamp, vk::Sampler const& __restrict samplerLinearRepeat, vk::Sampler const& __restrict samplerLinearMirroredRepeat
#define SAMPLER_SET_LINEAR_POINT vk::Sampler const& __restrict samplerLinearClamp, vk::Sampler const& __restrict samplerLinearRepeat, vk::Sampler const& __restrict samplerLinearMirroredRepeat, vk::Sampler const& __restrict samplerPointClamp, vk::Sampler const& __restrict samplerPointRepeat
#define SAMPLER_SET_LINEAR_POINT_ANISO vk::Sampler const& __restrict samplerLinearClamp, vk::Sampler const& __restrict samplerLinearRepeat, vk::Sampler const& __restrict samplerLinearMirroredRepeat, vk::Sampler const& __restrict samplerPointClamp, vk::Sampler const& __restrict samplerPointRepeat, vk::Sampler const& __restrict samplerAnisoClamp, vk::Sampler const& __restrict samplerAnisoRepeat

		void UpdateDescriptorSet_ComputeLight(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& __restrict samplerLinearBorder);
		//[[deprecated]] void UpdateDescriptorSet_TextureShader(vku::DescriptorSetUpdater& __restrict dsu, uint32_t const shader, SAMPLER_SET_STANDARD_POINT);

		void UpdateDescriptorSet_VolumetricLight(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict fullnormalImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, SAMPLER_SET_LINEAR_POINT);
		void UpdateDescriptorSet_VolumetricLightResolve(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, vk::ImageView const& __restrict fullvolumetricImageView, vk::ImageView const& __restrict fullreflectionImageView, SAMPLER_SET_LINEAR_POINT);
		void UpdateDescriptorSet_VolumetricLightUpsample(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict fulldepthImageView, vk::ImageView const& __restrict halfdepthImageView, vk::ImageView const& __restrict halfvolumetricImageView, vk::ImageView const& __restrict halfreflectionImageView, SAMPLER_SET_LINEAR_POINT);
		
		void UpdateDescriptorSet_PostAA_Post(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict colorImageView, vk::ImageView const& __restrict lastFrameView, SAMPLER_SET_LINEAR_POINT);
		void UpdateDescriptorSet_PostAA_Final(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict colorImageView, vk::ImageView const& __restrict guiImageView, SAMPLER_SET_LINEAR_POINT);
		
		void UpdateDescriptorSet_VoxelCommon(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict fullreflectionImageView, vk::ImageView const& __restrict lastColorImageView, SAMPLER_SET_LINEAR_POINT_ANISO);
		void UpdateDescriptorSet_Voxel_ClearMask(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict ds, SAMPLER_SET_LINEAR);

		__inline point2D_t const* const __restrict lookupVoxelModelInstanceRootIndex(uint32_t const hash) const;  // read-only access
		__inline point2D_t * const __restrict acquireVoxelModelInstanceRootIndex(uint32_t const hash);  // read-write access

		template<bool const Dynamic>
		__inline resolvedType<Dynamic, Volumetric::voxelModelInstance_Dynamic>* const __restrict lookupVoxelModelInstance(uint32_t const hash) const;
		template<bool const Dynamic>
		__inline resolvedType<!Dynamic, Volumetric::voxelModelInstance_Static>* const __restrict lookupVoxelModelInstance(uint32_t const hash) const;


		template<bool const Dynamic> // not so friendly usuage, but if you already have the voxelModel ....   returns [hash, instance] structured binding
		auto const placeVoxelModelInstanceAt(point2D_t const voxelIndex, Volumetric::voxB::voxelModel<Dynamic> const* const __restrict voxelModel, uint32_t const flags = 0);

		template<bool const Dynamic> // not so friendly usuage, but if you already have the voxelModel ....   returns [hash, instance, model] structured binding
		auto const placeProceduralVoxelModelInstanceAt(point2D_t const voxelIndex, uint32_t const flags = 0);


		template<int32_t const eVoxelModelGrpID> // public preferred usage returns [hash, instance] structured binding
		auto const placeVoxelModelInstanceAt(point2D_t const voxelIndex, uint32_t const modelIndex, uint32_t const flags = 0);

		template<typename TNonUpdateableGameObject, bool const Dynamic> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TNonUpdateableGameObject* const placeNonUpdateableInstanceAt(point2D_t const voxelIndex, Volumetric::voxB::voxelModel<Dynamic> const* const __restrict voxelModel, uint32_t const additional_flags = 0);

		template<typename TNonUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TNonUpdateableGameObject* const placeNonUpdateableInstanceAt(point2D_t const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags = 0);

		template<typename TNonUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TNonUpdateableGameObject* const __vectorcall placeNonUpdateableInstanceAt(FXMVECTOR const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags = 0);

		
		template<typename TUpdateableGameObject, bool const Dynamic> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TUpdateableGameObject* const placeUpdateableInstanceAt(point2D_t const voxelIndex, Volumetric::voxB::voxelModel<Dynamic> const* const __restrict voxelModel, uint32_t const additional_flags = 0);

		template<typename TUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TUpdateableGameObject* const placeUpdateableInstanceAt(point2D_t const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags = 0);

		template<typename TUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TUpdateableGameObject* const placeUpdateableInstanceAt(FXMVECTOR const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags = 0);


		template<typename TProceduralGameObject, bool const Dynamic> // allow polymorphic type to be passed, public preferred usage, returns the newly added game object instance
		TProceduralGameObject* const placeProceduralInstanceAt(point2D_t const voxelIndex, uint32_t const additional_flags = 0);

		uint32_t const hasVoxelModelInstanceAt(point2D_t const voxelIndex, int32_t const modelGroup, uint32_t const modelIndex) const;
		uint32_t const hasVoxelModelInstanceAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex) const;

		bool const hideVoxelModelInstanceAt(point2D_t const voxelIndex, int32_t const modelGroup, uint32_t const modelIndex, vector<Iso::voxelIndexHashPair>* const pRecordHidden = nullptr);
		bool const hideVoxelModelInstancesAt(rect2D_t voxelArea, int32_t const modelGroup, uint32_t const modelIndex, vector<Iso::voxelIndexHashPair>* const pRecordHidden = nullptr);

		bool const destroyVoxelModelInstanceAt(point2D_t const voxelIndex, uint32_t const hashTypes); // concurrency safe //
		bool const destroyVoxelModelInstancesAt(rect2D_t voxelArea, uint32_t const hashTypes); // concurrency safe
		void destroyVoxelModelInstance(uint32_t const hash);  // concurrency safe
		void destroyImmediatelyVoxelModelInstance(uint32_t const hash);  // not concurrency safe *** (public) // typically instances destroy themselves asynchronously, this is for special purposes

		void AsyncClears(uint32_t const resource_index);
		void CleanUp();

		size_t const numRootIndices() const { return(_hshVoxelModelRootIndex.size()); }
		size_t const numDynamicModelInstances() const { return(_hshVoxelModelInstances_Dynamic.size()); }
		size_t const numStaticModelInstances() const { return(_hshVoxelModelInstances_Static.size()); }

#ifndef NDEBUG
#ifdef DEBUG_VOXEL_RENDER_COUNTS
		size_t const numDynamicVoxelsRendered() const;
		size_t const numStaticVoxelsRendered() const;
		size_t const numTerrainVoxelsRendered() const;
		size_t const numLightVoxelsRendered() const;
#endif
#endif
		uvec4_v const __vectorcall blackbody(float const norm) const;
		void clearImporting() { _importing = false; }
		// [[deprecated]] void makeTextureShaderOutputsReadOnly(vk::CommandBuffer const& __restrict cb);
	private:
		// [[deprecated]] void createTextureShader(uint32_t const shader, std::wstring_view const szInputTexture);
		// [[deprecated]] void createTextureShader(uint32_t const shader, vku::GenericImage* const& __restrict input, bool const referenced = false, point2D_t const shader_dimensions = point2D_t{}, vk::Format const format = vk::Format::eB8G8R8A8Unorm);

		// placeXXXInstanceAt specializations

		// mouse occlusion
		void clearOcclusionInstances();
		void setOcclusionInstances();
		void updateMouseOcclusion(bool const bPaused);

		void __vectorcall UpdateUniformStateTarget(tTime const& __restrict tNow, tTime const& __restrict tStart, bool const bFirstUpdate = false);
		
		void createAllBuffers(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue);
		void OutputVoxelStats() const;
		void RenderTask_Normal(uint32_t const resource_index) const;
		void RenderTask_Minimap() const;
		void GenerateGround();
		
		XMVECTOR const XM_CALLCONV UpdateCamera(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
		void XM_CALLCONV HoverVoxel();
		
		bool const destroyVoxelModelInstanceAt(Iso::Voxel const& oVoxel, uint32_t const hashTypes);

		void CleanUpInstanceQueue();

		// ###############
		auto GridSnapshot() const -> std::pair<Iso::Voxel const* const __restrict, uint32_t const> const;
		void GridSnapshotLoad(Iso::Voxel const* const __restrict new_grid);
		// ###############

	private:
		UniformState						  _lastState, _currentState, _targetState;

		vku::TextureImage2D			* _terrainTexture,
			                        * _terrainTexture2,
									* _gridTexture;

		vku::TextureImage2D*		_blackbodyTexture;
		vku::TextureImageCube*      _spaceCubemapTexture;
		
		/*
		[[deprecated]] struct {
			vku::GenericImage*						input;	// can use any texture type as input
			vku::TextureImageStorage2D*				output;
			vku::IndirectBuffer*					indirect_buffer;
			
			UniformDecl::TextureShaderPushConstants push_constants;

			fp_seconds								accumulator;
			bool									referenced;	// input texture points to a already allocated texture for usage

		} _textureShader[eTextureShader::_size()];
		*/
		Imaging                    _heightmap;
		Imaging		               _blackbodyImage;

		Buffers						_buffers;

		Volumetric::voxelOpacity		_OpacityMap;
		Volumetric::voxelVisibility		_Visibility;

		struct {
			unordered_set<uint32_t>	fadedInstances[2];
			point2D_t				groundVoxelIndex,	// groundVoxelIndex & occlusionVoxelIndex are *read-only* for the rest of the program, do not modify.
									occlusionVoxelIndex;
			int32_t					state;
			fp_seconds				tToOcclude;
		} _occlusion;

		task_id_t					_AsyncClearTaskID;
		uint32_t					_mouseState;
		XMFLOAT2A					_vMouse;
		point2D_t					_voxelIndexHover;
		bool						_lastOcclusionQueryValid;

		XMFLOAT2A					_vDragLast;
		tTime						_tDragStart = zero_time_point;
		fp_seconds					_tBorderScroll;
		uint32_t					_inputEnabledBits;
		bool						_bMotionInvalidate = false, _bMotionDelta = false;
		bool						_bDraggingMouse = false;
		bool						_bIsEdgeScrolling = false;
		bool						_bCameraTurntable = false;
		bool						_onLoadedRequired;
		bool						_importing = false;

		std::array<float, 30> const	_sequence;
	private:
		typedef struct hashArea : no_copy {
			// order optimized for cache access
			bool	 dynamic;
			uint32_t hash;
			rect2D_t area;
			v2_rotation_t vR;
			
			hashArea() = default;
			hashArea(uint32_t const hash_, rect2D_t const area_)
				: dynamic(false), hash(hash_), area(area_)
			{}
			hashArea(uint32_t const hash_, rect2D_t const area_, v2_rotation_t const& vR_)
				: dynamic(true), hash(hash_), area(area_), vR(vR_)
			{}

			hashArea(hashArea&& src) noexcept
			{
				dynamic = std::move(src.dynamic);
				hash = std::move(src.hash); src.hash = 0;
				area = std::move(src.area);
				vR = std::move(src.vR);
			}
			hashArea& operator=(hashArea&& src) noexcept
			{
				dynamic = std::move(src.dynamic);
				hash = std::move(src.hash); src.hash = 0;
				area = std::move(src.area);
				vR = std::move(src.vR);

				return(*this);
			}

		} hashArea;

		tbb::concurrent_queue<hashArea>		_queueWatchedInstances,
											_queueCleanUpInstances;
		mapRootIndex						_hshVoxelModelRootIndex;	// from registered hash of root voxel
		mapVoxelModelInstancesStatic		_hshVoxelModelInstances_Static;	// from registered hash of root voxel
		mapVoxelModelInstancesDynamic		_hshVoxelModelInstances_Dynamic;	// from registered hash of root voxel
		
	private:
		void create_game_object(uint32_t const hash, uint32_t const gameobject_type);
	public:
		void upload_model_state(vector<model_root_index> const& __restrict data_rootIndex, vector<model_state_instance_static> const& __restrict data_models_static, vector<model_state_instance_dynamic> const& __restrict data_models_dynamic);
		world::model_state const download_model_state() const {
			return(world::model_state( _hshVoxelModelRootIndex, _hshVoxelModelInstances_Static, _hshVoxelModelInstances_Dynamic ));
		}

	public:
		cVoxelWorld();

#ifdef DEBUG_STORAGE_BUFFER
		vku::StorageBuffer*	DebugStorageBuffer;
		static inline UniformDecl::DebugStorageBuffer init_debug_buffer{ XMVECTORF32{0.0f, 0.0f, 0.0f, 0.0f}, {}, {} };
#endif

	private:
		StreamingGrid _streamingGrid; // should be *last* member in class **********************************************************************************
};

__inline point2D_t const* const __restrict cVoxelWorld::lookupVoxelModelInstanceRootIndex(uint32_t const hash) const	// read-only access
{
	if (hash) {
		// Get root voxel world coords
		auto const iFoundIndex = _hshVoxelModelRootIndex.find(hash);
		if (_hshVoxelModelRootIndex.cend() != iFoundIndex) {
			return(&iFoundIndex->second);
		}
	}
	return(nullptr);
}
__inline point2D_t * const __restrict cVoxelWorld::acquireVoxelModelInstanceRootIndex(uint32_t const hash) // read-write access
{
	if (hash) {
		// Get root voxel world coords
		auto const iFoundIndex = _hshVoxelModelRootIndex.find(hash);
		if (_hshVoxelModelRootIndex.cend() != iFoundIndex) {
			return(&iFoundIndex->second);
		}
	}
	return(nullptr);
}

// conditionally enabled template specializations based on template parameter condition for overloaded function distinct only by return type!!! : //
template<bool const Dynamic>
__inline resolvedType<Dynamic, Volumetric::voxelModelInstance_Dynamic>* const __restrict cVoxelWorld::lookupVoxelModelInstance(uint32_t const hash) const
{
	if (hash) {
		// resolve model id for dimensions
		auto const iFoundModel = _hshVoxelModelInstances_Dynamic.find(hash);
		if (_hshVoxelModelInstances_Dynamic.cend() != iFoundModel) {
			return(iFoundModel->second);
		}
	}
	return(nullptr);
}
template<bool const Dynamic>
__inline resolvedType<!Dynamic, Volumetric::voxelModelInstance_Static>* const __restrict cVoxelWorld::lookupVoxelModelInstance(uint32_t const hash) const
{
	if (hash) {
		// resolve model id for dimensions
		auto const iFoundModel = _hshVoxelModelInstances_Static.find(hash);
		if (_hshVoxelModelInstances_Static.cend() != iFoundModel) {
			return(iFoundModel->second);
		}
	}
	return(nullptr);
}


template<bool const Dynamic>
auto const cVoxelWorld::placeVoxelModelInstanceAt(point2D_t const voxelIndex, Volumetric::voxB::voxelModel<Dynamic> const* const __restrict voxelModel, uint32_t const flags)
{
	struct {

		uint32_t hash;
		typename std::conditional<Dynamic, Volumetric::voxelModelInstance_Dynamic, Volumetric::voxelModelInstance_Static>::type* __restrict instance;

	} binding = {};

	Iso::Voxel oVoxelRoot(getVoxelAt(voxelIndex));

	// if the area contains other instances, destroy them
	// disallowing the creation of this instance, kinda like a bulldoze happens first
	// user will wait till destruction is complete and "click" again to create new instance
	// otherwise (no existing instances) the creation starts immediately
	// in the event that an existing instance is still in the creation sequence, destruction of that instance starts irregardless
	uint32_t destroyExistingHashTypes(0);
	if (Volumetric::eVoxelModelInstanceFlags::DESTROY_EXISTING_DYNAMIC == (Volumetric::eVoxelModelInstanceFlags::DESTROY_EXISTING_DYNAMIC & flags))
	{
		destroyExistingHashTypes |= Iso::DYNAMIC_HASH;
	}
	if (Volumetric::eVoxelModelInstanceFlags::DESTROY_EXISTING_STATIC == (Volumetric::eVoxelModelInstanceFlags::DESTROY_EXISTING_STATIC & flags))
	{
		destroyExistingHashTypes |= Iso::STATIC_HASH;
	}

	bool bDestroyed(false);
	if (destroyExistingHashTypes) {
		rect2D_t const vWorldArea = r2D_add(voxelModel->_LocalArea, voxelIndex);

		bDestroyed = destroyVoxelModelInstancesAt(vWorldArea, destroyExistingHashTypes);
	}

	if ( !bDestroyed || Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING == (Volumetric::eVoxelModelInstanceFlags::IGNORE_EXISTING & flags)) // only if destruction is not neccessary
	{
		uint32_t const index = Iso::getNextAvailableHashIndex<Dynamic>(oVoxelRoot);

		if (0 == index) {
			// not available, no free hash indices
			FMT_LOG_WARN(GAME_LOG, "Voxel is full, {:s} instance cannot be added to world. Destroy first.", (Dynamic ? "Dynamic" : "Static"));
			return(binding);
		}

		// Generate *unique* (not already existing/used) 32 bit random number
		uint32_t newHash(0);
		do {
			newHash = PsuedoRandomNumber32(1); // avoiding zero
		} while (nullptr != lookupVoxelModelInstanceRootIndex(newHash));

		binding.hash = newHash;

		// create actual instance
		if constexpr (Dynamic)
		{
			binding.instance = Volumetric::voxelModelInstance_Dynamic::create(*voxelModel, binding.hash, voxelIndex, flags);
			_hshVoxelModelInstances_Dynamic[binding.hash] = binding.instance;
		}
		else
		{
			binding.instance = Volumetric::voxelModelInstance_Static::create(*voxelModel, binding.hash, voxelIndex, flags);// = modelID;
			_hshVoxelModelInstances_Static[binding.hash] = binding.instance;
		}

		rect2D_t const vLocalArea(voxelModel->_LocalArea);
		rect2D_t vWorldArea = r2D_add(vLocalArea, voxelIndex);

		// will be root voxel at end
		Iso::clearAsOwner(oVoxelRoot, index); // cleared temporarily

		// set as hash for this models instance on root voxel + voxels in area (below)
		if constexpr (Dynamic) {
			setVoxelsHashAt(vWorldArea, binding.hash, v2_rotation_t());
		}
		else {
			setVoxelsHashAt(vWorldArea, binding.hash);
		}

		Iso::setHash(oVoxelRoot, index, binding.hash); // ensure root/owner is also set

		Iso::clearEmissive(oVoxelRoot); // fix: "hovered" voxel is emissive - don't want the whole model area to be emissive too

		if (Volumetric::eVoxelModelInstanceFlags::GROUND_CONDITIONING == (Volumetric::eVoxelModelInstanceFlags::GROUND_CONDITIONING & flags))
		{
			// Get average height for area //
			uint32_t const uiAverageHeightStep = getVoxelsAt_AverageHeight(vWorldArea);

			auto const localvoxelIndex(getLocalVoxelIndexAt(voxelIndex));

			// all voxels in this area will be set to this average
			setVoxelsHeightAt(vWorldArea, uiAverageHeightStep);
			Iso::setHeightStep(localvoxelIndex, uiAverageHeightStep);

			// update elevation offset of instance
			binding.instance->setElevation(Iso::getRealHeight(localvoxelIndex));

			// leave a border of terrain around model
			vWorldArea = voxelArea_grow(vWorldArea, point2D_t(1, 1));

			// smooth border of model area with surrounding terrain
			smoothRect(vWorldArea);

			// go around perimeter doing lerp between border
			// *required* recompute adjacency for area as height of voxels has changed
			recomputeGroundAdjacency(vWorldArea);
				
		}
		// root is special

		// now flag as root (so that we did not set all voxels in area as roots before)
		if (!(Volumetric::eVoxelModelInstanceFlags::EMPTY_INSTANCE & flags)) {
			Iso::setAsOwner(oVoxelRoot, index);
		}
		setVoxelAt(voxelIndex, std::forward<Iso::Voxel const&& __restrict>(oVoxelRoot));

		// register hash for root voxel location lookup
		_hshVoxelModelRootIndex[binding.hash].v = voxelIndex.v;
	}
	return(binding);
}

template<typename T1, typename T2, typename T3>
struct many {
	T1 a;
	T2 b;
	T3 c;
};
template<bool const Dynamic>
auto const cVoxelWorld::placeProceduralVoxelModelInstanceAt(point2D_t const voxelIndex, uint32_t const flags)
{
	Volumetric::voxB::voxelModel<Dynamic>* model(nullptr);

	model = new Volumetric::voxB::voxelModel<Dynamic>(Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ); // create copy for unique usage by owner game object, owner must release model.

	auto const [hash, instance] = placeVoxelModelInstanceAt<Dynamic>(voxelIndex, model, flags);

#ifndef NDEBUG
	if (0 == (instance)) {
		FMT_LOG_WARN(VOX_LOG, "placeProceduralVoxelModelInstanceAt<{:s}> failed. at voxelIndex({:d},{:d})",
			(Dynamic ? "dynamic" : "static"), voxelIndex.x, voxelIndex.y);
	}
#endif

	return( many{hash, instance, model} ); // returns [hash, instance, model] structured binding
}


template<int32_t const eVoxelModelGrpID>
auto const cVoxelWorld::placeVoxelModelInstanceAt(point2D_t const voxelIndex, uint32_t const modelIndex, uint32_t const flags)
{
	// get model
	auto const* const __restrict voxelModel = Volumetric::getVoxelModel<eVoxelModelGrpID>(modelIndex);

	auto const binding = placeVoxelModelInstanceAt< (eVoxelModelGrpID < 0) >(voxelIndex, voxelModel, flags); 

#ifndef NDEBUG
	if (0 == (&binding)) {
		FMT_LOG_WARN(VOX_LOG, "placeVoxelModelInstanceAt<{:s}> failed. modelIndex({:d}) of modelGroup({:d}) at voxelIndex({:d},{:d})",
			((eVoxelModelGrpID < 0) ? "dynamic" : "static"), modelIndex, eVoxelModelGrpID, voxelIndex.x, voxelIndex.y);
	}
#endif
	return(binding); // returns [hash, instance] structured binding
}

template<typename TNonUpdateableGameObject, bool const Dynamic> // allow polymorphic type to be passed
TNonUpdateableGameObject* const cVoxelWorld::placeNonUpdateableInstanceAt(point2D_t const voxelIndex, Volumetric::voxB::voxelModel<Dynamic> const* const __restrict voxelModel, uint32_t const additional_flags)
{
	auto const [hash, instance] = placeVoxelModelInstanceAt<Dynamic>(voxelIndex, voxelModel, additional_flags);

	if (instance) {

		if (hash) {
			if constexpr (Dynamic) {

				return(&TNonUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]));
			}
			else {

				return(&TNonUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Static[hash]));
			}
		}
	}
#ifndef NDEBUG
	else {
		FMT_LOG_WARN(VOX_LOG, "placeNonUpdateableInstanceAt<{:s}> failed. at voxelIndex({:d},{:d})",
			(Dynamic ? "dynamic" : "static"), voxelIndex.x, voxelIndex.y);
	}
#endif
	return(nullptr);
}
template<typename TNonUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed
TNonUpdateableGameObject* const cVoxelWorld::placeNonUpdateableInstanceAt(point2D_t const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags)
{
	auto const [hash, instance] = placeVoxelModelInstanceAt<eVoxelModelGrpID>(voxelIndex, modelIndex, additional_flags);

	if (instance) {

		if (hash) {
			if constexpr (eVoxelModelGrpID < 0) {

				return(&TNonUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]));
			}
			else {

				return(&TNonUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Static[hash]));
			}
		}
	}
#ifndef NDEBUG
	else {
		FMT_LOG_WARN(VOX_LOG, "placeNonUpdateableInstanceAt<{:s}> failed. modelIndex({:d}) of modelGroup({:d}) at voxelIndex({:d},{:d})",
			((eVoxelModelGrpID < 0) ? "dynamic" : "static"), modelIndex, eVoxelModelGrpID, voxelIndex.x, voxelIndex.y);
	}
#endif
	return(nullptr);
}

template<typename TNonUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed
TNonUpdateableGameObject* const cVoxelWorld::placeNonUpdateableInstanceAt(FXMVECTOR const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags)
{
	point2D_t const ivoxelIndex(v2_to_p2D_rounded(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(voxelIndex)));

	auto const instance = placeNonUpdateableInstanceAt<TNonUpdateableGameObject, eVoxelModelGrpID>(ivoxelIndex, modelIndex, additional_flags);
	if (instance) {

		instance->getModelInstance()->setElevation(XMVectorGetY(voxelIndex));
	}
	return(instance);
}

template<typename TUpdateableGameObject, bool const Dynamic> // allow polymorphic type to be passed
TUpdateableGameObject* const cVoxelWorld::placeUpdateableInstanceAt(point2D_t const voxelIndex, Volumetric::voxB::voxelModel<Dynamic> const* const __restrict voxelModel, uint32_t const additional_flags)
{
	auto const [hash, instance] = placeVoxelModelInstanceAt<Dynamic>(voxelIndex, voxelModel, Volumetric::eVoxelModelInstanceFlags::UPDATEABLE | additional_flags);

	if (instance) {

		if (hash) {
			if constexpr (Dynamic) {

				return(&TUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]));
			}
			else {

				return(&TUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Static[hash]));
			}
		}
	}
#ifndef NDEBUG
	else {
		FMT_LOG_WARN(VOX_LOG, "placeUpdateableInstanceAt<{:s}> failed. at voxelIndex({:d},{:d})",
			(Dynamic ? "dynamic" : "static"), voxelIndex.x, voxelIndex.y);
	}
#endif
	return(nullptr);
}
template<typename TUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed
TUpdateableGameObject* const cVoxelWorld::placeUpdateableInstanceAt(point2D_t const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags)
{
	auto const [hash, instance] = placeVoxelModelInstanceAt<eVoxelModelGrpID>(voxelIndex, modelIndex, Volumetric::eVoxelModelInstanceFlags::UPDATEABLE | additional_flags);

	if (instance) {

		if (hash) {
			if constexpr (eVoxelModelGrpID < 0) {

				return(&TUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash]));
			}
			else {

				return(&TUpdateableGameObject::emplace_back(_hshVoxelModelInstances_Static[hash]));
			}
		}
	}
#ifndef NDEBUG
	else {
		FMT_LOG_WARN(VOX_LOG, "placeUpdateableInstanceAt<{:s}> failed. modelIndex({:d}) of modelGroup({:d}) at voxelIndex({:d},{:d})",
			((eVoxelModelGrpID < 0) ? "dynamic" : "static"), modelIndex, eVoxelModelGrpID, voxelIndex.x, voxelIndex.y);
	}
#endif
	return(nullptr);
}
template<typename TUpdateableGameObject, int32_t const eVoxelModelGrpID> // allow polymorphic type to be passed
TUpdateableGameObject* const cVoxelWorld::placeUpdateableInstanceAt(FXMVECTOR const voxelIndex, uint32_t const modelIndex, uint32_t const additional_flags)
{
	point2D_t const ivoxelIndex(v2_to_p2D_rounded(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(voxelIndex)));

	auto const instance = placeUpdateableInstanceAt<TUpdateableGameObject, eVoxelModelGrpID>(ivoxelIndex, modelIndex, additional_flags);
	if (instance) {

		instance->getModelInstance()->setElevation(XMVectorGetY(voxelIndex));
	}
	return(instance);
}

template<typename TProceduralGameObject, bool const Dynamic> // allow polymorphic type to be passed
TProceduralGameObject* const cVoxelWorld::placeProceduralInstanceAt(point2D_t const voxelIndex, uint32_t const additional_flags)
{
	auto const [hash, instance, model] = placeProceduralVoxelModelInstanceAt<Dynamic>(voxelIndex, Volumetric::eVoxelModelInstanceFlags::PROCEDURAL | Volumetric::eVoxelModelInstanceFlags::UPDATEABLE | additional_flags);

	if (instance && model) {

		if (hash) {
			if constexpr (Dynamic) {

				return(&TProceduralGameObject::emplace_back(_hshVoxelModelInstances_Dynamic[hash], model));
			}
			else {

				return(&TProceduralGameObject::emplace_back(_hshVoxelModelInstances_Static[hash], model));
			}
		}
	}
#ifndef NDEBUG
	else {
		FMT_LOG_WARN(VOX_LOG, "placeProceduralInstanceAt<{:s}> failed. at voxelIndex({:d},{:d})",
			(Dynamic ? "dynamic" : "static"), voxelIndex.x, voxelIndex.y);
	}
#endif
	return(nullptr);
}

template<bool const bEnable> // eInputEnabledBits
uint32_t const cVoxelWorld::InputEnable(uint32_t const bits)
{
	if constexpr (bEnable) {
		_inputEnabledBits |= bits; // set the specified bits
	}
	else {
		_inputEnabledBits &= ~bits; // clear the specified bits
	}

	return(_inputEnabledBits); // return the current inputenabledbits state
}

} // end ns world


