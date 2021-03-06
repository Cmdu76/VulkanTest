#pragma once

#include "../../CookBook/SampleBase.hpp"

#include "../../Math/Matrix4.hpp"

#include "../../Mesh.hpp"
#include "../../Window.hpp"

#include "../../VertexBuffer.hpp"
#include "../../StagingBuffer.hpp"
#include "../../UniformBuffer.hpp"

/* 

TODO : FIND WHY THIS ISNT WORKING ....

*/

class AddingShadows : public SampleBase
{
	public:
		std::array<nu::Mesh, 2> mScene;
		nu::VertexBuffer::Ptr mVertexBuffer;

		VulkanImageHelperPtr mShadowMap;
		VulkanFramebufferPtr mShadowMapFramebuffer;

		VulkanDescriptorSetLayoutPtr mDescriptorSetLayout;
		VulkanDescriptorPoolPtr mDescriptorPool;
		std::vector<VulkanDescriptorSetPtr> mDescriptorSets;

		VulkanPipelineLayoutPtr mPipelineLayout;

		VulkanRenderPassPtr mShadowRenderPass;
		VulkanRenderPassPtr mSceneRenderPass;

		std::vector<VulkanGraphicsPipelinePtr> mPipelines;
		enum PipelineNames
		{
			ShadowPipeline = 0,
			ScenePipeline = 1,
			Count
		};

		nu::UniformBuffer::Ptr mUniformBuffer;
		nu::StagingBuffer::Ptr mStagingBuffer;

		uint32_t mFrameIndex = 0;

		virtual bool initialize(VulkanWindowParameters windowParameters) override
		{
			if (!initializeVulkan(windowParameters, nullptr))
			{
				return false;
			}

			// Vertex data
			if (!mScene[0].loadFromFile("../../Data/Models/knot.obj", true, false, false, true))
			{
				return false;
			}
			if (!mScene[1].loadFromFile("../../Data/Models/plane.obj", true, false, false, false))
			{
				return false;
			}
			std::vector<float> vertexData(mScene[0].data);
			vertexData.insert(vertexData.end(), mScene[1].data.begin(), mScene[1].data.end());
			mVertexBuffer = nu::VertexBuffer::createVertexBuffer(VulkanDevice::get(), (uint32_t)sizeof(vertexData[0]) * (uint32_t)vertexData.size());
			if (!mVertexBuffer || !mVertexBuffer->updateAndWait((uint32_t)sizeof(vertexData[0]) * (uint32_t)vertexData.size(), &vertexData[0], 0, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, mFramesResources.front().mCommandBuffer.get(), mGraphicsQueue, {}, 50000000))
			{
				return false;
			}

			// Staging buffer & Uniform buffer
			mUniformBuffer = nu::UniformBuffer::createUniformBuffer(VulkanDevice::get(), 3 * 16 * sizeof(float));
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

			// Image in which shadow map will be stored
			mShadowMap = VulkanImageHelper::createCombinedImageSampler(VulkanDevice::get(), VK_IMAGE_TYPE_2D, mSwapchain->getDepthFormat(), { 512, 512, 1 }, 1, 1,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, VK_FILTER_LINEAR,
				VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, 0.0f, 1.0f, false, 1.0f, false, VK_COMPARE_OP_ALWAYS, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
				false);
			if (mShadowMap == nullptr || !mShadowMap->hasSampler() || mShadowMap->getImage() == nullptr || mShadowMap->getMemoryBlock() == nullptr || mShadowMap->getImageView() == nullptr)
			{
				return false;
			}

			// TODO : Is the shadow map sent to queue ? (Should it be ?)

			// Descriptor set with uniform buffer
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

			// TODO : Update more than one at once
			mUniformBuffer->updateDescriptor(mDescriptorSets[0].get(), 0, 0);

			VulkanImageDescriptorInfo imageDescriptorUpdate = {
				mDescriptorSets[0]->getHandle(),                  // VkDescriptorSet                      TargetDescriptorSet
				1,                                                // uint32_t                             TargetDescriptorBinding
				0,                                                // uint32_t                             TargetArrayElement
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,        // VkDescriptorType                     TargetDescriptorType
				{                                                 // std::vector<VkDescriptorImageInfo>   ImageInfos
					{
						mShadowMap->getSampler()->getHandle(),          // VkSampler                            sampler
						mShadowMap->getImageView()->getHandle(),        // VkImageView                          imageView
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL // VkImageLayout                        imageLayout
					}
				}
			};

			VulkanDevice::get().updateDescriptorSets({ imageDescriptorUpdate }, {  }, {}, {});

			// Shadow map render pass - for rendering into depth attachment

			mShadowRenderPass = VulkanDevice::get().initRenderPass();

			mShadowRenderPass->addAttachment({
				0,                                                // VkAttachmentDescriptionFlags     flags
				mSwapchain->getDepthFormat(),                     // VkFormat                         format
				VK_SAMPLE_COUNT_1_BIT,                            // VkSampleCountFlagBits            samples
				VK_ATTACHMENT_LOAD_OP_CLEAR,                      // VkAttachmentLoadOp               loadOp
				VK_ATTACHMENT_STORE_OP_STORE,                     // VkAttachmentStoreOp              storeOp
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,                  // VkAttachmentLoadOp               stencilLoadOp
				VK_ATTACHMENT_STORE_OP_DONT_CARE,                 // VkAttachmentStoreOp              stencilStoreOp
				VK_IMAGE_LAYOUT_UNDEFINED,                        // VkImageLayout                    initialLayout
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL   // VkImageLayout                    finalLayout
			});

			mShadowRenderPass->addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
			mShadowRenderPass->addDepthStencilAttachmentToSubpass(0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			mShadowRenderPass->addDependency({
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   srcSubpass
				0,                                              // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,          // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,     // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_SHADER_READ_BIT,                      // VkAccessFlags              srcAccessMask
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,   // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			});
			mShadowRenderPass->addDependency({
				0,                                              // uint32_t                   srcSubpass
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,      // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,          // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,   // VkAccessFlags              srcAccessMask
				VK_ACCESS_SHADER_READ_BIT,                      // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
				});

			if (!mShadowRenderPass->create())
			{
				return false;
			}

			mShadowMapFramebuffer = mShadowRenderPass->createFramebuffer({ mShadowMap->getImageView()->getHandle() }, 512, 512, 1);
			if (mShadowMapFramebuffer == nullptr || !mShadowMapFramebuffer->isInitialized())
			{
				return false;
			}

			// Render pass
			mSceneRenderPass = VulkanDevice::get().initRenderPass();
			mSceneRenderPass->addAttachment({
				0,                                                // VkAttachmentDescriptionFlags     flags
				mSwapchain->getFormat(),                          // VkFormat                         format
				VK_SAMPLE_COUNT_1_BIT,                            // VkSampleCountFlagBits            samples
				VK_ATTACHMENT_LOAD_OP_CLEAR,                      // VkAttachmentLoadOp               loadOp
				VK_ATTACHMENT_STORE_OP_STORE,                     // VkAttachmentStoreOp              storeOp
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,                  // VkAttachmentLoadOp               stencilLoadOp
				VK_ATTACHMENT_STORE_OP_DONT_CARE,                 // VkAttachmentStoreOp              stencilStoreOp
				VK_IMAGE_LAYOUT_UNDEFINED,                        // VkImageLayout                    initialLayout
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR                   // VkImageLayout                    finalLayout
				});
			mSceneRenderPass->addAttachment({
				0,                                                // VkAttachmentDescriptionFlags     flags
				mSwapchain->getDepthFormat(),                     // VkFormat                         format
				VK_SAMPLE_COUNT_1_BIT,                            // VkSampleCountFlagBits            samples
				VK_ATTACHMENT_LOAD_OP_CLEAR,                      // VkAttachmentLoadOp               loadOp
				VK_ATTACHMENT_STORE_OP_DONT_CARE,                 // VkAttachmentStoreOp              storeOp
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,                  // VkAttachmentLoadOp               stencilLoadOp
				VK_ATTACHMENT_STORE_OP_DONT_CARE,                 // VkAttachmentStoreOp              stencilStoreOp
				VK_IMAGE_LAYOUT_UNDEFINED,                        // VkImageLayout                    initialLayout
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL  // VkImageLayout                    finalLayout
				});
			mSceneRenderPass->addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
			mSceneRenderPass->addColorAttachmentToSubpass(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			mSceneRenderPass->addDepthStencilAttachmentToSubpass(1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			mSceneRenderPass->addDependency({
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   srcSubpass
				0,                                              // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,      // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,          // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,   // VkAccessFlags              srcAccessMask
				VK_ACCESS_SHADER_READ_BIT,                      // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			});
			mSceneRenderPass->addDependency({
				0,                                              // uint32_t                   srcSubpass
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,              // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags              srcAccessMask
				VK_ACCESS_MEMORY_READ_BIT,                      // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			});
			if (!mSceneRenderPass->create())
			{
				return false;
			}

			// Graphics pipeline
			mPipelineLayout = VulkanDevice::get().createPipelineLayout({ mDescriptorSetLayout->getHandle() }, { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4 } });
			if (mPipelineLayout == nullptr || !mPipelineLayout->isInitialized())
			{
				return false;
			}

			mPipelines.resize(PipelineNames::Count);
			mPipelines[PipelineNames::ScenePipeline] = VulkanDevice::get().initGraphicsPipeline(*mPipelineLayout, *mSceneRenderPass, nullptr);
			mPipelines[PipelineNames::ShadowPipeline] = VulkanDevice::get().initGraphicsPipeline(*mPipelineLayout, *mShadowRenderPass, nullptr);

			VulkanGraphicsPipeline* scenePipeline = mPipelines[PipelineNames::ScenePipeline].get();

			scenePipeline->setSubpass(0);

			VulkanShaderModulePtr vertexShaderModule = VulkanDevice::get().initShaderModule();
			if (vertexShaderModule == nullptr || !vertexShaderModule->loadFromFile("../../Examples/5 - Adding Shadows/scene.vert.spv"))
			{
				return false;
			}
			vertexShaderModule->setVertexEntrypointName("main");

			VulkanShaderModulePtr fragmentShaderModule = VulkanDevice::get().initShaderModule();
			if (fragmentShaderModule == nullptr || !fragmentShaderModule->loadFromFile("../../Examples/5 - Adding Shadows/scene.frag.spv"))
			{
				return false;
			}
			fragmentShaderModule->setFragmentEntrypointName("main");

			if (!vertexShaderModule->create() || !fragmentShaderModule->create())
			{
				return false;
			}

			scenePipeline->addShaderModule(vertexShaderModule.get());
			scenePipeline->addShaderModule(fragmentShaderModule.get());

			scenePipeline->addVertexBinding(0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX);
			scenePipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			scenePipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float));

			//scenePipeline->setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false);

			scenePipeline->setViewport(0.0f, 0.0f, 512.0f, 512.0f, 0.0f, 1.0f);
			scenePipeline->setScissor(0, 0, 512, 512);

			//scenePipeline->setRasterizationState(false, false, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, false, 0.0f, 0.0f, 0.0f, 1.0f);

			//scenePipeline->setMultisampleState(VK_SAMPLE_COUNT_1_BIT, false, 0.0f, nullptr, false, false);

			//scenePipeline->setDepthStencilState(true, true, VK_COMPARE_OP_LESS_OR_EQUAL, false, false, {}, {}, 0.0f, 1.0f);

			//scenePipeline->addBlend(false);
			//scenePipeline->setBlendState(false, VK_LOGIC_OP_COPY, 1.0f, 1.0f, 1.0f, 1.0f);

			scenePipeline->addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			scenePipeline->addDynamicState(VK_DYNAMIC_STATE_SCISSOR);

			if (!scenePipeline->create())
			{
				return false;
			}



			VulkanGraphicsPipeline* shadowPipeline = mPipelines[PipelineNames::ShadowPipeline].get();

			shadowPipeline->setSubpass(0);

			VulkanShaderModulePtr shadowVertexShaderModule = VulkanDevice::get().initShaderModule();
			if (vertexShaderModule == nullptr || !shadowVertexShaderModule->loadFromFile("../../Examples/5 - Adding Shadows/shadow.vert.spv"))
			{
				return false;
			}
			shadowVertexShaderModule->setVertexEntrypointName("main");
			if (!shadowVertexShaderModule->create())
			{
				return false;
			}

			shadowPipeline->addShaderModule(shadowVertexShaderModule.get());

			shadowPipeline->addVertexBinding(0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX);
			shadowPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

			//shadowPipeline->setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false);

			shadowPipeline->setViewport(0.0f, 0.0f, 512.0f, 512.0f, 0.0f, 1.0f);
			shadowPipeline->setScissor(0, 0, 512, 512);

			//shadowPipeline->setRasterizationState(false, false, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, false, 0.0f, 0.0f, 0.0f, 1.0f);

			//shadowPipeline->setMultisampleState(VK_SAMPLE_COUNT_1_BIT, false, 0.0f, nullptr, false, false);

			//shadowePipeline->setDepthStencilState(true, true, VK_COMPARE_OP_LESS_OR_EQUAL, false, false, {}, {}, 0.0f, 1.0f);

			//shadowPipeline->addBlend(false);
			//shadowPipeline->setBlendState(false, VK_LOGIC_OP_COPY, 1.0f, 1.0f, 1.0f, 1.0f);

			if (!shadowPipeline->create())
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

				// Shadow map generation
				commandBuffer->beginRenderPass(mShadowRenderPass->getHandle(), mShadowMapFramebuffer->getHandle(), { { 0, 0, },{ 512, 512 } }, { { 1.0f, 0 } }, VK_SUBPASS_CONTENTS_INLINE);
				mVertexBuffer->bindTo(commandBuffer, 0, 0);
				commandBuffer->bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout->getHandle(), 0, { mDescriptorSets[0].get() }, {});
				commandBuffer->bindPipeline(mPipelines[PipelineNames::ShadowPipeline].get());
				commandBuffer->drawGeometry(mScene[0].parts[0].vertexCount + mScene[1].parts[0].vertexCount, 1, 0, 0);
				commandBuffer->endRenderPass();

				// Image transition before drawing
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
					commandBuffer->setImageMemoryBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, { imageTransitionBeforeDrawing });
				}

				// Drawing
				commandBuffer->beginRenderPass(mSceneRenderPass->getHandle(), framebuffer->getHandle(), { { 0, 0 }, mSwapchain->getSize() }, { { 0.1f, 0.2f, 0.3f, 1.0f },{ 1.0f, 0 } }, VK_SUBPASS_CONTENTS_INLINE);
				commandBuffer->bindPipeline(mPipelines[PipelineNames::ScenePipeline].get());

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

				static float lightPosition[4] = { 3.0f, 3.0f, 3.0f, 1.0f };

				commandBuffer->provideDataToShadersThroughPushConstants(mPipelineLayout->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4, &lightPosition[0]);

				commandBuffer->drawGeometry(mScene[0].parts[0].vertexCount + mScene[1].parts[0].vertexCount, 1, 0, 0);

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
					commandBuffer->setImageMemoryBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { imageTransitionBeforePresent });
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

			std::vector<VkImageView> attachments = { mSwapchain->getImageViewHandle(imageIndex) };
			if (currentFrame.mDepthAttachment != nullptr)
			{
				attachments.push_back(currentFrame.mDepthAttachment->getHandle());
			}

			// TODO : Only create once
			currentFrame.mFramebuffer = mSceneRenderPass->createFramebuffer(attachments, mSwapchain->getSize().width, mSwapchain->getSize().height, 1);
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

				if (!mStagingBuffer->mapWriteUnmap(0, sizeof(float) * 16, &modelViewMatrix[0]))
				{
					return false;
				}

				nu::Matrix4f perspectiveMatrix = nu::Matrix4f::perspective(50.0f, static_cast<float>(mSwapchain->getSize().width) / static_cast<float>(mSwapchain->getSize().height), 0.5f, 10.0f);

				if (!mStagingBuffer->mapWriteUnmap(sizeof(float) * 16, sizeof(float) * 16, &perspectiveMatrix[0]))
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
				if (!updateStagingBuffer(true))
				{
					return false;
				}
			}
			return true;
		}
};