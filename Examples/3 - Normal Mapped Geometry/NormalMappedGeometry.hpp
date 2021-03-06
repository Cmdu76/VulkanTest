#pragma once

#include "../../CookBook/SampleBase.hpp"

#include "../../Math/Matrix4.hpp"

#include "../../Mesh.hpp"
#include "../../Window.hpp"

#include "../../VertexBuffer.hpp"
#include "../../StagingBuffer.hpp"
#include "../../UniformBuffer.hpp"

class NormalMappedGeometry : public SampleBase
{
	public:
		nu::Mesh mMesh;
		nu::VertexBuffer::Ptr mVertexBuffer;

		VulkanImageHelperPtr mTexture;

		VulkanDescriptorSetLayoutPtr mDescriptorSetLayout;
		VulkanDescriptorPoolPtr mDescriptorPool;
		std::vector<VulkanDescriptorSetPtr> mDescriptorSets;

		VulkanRenderPassPtr mRenderPass;
		VulkanPipelineLayoutPtr mPipelineLayout;
		std::vector<VulkanGraphicsPipelinePtr> mPipelines;
		enum PipelineNames
		{
			MeshPipeline = 0,
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

			// Texture
			int width = 1;
			int height = 1;
			std::vector<unsigned char> imageData;
			if (!loadTextureDataFromFile("../../Data/Textures/normal_map.png", 4, imageData, &width, &height))
			{
				return false;
			}
			mTexture = VulkanImageHelper::createCombinedImageSampler(VulkanDevice::get(), VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { (uint32_t)width, (uint32_t)height, 1 },
				1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, VK_FILTER_LINEAR,
				VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
				VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.0f, 0.0f, 1.0f, false, 1.0f, false, VK_COMPARE_OP_ALWAYS, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, false);
			if (mTexture == nullptr || mTexture->getImage() == nullptr || mTexture->getMemoryBlock() == nullptr || mTexture->getImageView() == nullptr || !mTexture->hasSampler())
			{
				return false;
			}
			VkImageSubresourceLayers imageSubresourceLayer = {
				VK_IMAGE_ASPECT_COLOR_BIT,    // VkImageAspectFlags     aspectMask
				0,                            // uint32_t               mipLevel
				0,                            // uint32_t               baseArrayLayer
				1                             // uint32_t               layerCount
			};
			if (!mGraphicsQueue->useStagingBufferToUpdateImageAndWait(static_cast<VkDeviceSize>(imageData.size()), &imageData[0], mTexture->getImage(), 
				imageSubresourceLayer, { 0, 0, 0 }, { (uint32_t)width, (uint32_t)height, 1 }, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, mFramesResources.front().mCommandBuffer.get(), {}, 50000000))
			{
				return false;
			}

			// Vertex data
			uint32_t vertexStride = 0;
			if (!mMesh.loadFromFile("../../Data/Models/ice.obj", true, true, true, true, &vertexStride))
			{
				return false;
			}
			mVertexBuffer = nu::VertexBuffer::createVertexBuffer(VulkanDevice::get(), mMesh.size());
			if (!mVertexBuffer || !mVertexBuffer->updateAndWait(mMesh.size(), &mMesh.data[0], 0, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, mFramesResources.front().mCommandBuffer.get(), mGraphicsQueue, {}, 50000000))
			{
				return false;
			}

			// Uniform buffer
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

			// TODO : Update more than one at once
			mUniformBuffer->updateDescriptor(mDescriptorSets[0].get(), 0, 0);

			VulkanImageDescriptorInfo imageDescriptorUpdate = {
				mDescriptorSets[0]->getHandle(),            // VkDescriptorSet                      TargetDescriptorSet
				1,                                          // uint32_t                             TargetDescriptorBinding
				0,                                          // uint32_t                             TargetArrayElement
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  // VkDescriptorType                     TargetDescriptorType
				{                                           // std::vector<VkDescriptorImageInfo>   ImageInfos
					{
						mTexture->getSampler()->getHandle(),      // VkSampler                            sampler
						mTexture->getImageView()->getHandle(),    // VkImageView                          imageView
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  // VkImageLayout                        imageLayout
					}
				}
			};

			VulkanDevice::get().updateDescriptorSets({ imageDescriptorUpdate }, {}, {}, {});

			// Render pass
			mRenderPass = VulkanDevice::get().initRenderPass();
			mRenderPass->addAttachment(mSwapchain->getFormat());
			mRenderPass->setAttachmentLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
			mRenderPass->setAttachmentStoreOp(VK_ATTACHMENT_STORE_OP_STORE);
			mRenderPass->setAttachmentFinalLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			mRenderPass->addAttachment(mSwapchain->getDepthFormat());
			mRenderPass->setAttachmentLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
			mRenderPass->setAttachmentFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			mRenderPass->addSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
			mRenderPass->addColorAttachmentToSubpass(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			mRenderPass->addDepthStencilAttachmentToSubpass(1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			mRenderPass->addDependency(
				VK_SUBPASS_EXTERNAL,                            // uint32_t                   srcSubpass
				0,                                              // uint32_t                   dstSubpass
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,              // VkPipelineStageFlags       srcStageMask
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags       dstStageMask
				VK_ACCESS_MEMORY_READ_BIT,                      // VkAccessFlags              srcAccessMask
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags              dstAccessMask
				VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags          dependencyFlags
			);
			mRenderPass->addDependency(
				0,                                              // uint32_t                   srcSubpass
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
					VK_SHADER_STAGE_FRAGMENT_BIT,     // VkShaderStageFlags   stageFlags
					0,								  // uint32_t			  offset
					sizeof(float) * 4				  // uint32_t             size
				}
			};

			mPipelineLayout = VulkanDevice::get().createPipelineLayout({ mDescriptorSetLayout->getHandle() }, pushConstantRanges);
			if (mPipelineLayout == nullptr || !mPipelineLayout->isInitialized())
			{
				return false;
			}


			VulkanShaderModulePtr vertexShaderModule = VulkanDevice::get().initShaderModule();
			if (vertexShaderModule == nullptr || !vertexShaderModule->loadFromFile("../../Examples/3 - Normal Mapped Geometry/shader.vert.spv", VulkanShaderModule::Vertex))
			{
				return false;
			}

			VulkanShaderModulePtr fragmentShaderModule = VulkanDevice::get().initShaderModule();
			if (fragmentShaderModule == nullptr || !fragmentShaderModule->loadFromFile("../../Examples/3 - Normal Mapped Geometry/shader.frag.spv", VulkanShaderModule::Fragment))
			{
				return false;
			}

			mPipelines.resize(PipelineNames::Count);
			mPipelines[PipelineNames::MeshPipeline] = VulkanDevice::get().initGraphicsPipeline(*mPipelineLayout, *mRenderPass, nullptr);

			VulkanGraphicsPipeline* modelPipeline = mPipelines[PipelineNames::MeshPipeline].get();

			modelPipeline->setSubpass(0);

			modelPipeline->addShaderModule(vertexShaderModule.get());
			modelPipeline->addShaderModule(fragmentShaderModule.get());

			modelPipeline->addVertexBinding(0, vertexStride, VK_VERTEX_INPUT_RATE_VERTEX);
			modelPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
			modelPipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float));
			modelPipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, 6 * sizeof(float));
			modelPipeline->addVertexAttribute(3, 0, VK_FORMAT_R32G32B32_SFLOAT, 8 * sizeof(float));
			modelPipeline->addVertexAttribute(4, 0, VK_FORMAT_R32G32B32_SFLOAT, 11 * sizeof(float));

			//modelPipeline->setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false);

			modelPipeline->setViewport(0.0f, 0.0f, 500.0f, 500.0f, 0.0f, 1.0f);
			modelPipeline->setScissor(0, 0, 500, 500);

			//modelPipeline->setRasterizationState(false, false, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, false, 0.0f, 0.0f, 0.0f, 1.0f);

			//modelPipeline->setMultisampleState(VK_SAMPLE_COUNT_1_BIT, false, 0.0f, nullptr, false, false);

			//modelPipeline->setDepthStencilState(true, true, VK_COMPARE_OP_LESS_OR_EQUAL, false, false, {}, {}, 0.0f, 1.0f);

			//modelPipeline->addBlend(false);
			//modelPipeline->setBlendState(false, VK_LOGIC_OP_COPY, 1.0f, 1.0f, 1.0f, 1.0f);

			modelPipeline->addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
			modelPipeline->addDynamicState(VK_DYNAMIC_STATE_SCISSOR);

			if (!modelPipeline->create())
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

				mVertexBuffer->bindTo(commandBuffer, 0, 0);

				commandBuffer->bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout->getHandle(), 0, { mDescriptorSets[0].get() }, {});

				commandBuffer->bindPipeline(mPipelines[PipelineNames::MeshPipeline].get());

				std::array<float, 4> lightPosition = { 5.0f, 5.0f, 0.0f, 0.0f };
				commandBuffer->provideDataToShadersThroughPushConstants(mPipelineLayout->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, &lightPosition[0]);

				for (size_t i = 0; i < mMesh.parts.size(); i++) 
				{
					commandBuffer->drawGeometry(mMesh.parts[i].vertexCount, 1, mMesh.parts[i].vertexOffset, 0);
				}

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

			std::vector<VkImageView> attachments = { mSwapchain->getImageViewHandle(imageIndex) };
			if (currentFrame.mDepthAttachment != nullptr)
			{
				attachments.push_back(currentFrame.mDepthAttachment->getHandle());
			}

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
