#pragma once

#include "../../CookBook/SampleBase.hpp"

#include "../../Math/Matrix4.hpp"

#include "../../Mesh.hpp"
#include "../../Window.hpp"

#include "../../VertexBuffer.hpp"
#include "../../StagingBuffer.hpp"
#include "../../UniformBuffer.hpp"

class Postprocessing : public SampleBase
{
	public:
		nu::Mesh mSkybox;
		nu::VertexBuffer::Ptr mSkyboxVertexBuffer;
		VulkanImageHelperPtr mSkyboxCubemap;

		nu::Mesh mModel;
		nu::VertexBuffer::Ptr mModelVertexBuffer;

		nu::VertexBuffer::Ptr mPostprocessVertexBuffer;
		VulkanImageHelperPtr mSceneImage;
		VulkanFencePtr mSceneFence;

		nu::UniformBuffer::Ptr mUniformBuffer;
		nu::StagingBuffer::Ptr mStagingBuffer;

		VulkanDescriptorSetLayoutPtr mDescriptorSetLayout;
		VulkanDescriptorPoolPtr mDescriptorPool;
		std::vector<VulkanDescriptorSetPtr> mDescriptorSets;

		VulkanDescriptorSetLayoutPtr mPostprocessDescriptorSetLayout;
		VulkanDescriptorPoolPtr mPostprocessDescriptorPool;
		std::vector<VulkanDescriptorSetPtr> mPostprocessDescriptorSets;

		VulkanRenderPassPtr mRenderPass;
		VulkanPipelineLayoutPtr mPipelineLayout;
		VulkanPipelineLayoutPtr mPostprocessPipelineLayout;
		std::vector<VulkanGraphicsPipelinePtr> mPipelines;
		enum PipelineNames
		{
			SkyboxPipeline = 0,
			ModelPipeline = 1,
			PostprocessPipeline = 2,
			Count
		};

		uint32_t mFrameIndex = 0;

		virtual bool initialize(VulkanWindowParameters windowParameters) override 
		{
			if (!initializeVulkan(windowParameters, nullptr)) 
			{
				return false;
			}

			mSceneFence = VulkanDevice::get().createFence(true);
			if (mSceneFence == nullptr)
			{
				return false;
			}

			// Vertex data - Model
			if (!mModel.loadFromFile("../../Data/Models/sphere.obj", true, false, false, true))
			{
				return false;
			}
			mModelVertexBuffer = nu::VertexBuffer::createVertexBuffer(VulkanDevice::get(), mModel.size());
			if (!mModelVertexBuffer || !mModelVertexBuffer->updateAndWait(mModel.size(), &mModel.data[0], 0, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, mFramesResources.front().mCommandBuffer.get(), mGraphicsQueue, {}, 50000000))
			{
				return false;
			}

			// Vertex data - Skybox
			if (!mSkybox.loadFromFile("../../Data/Models/cube.obj", false, false, false, false))
			{
				return false;
			}
			mSkyboxVertexBuffer = nu::VertexBuffer::createVertexBuffer(VulkanDevice::get(), mSkybox.size());
			if (!mSkyboxVertexBuffer || !mSkyboxVertexBuffer->updateAndWait(mSkybox.size(), &mSkybox.data[0], 0, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, mFramesResources.front().mCommandBuffer.get(), mGraphicsQueue, {}, 50000000))
			{
				return false;
			}

			// Fullscreen quad for postprocess
			std::vector<float> vertices = {
				// positions
				-1.0f, -1.0f, 0.0f,
				-1.0f,  1.0f, 0.0f,
				1.0f, -1.0f, 0.0f,
				1.0f, -1.0f, 0.0f,
				-1.0f,  1.0f, 0.0f,
				1.0f,  1.0f, 0.0f,
			};
			mPostprocessVertexBuffer = nu::VertexBuffer::createVertexBuffer(VulkanDevice::get(), (uint32_t)vertices.size() * sizeof(float));
			if (!mPostprocessVertexBuffer || !mPostprocessVertexBuffer->updateAndWait((uint32_t)vertices.size() * sizeof(float), &vertices[0], 0, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, mFramesResources.front().mCommandBuffer.get(), mGraphicsQueue, {}, 50000000))
			{
				return false;
			}

			// Staging buffer & Uniform buffer
			mUniformBuffer = nu::UniformBuffer::createUniformBuffer(VulkanDevice::get(), 2 * 16 * sizeof(float));
			if (mUniformBuffer == nullptr)
			{
				return false;
			}
			mStagingBuffer = mUniformBuffer->createStagingBuffer();
			if (mStagingBuffer == nullptr)
			{
				return false;
			}
			if (!updateStagingBuffer(true)) 
			{
				return false;
			}

			// Cubemap
			mSkyboxCubemap = VulkanImageHelper::createCombinedImageSampler(VulkanDevice::get(), VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { 1024, 1024, 1 }, 1, 6,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT, VK_FILTER_LINEAR,
				VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, 0.0f, 1.0f, false, 1.0f, false, VK_COMPARE_OP_ALWAYS, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, false);
			std::vector<std::string> cubemapImages = {
				"../../Data/Textures/Skansen/posx.jpg",
				"../../Data/Textures/Skansen/negx.jpg",
				"../../Data/Textures/Skansen/posy.jpg",
				"../../Data/Textures/Skansen/negy.jpg",
				"../../Data/Textures/Skansen/posz.jpg",
				"../../Data/Textures/Skansen/negz.jpg"
			};

			for (size_t i = 0; i < cubemapImages.size(); i++) 
			{
				std::vector<unsigned char> cubemapImageData;
				int imageDataSize;
				if (!loadTextureDataFromFile(cubemapImages[i].c_str(), 4, cubemapImageData, nullptr, nullptr, nullptr, &imageDataSize)) 
				{
					return false;
				}
				VkImageSubresourceLayers imageSubresource = {
					VK_IMAGE_ASPECT_COLOR_BIT,    // VkImageAspectFlags     aspectMask
					0,                            // uint32_t               mipLevel
					static_cast<uint32_t>(i),     // uint32_t               baseArrayLayer
					1                             // uint32_t               layerCount
				};
				mGraphicsQueue->useStagingBufferToUpdateImageAndWait(imageDataSize, &cubemapImageData[0],
					mSkyboxCubemap->getImage(), imageSubresource, { 0, 0, 0 }, { 1024, 1024, 1 }, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					mFramesResources.front().mCommandBuffer.get(), {}, 50000000);
			}

			// Scene image (color attachment in 1st subpass, input attachment in 2nd subpass
			mSceneImage = VulkanImageHelper::createInputAttachment(VulkanDevice::get(), VK_IMAGE_TYPE_2D, mSwapchain->getFormat(), { mSwapchain->getSize().width, mSwapchain->getSize().height, 1 }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
			if (mSceneImage == nullptr)
			{
				return false;
			}

			// Descriptor sets with uniform buffer
			std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings = {
				{
					0,                                          // uint32_t             binding
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // VkDescriptorType     descriptorType
					1,                                          // uint32_t             descriptorCount
					VK_SHADER_STAGE_VERTEX_BIT,                 // VkShaderStageFlags   stageFlags
					nullptr                                     // const VkSampler    * pImmutableSamplers
				},
				{
					1,                                          // uint32_t             binding
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // VkDescriptorType     descriptorType
					1,                                          // uint32_t             descriptorCount
					VK_SHADER_STAGE_FRAGMENT_BIT,               // VkShaderStageFlags   stageFlags
					nullptr                                     // const VkSampler    * pImmutableSamplers
				}
			};
			mDescriptorSetLayout = VulkanDevice::get().createDescriptorSetLayout(descriptorSetLayoutBindings);
			if (mDescriptorSetLayout == nullptr || !mDescriptorSetLayout->isInitialized())
			{
				return false;
			}

			std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
				{
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // VkDescriptorType     type
					1                                           // uint32_t             descriptorCount
				},
				{
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // VkDescriptorType     type
					1                                           // uint32_t             descriptorCount
				}
			};
			mDescriptorPool = VulkanDevice::get().createDescriptorPool(false, 1, descriptorPoolSizes);
			if (mDescriptorPool == nullptr || !mDescriptorPool->isInitialized())
			{
				return false;
			}

			// TODO : Allocate more than one at once
			mDescriptorSets.resize(1);
			mDescriptorSets[0] = mDescriptorPool->allocateDescriptorSet(mDescriptorSetLayout.get());
			if (mDescriptorSets[0] == nullptr || !mDescriptorSets[0]->isInitialized())
			{
				return false;
			}

			// Postprocess Descriptor sets with input attachment
			std::vector<VkDescriptorSetLayoutBinding> postprocessDescriptorSetLayoutBindings = {
				{
					0,                                          // uint32_t             binding
					VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,        // VkDescriptorType     descriptorType
					1,                                          // uint32_t             descriptorCount
					VK_SHADER_STAGE_FRAGMENT_BIT,               // VkShaderStageFlags   stageFlags
					nullptr                                     // const VkSampler    * pImmutableSamplers
				}
			};
			mPostprocessDescriptorSetLayout = VulkanDevice::get().createDescriptorSetLayout(postprocessDescriptorSetLayoutBindings);
			if (mPostprocessDescriptorSetLayout == nullptr || !mPostprocessDescriptorSetLayout->isInitialized())
			{
				return false;
			}

			std::vector<VkDescriptorPoolSize> postprocessDescriptorPoolSizes = {
				{
					VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,        // VkDescriptorType     type
					1                                           // uint32_t             descriptorCount
				},
			};
			mPostprocessDescriptorPool = VulkanDevice::get().createDescriptorPool(false, 1, postprocessDescriptorPoolSizes);
			if (mPostprocessDescriptorPool == nullptr || !mPostprocessDescriptorPool->isInitialized())
			{
				return false;
			}

			// TODO : Allocate more than one at once
			mPostprocessDescriptorSets.resize(1);
			mPostprocessDescriptorSets[0] = mPostprocessDescriptorPool->allocateDescriptorSet(mPostprocessDescriptorSetLayout.get());
			if (mPostprocessDescriptorSets[0] == nullptr || !mPostprocessDescriptorSets[0]->isInitialized())
			{
				return false;
			}

			// Update descriptor
			// TODO : Update more than one at once
			mUniformBuffer->updateDescriptor(mDescriptorSets[0].get(), 0, 0);

			VulkanImageDescriptorInfo imageDescriptorUpdate = {
				mDescriptorSets[0]->getHandle(),            // VkDescriptorSet                      TargetDescriptorSet
				1,                                          // uint32_t                             TargetDescriptorBinding
				0,                                          // uint32_t                             TargetArrayElement
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // VkDescriptorType                     TargetDescriptorType
				{                                           // std::vector<VkDescriptorImageInfo>   ImageInfos
					{
						mSkyboxCubemap->getSampler()->getHandle(),      // VkSampler                            sampler
						mSkyboxCubemap->getImageView()->getHandle(),    // VkImageView                          imageView
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  // VkImageLayout                        imageLayout
					}
				}
			};

			VulkanImageDescriptorInfo sceneImageDescriptorUpdate = {
				mPostprocessDescriptorSets[0]->getHandle(), // VkDescriptorSet                      TargetDescriptorSet
				0,                                          // uint32_t                             TargetDescriptorBinding
				0,                                          // uint32_t                             TargetArrayElement
				VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,        // VkDescriptorType                     TargetDescriptorType
			{                                           // std::vector<VkDescriptorImageInfo>   ImageInfos
				{
					VK_NULL_HANDLE,                           // VkSampler                            sampler
					mSceneImage->getImageView()->getHandle(), // VkImageView                          imageView
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  // VkImageLayout                        imageLayout
				}
			}
			};

			VulkanDevice::get().updateDescriptorSets({ imageDescriptorUpdate, sceneImageDescriptorUpdate }, {}, {}, {});


			// Render pass
			mRenderPass = VulkanDevice::get().initRenderPass();

			mRenderPass->addAttachment(mSwapchain->getFormat());
			mRenderPass->setAttachmentLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
			mRenderPass->setAttachmentFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			mRenderPass->addAttachment(mSwapchain->getDepthFormat());
			mRenderPass->setAttachmentLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
			mRenderPass->setAttachmentFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			mRenderPass->addAttachment(mSwapchain->getFormat());
			mRenderPass->setAttachmentStoreOp(VK_ATTACHMENT_STORE_OP_STORE);
			mRenderPass->setAttachmentFinalLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

			mRenderPass->addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
			mRenderPass->addColorAttachmentToSubpass(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			mRenderPass->addDepthStencilAttachmentToSubpass(1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			mRenderPass->addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
			mRenderPass->addInputAttachmentToSubpass(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			mRenderPass->addColorAttachmentToSubpass(2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			mRenderPass->addDependency(
				0,                                              // uint32_t                   srcSubpass
				1,                                              // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,          // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags              srcAccessMask
				VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,            // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			);
			mRenderPass->addDependency(
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   srcSubpass
				1,                                              // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,              // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_MEMORY_READ_BIT,                      // VkAccessFlags              srcAccessMask
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			);
			mRenderPass->addDependency(
				1,                                              // uint32_t                   srcSubpass
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,              // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags              srcAccessMask
				VK_ACCESS_MEMORY_READ_BIT,                      // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			);

			if (!mRenderPass->create())
			{
				return false;
			}

			// Graphics pipeline

			std::vector<VkPushConstantRange> pushConstantRanges = {
				{
					VK_SHADER_STAGE_FRAGMENT_BIT,   // VkShaderStageFlags     stageFlags
					0,                              // uint32_t               offset
					sizeof(float) * 4               // uint32_t               size
				}
			};
			mPipelineLayout = VulkanDevice::get().createPipelineLayout({ mDescriptorSetLayout->getHandle() }, pushConstantRanges);
			if (mPipelineLayout == nullptr || !mPipelineLayout->isInitialized())
			{
				return false;
			}
			mPostprocessPipelineLayout = VulkanDevice::get().createPipelineLayout({ mPostprocessDescriptorSetLayout->getHandle() }, pushConstantRanges);
			if (mPostprocessPipelineLayout == nullptr || !mPostprocessPipelineLayout->isInitialized())
			{
				return false;
			}

			mPipelines.resize(PipelineNames::Count);
			mPipelines[PipelineNames::SkyboxPipeline] = VulkanDevice::get().initGraphicsPipeline(*mPipelineLayout, *mRenderPass, nullptr);
			mPipelines[PipelineNames::ModelPipeline] = VulkanDevice::get().initGraphicsPipeline(*mPipelineLayout, *mRenderPass, nullptr);
			mPipelines[PipelineNames::PostprocessPipeline] = VulkanDevice::get().initGraphicsPipeline(*mPostprocessPipelineLayout, *mRenderPass, nullptr);

			VulkanGraphicsPipeline* skyboxPipeline = mPipelines[PipelineNames::SkyboxPipeline].get();
			VulkanGraphicsPipeline* modelPipeline = mPipelines[PipelineNames::ModelPipeline].get();
			VulkanGraphicsPipeline* postprocessPipeline = mPipelines[PipelineNames::PostprocessPipeline].get();

			skyboxPipeline->setSubpass(0);
			modelPipeline->setSubpass(0);
			postprocessPipeline->setSubpass(1);

			VulkanShaderModulePtr skyboxVertexShaderModule = VulkanDevice::get().initShaderModule();
			if (skyboxVertexShaderModule == nullptr || !skyboxVertexShaderModule->loadFromFile("../../Examples/10 - Postprocessing/skybox.vert.spv"))
			{
				return false;
			}
			skyboxVertexShaderModule->setVertexEntrypointName("main");
			VulkanShaderModulePtr skyboxFragmentShaderModule = VulkanDevice::get().initShaderModule();
			if (skyboxFragmentShaderModule == nullptr || !skyboxFragmentShaderModule->loadFromFile("../../Examples/10 - Postprocessing/skybox.frag.spv"))
			{
				return false;
			}
			skyboxFragmentShaderModule->setFragmentEntrypointName("main");
			if (!skyboxVertexShaderModule->create() || !skyboxFragmentShaderModule->create())
			{
				return false;
			}

			VulkanShaderModulePtr modelVertexShaderModule = VulkanDevice::get().initShaderModule();
			if (modelVertexShaderModule == nullptr || !modelVertexShaderModule->loadFromFile("../../Examples/10 - Postprocessing/model.vert.spv"))
			{
				return false;
			}
			modelVertexShaderModule->setVertexEntrypointName("main");
			VulkanShaderModulePtr modelFragmentShaderModule = VulkanDevice::get().initShaderModule();
			if (modelFragmentShaderModule == nullptr || !modelFragmentShaderModule->loadFromFile("../../Examples/10 - Postprocessing/model.frag.spv"))
			{
				return false;
			}
			modelFragmentShaderModule->setFragmentEntrypointName("main");
			if (!modelVertexShaderModule->create() || !modelFragmentShaderModule->create())
			{
				return false;
			}

			VulkanShaderModulePtr postprocessVertexShaderModule = VulkanDevice::get().initShaderModule();
			if (postprocessVertexShaderModule == nullptr || !postprocessVertexShaderModule->loadFromFile("../../Examples/10 - Postprocessing/postprocess.vert.spv"))
			{
				return false;
			}
			postprocessVertexShaderModule->setVertexEntrypointName("main");
			VulkanShaderModulePtr postprocessFragmentShaderModule = VulkanDevice::get().initShaderModule();
			if (postprocessFragmentShaderModule == nullptr || !postprocessFragmentShaderModule->loadFromFile("../../Examples/10 - Postprocessing/postprocess.frag.spv"))
			{
				return false;
			}
			postprocessFragmentShaderModule->setFragmentEntrypointName("main");
			if (!postprocessVertexShaderModule->create() || !postprocessFragmentShaderModule->create())
			{
				return false;
			}


			skyboxPipeline->addShaderModule(skyboxVertexShaderModule.get());
			skyboxPipeline->addShaderModule(skyboxFragmentShaderModule.get());
			modelPipeline->addShaderModule(modelVertexShaderModule.get());
			modelPipeline->addShaderModule(modelFragmentShaderModule.get());
			postprocessPipeline->addShaderModule(postprocessVertexShaderModule.get());
			postprocessPipeline->addShaderModule(postprocessFragmentShaderModule.get());

			skyboxPipeline->addVertexBinding(0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX);
			skyboxPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			modelPipeline->addVertexBinding(0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX);
			modelPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			modelPipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float));
			postprocessPipeline->addVertexBinding(0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX);
			postprocessPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

			skyboxPipeline->setRasterizationState(false, false, VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, false, 0.0f, 0.0f, 0.0f, 1.0f);

			skyboxPipeline->addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			skyboxPipeline->addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
			modelPipeline->addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			modelPipeline->addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
			postprocessPipeline->addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			postprocessPipeline->addDynamicState(VK_DYNAMIC_STATE_SCISSOR);

			if (!skyboxPipeline->create() || !modelPipeline->create() || !postprocessPipeline->create())
			{
				return false;
			}

			return true;
		}

		virtual bool draw() override 
		{
			auto prepareFrame = [&](VulkanCommandBuffer* commandBuffer, uint32_t swapchainImageIndex, VulkanFramebuffer* framebuffer) 
			{
				if (!commandBuffer->beginRecording(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr))
				{
					return false;
				}

				if (mStagingBuffer->needToSend())
				{
					mStagingBuffer->send(commandBuffer);
				}

				if (mPresentQueue->getFamilyIndex() != mGraphicsQueue->getFamilyIndex()) 
				{
					VulkanImageTransition imageTransitionBeforeDrawing = {
						mSwapchain->getImageHandle(swapchainImageIndex), // VkImage             image
						VK_ACCESS_MEMORY_READ_BIT,                // VkAccessFlags        currentAccess
						VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,     // VkAccessFlags        newAccess
						VK_IMAGE_LAYOUT_UNDEFINED,                // VkImageLayout        currentLayout
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, // VkImageLayout        newLayout
						mPresentQueue->getFamilyIndex(),          // uint32_t             currentQueueFamily
						mGraphicsQueue->getFamilyIndex(),         // uint32_t             newQueueFamily
						VK_IMAGE_ASPECT_COLOR_BIT                 // VkImageAspectFlags   aspect
					};
					commandBuffer->setImageMemoryBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, { imageTransitionBeforeDrawing } );
				}

				// Drawing
				commandBuffer->beginRenderPass(mRenderPass->getHandle(), framebuffer->getHandle(), { { 0, 0 }, mSwapchain->getSize() }, { { 0.1f, 0.2f, 0.3f, 1.0f },{ 1.0f, 0 } }, VK_SUBPASS_CONTENTS_INLINE);

				// Subpass 0

				uint32_t width = mSwapchain->getSize().width;
				uint32_t height = mSwapchain->getSize().height;

				VkViewport viewport = {
					0.0f,                                       // float    x
					0.0f,                                       // float    y
					static_cast<float>(width),                  // float    width
					static_cast<float>(height),                 // float    height
					0.0f,                                       // float    minDepth
					1.0f,                                       // float    maxDepth
				};
				commandBuffer->setViewportStateDynamically(0, { viewport });

				VkRect2D scissor = {
					{                                           // VkOffset2D     offset
						0,                                            // int32_t        x
						0                                             // int32_t        y
					},
					{                                           // VkExtent2D     extent
						width,                                      // uint32_t       width
						height                                      // uint32_t       height
					}
				};
				commandBuffer->setScissorStateDynamically(0, { scissor });

				commandBuffer->bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout->getHandle(), 0, { mDescriptorSets[0].get() }, {});

				float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				commandBuffer->provideDataToShadersThroughPushConstants(mPipelineLayout->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, &p[0]);

				// Draw model
				commandBuffer->bindPipeline(mPipelines[PipelineNames::ModelPipeline].get());
				mModelVertexBuffer->bindTo(commandBuffer, 0, 0);
				for (size_t i = 0; i < mModel.parts.size(); i++) 
				{
					commandBuffer->drawGeometry(mModel.parts[i].vertexCount, 1, mModel.parts[i].vertexOffset, 0);
				}

				// Draw skybox
				commandBuffer->bindPipeline(mPipelines[PipelineNames::SkyboxPipeline].get());
				mSkyboxVertexBuffer->bindTo(commandBuffer, 0, 0);
				for (size_t i = 0; i < mSkybox.parts.size(); i++)
				{
					commandBuffer->drawGeometry(mSkybox.parts[i].vertexCount, 1, mSkybox.parts[i].vertexOffset, 0);
				}

				// Subpass 1
				commandBuffer->progressToTheNextSubpass(VK_SUBPASS_CONTENTS_INLINE);

				commandBuffer->bindPipeline(mPipelines[PipelineNames::PostprocessPipeline].get());
				commandBuffer->bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, mPostprocessPipelineLayout->getHandle(), 0, { mPostprocessDescriptorSets[0].get() }, {});
				mPostprocessVertexBuffer->bindTo(commandBuffer, 0, 0);
				float time = mTimerState.getTime();
				commandBuffer->provideDataToShadersThroughPushConstants(mPostprocessPipelineLayout->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &time);
				commandBuffer->drawGeometry(6, 1, 0, 0);

				commandBuffer->endRenderPass();

				if (mPresentQueue->getFamilyIndex() != mGraphicsQueue->getFamilyIndex())
				{
					VulkanImageTransition imageTransitionBeforePresent = {
						mSwapchain->getImageHandle(swapchainImageIndex),  // VkImage            image
						VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,     // VkAccessFlags        currentAccess
						VK_ACCESS_MEMORY_READ_BIT,                // VkAccessFlags        newAccess
						VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,          // VkImageLayout        currentLayout
						VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,          // VkImageLayout        newLayout
						mGraphicsQueue->getFamilyIndex(),         // uint32_t             currentQueueFamily
						mPresentQueue->getFamilyIndex(),          // uint32_t             newQueueFamily
						VK_IMAGE_ASPECT_COLOR_BIT                 // VkImageAspectFlags   aspect
					};
					commandBuffer->setImageMemoryBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { imageTransitionBeforePresent } );
				}

				return commandBuffer->endRecording();
			};

			FrameResources& currentFrame = mFramesResources[mFrameIndex];

			// TODO : Fence warning might come from here : "vkWaitForFences called for fence 0x... which has not been submitted on a Queue or during acquire next image."
			if (!currentFrame.mDrawingFinishedFence->wait(2000000000))
			{
				return false;
			}

			if (!currentFrame.mDrawingFinishedFence->reset())
			{
				return false;
			}

			uint32_t imageIndex;
			if (!mSwapchain->acquireImageIndex(2000000000, currentFrame.mImageAcquiredSemaphore->getHandle(), VK_NULL_HANDLE, imageIndex))
			{
				return false;
			}

			std::vector<VkImageView> attachments = { mSceneImage->getImageView()->getHandle(), currentFrame.mDepthAttachment->getHandle(), mSwapchain->getImageViewHandle(imageIndex) };

			// TODO : Only create once
			currentFrame.mFramebuffer = mRenderPass->createFramebuffer(attachments, mSwapchain->getSize().width, mSwapchain->getSize().height, 1);
			if (currentFrame.mFramebuffer == nullptr || !currentFrame.mFramebuffer->isInitialized())
			{
				return false;
			}

			if (!prepareFrame(currentFrame.mCommandBuffer.get(), imageIndex, currentFrame.mFramebuffer.get()))
			{
				return false;
			}

			std::vector<WaitSemaphoreInfo> waitSemaphoreInfos = {};
			waitSemaphoreInfos.push_back({
				currentFrame.mImageAcquiredSemaphore->getHandle(),  // VkSemaphore            Semaphore
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT       // VkPipelineStageFlags   WaitingStage
			});
			if (!mGraphicsQueue->submitCommandBuffers({ currentFrame.mCommandBuffer.get() }, waitSemaphoreInfos, { currentFrame.mReadyToPresentSemaphore->getHandle() }, currentFrame.mDrawingFinishedFence.get()))
			{
				return false;
			}

			PresentInfo presentInfo = {
				mSwapchain->getHandle(), // VkSwapchainKHR         Swapchain
				imageIndex               // uint32_t               ImageIndex
			};
			if (!mPresentQueue->presentImage({ currentFrame.mReadyToPresentSemaphore->getHandle() }, { presentInfo }))
			{
				return false;
			}

			mFrameIndex = (mFrameIndex + 1) % mFramesResources.size();
			return true;
		}

		void onMouseEvent() 
		{
			updateStagingBuffer(false);
		}

		bool updateStagingBuffer(bool force)
		{
			static float horizontalAngle = 0.0f;
			static float verticalAngle = 0.0f;

			if (mMouseState.buttons[0].isPressed || force) 
			{
				horizontalAngle += 0.5f * mMouseState.position.delta.x;
				verticalAngle = nu::clamp(verticalAngle - 0.5f * mMouseState.position.delta.y, -90.0f, 90.0f);

				nu::Matrix4f mr1 = nu::Quaternionf(verticalAngle, { -1.0f, 0.0f, 0.0f }).toMatrix4();
				nu::Matrix4f mr2 = nu::Quaternionf(horizontalAngle, { 0.0f, 1.0f, 0.0f }).toMatrix4();
				nu::Matrix4f rotation = mr1 * mr2;
				nu::Matrix4f translation = nu::Matrix4f::translation({ 0.0f, 0.0f, -4.0f });
				nu::Matrix4f modelMatrix = translation * rotation;

				nu::Matrix4f viewMatrix = nu::Matrix4f::lookAt(nu::Vector3f::zero, { 0.0f, 0.0f, -4.0f }, nu::Vector3f::up);

				nu::Matrix4f modelViewMatrix = viewMatrix * modelMatrix;

				if (!mStagingBuffer->mapWriteUnmap(0, sizeof(float) * 16, &modelViewMatrix[0], true, nullptr))
				{
					return false;
				}

				nu::Matrix4f perspectiveMatrix = nu::Matrix4f::perspective(50.0f, static_cast<float>(mSwapchain->getSize().width) / static_cast<float>(mSwapchain->getSize().height), 0.5f, 10.0f);

				if (!mStagingBuffer->mapWriteUnmap(sizeof(float) * 16, sizeof(float) * 16, &perspectiveMatrix[0], true, nullptr))
				{
					return false;
				}
			}
			return true;
		}

		virtual bool resize() override
		{
			if (!mSwapchain->recreate(mFramesResources))
			{
				return false;
			}

			if (isReady()) 
			{
				// Scene image (color attachment in 1st subpass, input attachment in 2nd subpass
				mSceneImage = VulkanImageHelper::createInputAttachment(VulkanDevice::get(), VK_IMAGE_TYPE_2D, mSwapchain->getFormat(), { mSwapchain->getSize().width, mSwapchain->getSize().height, 1 }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
				if (mSceneImage == nullptr)
				{
					return false;
				}

				// Postprocess descriptor set - with input attachment

				VulkanImageDescriptorInfo sceneImageDescriptorUpdate = {
					mPostprocessDescriptorSets[0]->getHandle(), // VkDescriptorSet                      TargetDescriptorSet
					0,                                          // uint32_t                             TargetDescriptorBinding
					0,                                          // uint32_t                             TargetArrayElement
					VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,        // VkDescriptorType                     TargetDescriptorType
					{                                           // std::vector<VkDescriptorImageInfo>   ImageInfos
						{
							VK_NULL_HANDLE,                           // VkSampler                            sampler
							mSceneImage->getImageView()->getHandle(), // VkImageView                          imageView
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  // VkImageLayout                        imageLayout
						}
					}
				};

				VulkanDevice::get().updateDescriptorSets({ sceneImageDescriptorUpdate }, {}, {}, {});


				if (!updateStagingBuffer(true)) 
				{
					return false;
				}
			}
			return true;
		}
};
