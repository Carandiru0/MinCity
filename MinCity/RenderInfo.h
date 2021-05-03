#pragma once
#include "cVulkan.h"

typedef struct sDescriptorSetInfo
{
	vk::UniqueDescriptorSetLayout const&__restrict	descLayout;
	std::vector<vk::DescriptorSet>const&__restrict	sets;
	vku::UniformBuffer const* const __restrict		ubo;

	sDescriptorSetInfo(vk::UniqueDescriptorSetLayout const&__restrict	indescLayout,
		std::vector<vk::DescriptorSet> const&__restrict	insets,
		vku::UniformBuffer const* const __restrict		inubo = nullptr)
		: descLayout(indescLayout), sets(insets), ubo(inubo)
	{}
} DescriptorSetInfo;

typedef struct sRenderingInfo
{
	vk::RenderPassBeginInfo const& __restrict		rpbi;
	vk::UniquePipelineLayout const&__restrict		pipelineLayout;
	vk::Pipeline const&__restrict					pipeline;
	vk::UniqueDescriptorSetLayout const&__restrict	descLayout;
	std::vector<vk::DescriptorSet> const&__restrict	sets;

	sRenderingInfo(vk::RenderPassBeginInfo const& __restrict	inrpbi,
		vk::UniquePipelineLayout const&	__restrict		inpipelineLayout,
		vk::Pipeline const&__restrict					inpipeline,
		vk::UniqueDescriptorSetLayout const&__restrict	indescLayout,
		std::vector<vk::DescriptorSet> const&__restrict	insets)
		: rpbi(inrpbi), pipelineLayout(inpipelineLayout), pipeline(inpipeline), descLayout(indescLayout), sets(insets)
	{}
} RenderingInfo;



