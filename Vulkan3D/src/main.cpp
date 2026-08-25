#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <volk/volk.h>

#define VKB_VULKAN_H_PATH <volk.h>
#include <vkboostrap/VkBootstrap.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <fstream>
#include <stdexcept>

std::vector<char> readFile(const std::string& filename) {

	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open shader file: " + filename);
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();
	return buffer;
}

struct Vertex {
	float position[3];
	float uv[2];
	float normal[3];
	bool operator==(const Vertex& other) const {
		return position[0] == other.position[0] && position[1] == other.position[1] && position[2] == other.position[2] &&
			uv[0] == other.uv[0] && uv[1] == other.uv[1] &&
			normal[0] == other.normal[0] && normal[1] == other.normal[1] && normal[2] == other.normal[2];
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			size_t result = 0;
			hash<float> float_hash;

			// Hash positions
			for (int i = 0; i < 3; ++i) {
				result ^= float_hash(vertex.position[i]) + 0x9e3779b9 + (result << 6) + (result >> 2);
			}
			// Hash UVs
			for (int i = 0; i < 2; ++i) {
				result ^= float_hash(vertex.uv[i]) + 0x9e3779b9 + (result << 6) + (result >> 2);
			}
			// Hash normals
			for (int i = 0; i < 3; ++i) {
				result ^= float_hash(vertex.normal[i]) + 0x9e3779b9 + (result << 6) + (result >> 2);
			}

			return result;
		}
	};
};

struct VulkanContext {

	static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
	int current_frame = 0;

	// Vkboostrap wrapper objects
	vkb::Instance vkb_instance;
	vkb::PhysicalDevice vkb_physicalDevice;
	vkb::Device vkb_device;
	vkb::Swapchain vkb_swapchain;

	// Vulkan objects
	VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
	VkQueue vk_graphics_queue = VK_NULL_HANDLE;
	VkQueue vk_present_queue = VK_NULL_HANDLE;

	std::vector<VkImage> vk_swapchain_images;
	std::vector<VkImageView> vk_swapchain_image_views;

	VkImage vk_depth_buffer_image = VK_NULL_HANDLE;
	VkImageView vk_depth_buffer_image_view = VK_NULL_HANDLE;
	VmaAllocation vma_depth_buffer_allocation = nullptr;
	VkFormat vk_depth_buffer_image_format = VK_FORMAT_D32_SFLOAT;

	VkImage vk_msaa_image = VK_NULL_HANDLE;
	VkImageView vk_msaa_image_view = VK_NULL_HANDLE;
	VmaAllocation vma_msaa_allocation = nullptr;

	VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_4_BIT;
	
	VkExtent2D vk_swapchain_extent = VkExtent2D(1, 1);
	VkFormat vk_swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;
	VkColorSpaceKHR vk_swapchain_colorspace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout global_vertex_descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout global_fragment_descriptor_set_layout = VK_NULL_HANDLE;

	VkPipelineLayout vk_graphics_pipeline_layout = VK_NULL_HANDLE;
	VkPipeline vk_graphics_pipeline = VK_NULL_HANDLE;

	std::vector<VkCommandPool> vk_cmd_pools;
	std::vector<VkCommandBuffer> vk_cmd_buffers;

	std::vector<VkSemaphore> vk_image_avaliable_semaphores;
	std::vector<VkSemaphore> vk_render_finished_semaphores;

	VkSemaphore vk_timeline_semaphore = VK_NULL_HANDLE;
	uint64_t timeline_values[2] = { 0 , 0 };
};

struct GLFWContext {
	GLFWwindow* window = nullptr;
	int Window_Creation_Width = 800;
	int Window_Creation_Height = 640;
	const char* Window_Creation_Title = "Window";
};

GLFWContext glfw_context;
VulkanContext vk_context;

VmaAllocator vma_allocator;

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation alloc = nullptr;
	VkDeviceSize size = 0;

	void* mappedData = nullptr;

	bool is_empty() { return (buffer == VK_NULL_HANDLE); }

	void free() {
		if (!is_empty()) {

			vmaDestroyBuffer(vma_allocator, buffer, alloc);

			buffer = VK_NULL_HANDLE;
			alloc = nullptr;
			mappedData = nullptr;
			size = 0;
		}
	}
};

void CreateBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags bufferUsageFlags, VkSharingMode sharingMode, VmaMemoryUsage memUsageFlags, VmaAllocationCreateFlags allocFlags) {

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = size;
	bufferInfo.usage = bufferUsageFlags;
	bufferInfo.sharingMode = sharingMode;

	VmaAllocationCreateInfo bufferAllocInfo{};
	bufferAllocInfo.usage = memUsageFlags;
	bufferAllocInfo.flags = allocFlags;

	VmaAllocationInfo resultInfo;
	if (vmaCreateBuffer(vma_allocator, &bufferInfo, &bufferAllocInfo, &buffer.buffer, &buffer.alloc, &resultInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Buffer.");
	}

	buffer.mappedData = resultInfo.pMappedData;
	buffer.size = size;
}

void VulkanCreateDescriptorSet(VkDescriptorSet &set,VkDescriptorSetLayout layout) {
	VkDescriptorSetAllocateInfo descSetAllocInfo{};
	descSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descSetAllocInfo.pNext = nullptr;

	descSetAllocInfo.descriptorPool = vk_context.descriptor_pool;
	descSetAllocInfo.descriptorSetCount = 1;
	descSetAllocInfo.pSetLayouts = &layout;
	VkResult result = vkAllocateDescriptorSets(
		vk_context.vkb_device.device,
		&descSetAllocInfo,
		&set
	);

}

void CreateUniformBuffer(Buffer &buffer, VkDeviceSize size, VkDescriptorSet &set,uint32_t binding) {

	CreateBuffer(
		buffer,
		size,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		VMA_MEMORY_USAGE_AUTO,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
	);

	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = buffer.buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = size;

	VkWriteDescriptorSet writeSet{};
	writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeSet.pNext = nullptr;

	writeSet.dstSet = set;
	writeSet.dstBinding = binding;
	writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writeSet.descriptorCount = 1;
	writeSet.pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(vk_context.vkb_device.device, 1, &writeSet, 0, nullptr);

}

struct Texture{
	VkImage image = VK_NULL_HANDLE;
	VkImageView image_view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VmaAllocation alloc = nullptr;

	void free() {
		vmaDestroyImage(vma_allocator, image, alloc);
		vkDestroyImageView(vk_context.vkb_device.device, image_view, nullptr);
		vkDestroySampler(vk_context.vkb_device.device, sampler, nullptr);
		
		image = VK_NULL_HANDLE;
		image_view = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
		alloc = nullptr;
	}
};

struct Material {
	Texture* albedo = nullptr;
	VkDescriptorSet descriptor = VK_NULL_HANDLE;

	Buffer texture_uv_scale_uniform_buffer;

	void create(Texture albedoTex) {

		VulkanCreateDescriptorSet(descriptor, vk_context.global_fragment_descriptor_set_layout);
		CreateUniformBuffer(texture_uv_scale_uniform_buffer, sizeof(glm::vec2), descriptor, 1);

		VkDescriptorImageInfo imageDescriptorInfo{};
		imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageDescriptorInfo.imageView = albedoTex.image_view;
		imageDescriptorInfo.sampler = albedoTex.sampler;

		VkWriteDescriptorSet writeDescriptor{};
		writeDescriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptor.pNext = nullptr;

		writeDescriptor.dstBinding = 0;
		writeDescriptor.dstSet = descriptor;

		writeDescriptor.descriptorCount = 1;
		writeDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		writeDescriptor.pImageInfo = &imageDescriptorInfo;

		vkUpdateDescriptorSets(vk_context.vkb_device.device, 1, &writeDescriptor, 0, nullptr);

		albedo = &albedoTex;
	}

	void bind(VkCommandBuffer cmdBuffer, glm::vec2 uvScale) {
		memcpy(texture_uv_scale_uniform_buffer.mappedData, &uvScale, sizeof(glm::vec2));
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_context.vk_graphics_pipeline_layout, 1, 1, &descriptor, 0, 0);
	}

	void free() {
		vkFreeDescriptorSets(vk_context.vkb_device.device, vk_context.descriptor_pool, 1, &descriptor);
		texture_uv_scale_uniform_buffer.free();
	}
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	Buffer vertex_buffer;
	Buffer index_buffer;

	void update() {
		VkDeviceSize vertices_size = sizeof(Vertex) * vertices.size();
		
		if (vertex_buffer.is_empty()) {
			CreateBuffer(vertex_buffer,
				vertices_size,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_SHARING_MODE_EXCLUSIVE,
				VMA_MEMORY_USAGE_AUTO,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
			);
		}

		memcpy(vertex_buffer.mappedData, vertices.data(), vertices_size);

		VkDeviceSize indices_size = sizeof(uint32_t) * indices.size();

		if (index_buffer.is_empty()) {
			CreateBuffer(index_buffer,
				indices_size,
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_SHARING_MODE_EXCLUSIVE,
				VMA_MEMORY_USAGE_AUTO,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
			);
		}

		memcpy(index_buffer.mappedData, indices.data(), indices_size);
	}

	void draw(VkCommandBuffer& cmd_buffer) {

		VkDeviceSize offsets[] = { 0 };

		vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vertex_buffer.buffer, offsets);

		vkCmdBindIndexBuffer(cmd_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		uint32_t indexCount = static_cast<uint32_t>(indices.size());
		vkCmdDrawIndexed(cmd_buffer, indexCount, 1, 0, 0, 0);
	}

	void free() {
		vertex_buffer.free();
		index_buffer.free();
		std::vector<Vertex>().swap(vertices);
		std::vector<uint32_t>().swap(indices);
	}
};

void LoadOBJMesh(Mesh& mesh, const std::string& filename) {

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename.c_str());

	if (!err.empty()) std::cerr << "ERR: " << err << std::endl;
	if (!ret) return;

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};

	for (const auto& shape : shapes) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
			size_t fv = size_t(shape.mesh.num_face_vertices[f]);

			for (size_t v = 0; v < fv; v++) {
				tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
				Vertex vertex{};

				// 1. Populate Position
				vertex.position[0] = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
				vertex.position[1] = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
				vertex.position[2] = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

				// 2. Populate UVs (Default to 0 if not present)
				if (idx.texcoord_index >= 0) {
					vertex.uv[0] = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
					vertex.uv[1] = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
				}
				else {
					vertex.uv[0] = 0.0f;
					vertex.uv[1] = 0.0f;
				}

				// 3. Populate Normals (Default to 0 if not present)
				if (idx.normal_index >= 0) {
					vertex.normal[0] = attrib.normals[3 * size_t(idx.normal_index) + 0];
					vertex.normal[1] = attrib.normals[3 * size_t(idx.normal_index) + 1];
					vertex.normal[2] = attrib.normals[3 * size_t(idx.normal_index) + 2];
				}
				else {
					vertex.normal[0] = 0.0f;
					vertex.normal[1] = 0.0f;
					vertex.normal[2] = 0.0f;
				}

				// Deduplicate and populate index buffer
				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = static_cast<uint32_t>(mesh.vertices.size());
					mesh.vertices.push_back(vertex);
				}

				mesh.indices.push_back(uniqueVertices[vertex]);
			}
			index_offset += fv;
		}
	}

	mesh.update();
};

VkDescriptorSet globalVertexSet;

Buffer camera_uniform_buffer;

const std::vector<Vertex> cube_vertices = {
	// Front face (Normal: Z+)
	{{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f,  1.0f}},
	{{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f,  1.0f}},
	{{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f, 0.0f,  1.0f}},
	{{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}, {0.0f, 0.0f,  1.0f}},

	// Back face (Normal: Z-)
	{{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	{{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	{{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
	{{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},

	// Left face (Normal: X-)
	{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
	{{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
	{{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
	{{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},

	// Right face (Normal: X+)
	{{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	{{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	{{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
	{{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},

	// Top face (Normal: Y-) -> Warning: Coordinates imply Y = -0.5f
	{{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
	{{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
	{{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
	{{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},

	// Bottom face (Normal: Y+) -> Warning: Coordinates imply Y = +0.5f
	{{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  1.0f, 0.0f}},
	{{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f, 0.0f}},
	{{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  1.0f, 0.0f}},
	{{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f, 0.0f}}
};


const std::vector<uint32_t> cube_indices = {
	0,  1,  2,      0,  2,  3,

	4,  7,  6,      4,  6,  5,

	8,  9,  10,     8,  10, 11,

	12, 15, 14,     12, 14, 13,

	16, 17, 18,     16, 18, 19,

	20, 23, 22,     20, 22, 21
};

const std::vector<Vertex> plane_vertices = {
	// Top-left
	{{-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},

	// Bottom-left
	{{-0.5f, 0.0f,  0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},

	// Bottom-right
	{{ 0.5f, 0.0f,  0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},

	// Top-right
	{{ 0.5f, 0.0f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
};

const std::vector<uint32_t> plane_indices = {
	0, 1, 2,
	2, 3, 0
};

struct MeshPushConstants {
	glm::mat4 model_matrix;
};

struct Camera {
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation = glm::vec3(0.0f);

	float FOV = 70.0f;
	float zFar = 100.0f;
	float zNear = 0.2f;

	glm::mat4 getVPMatrix(const float& aspectRatio) {

		float pitch = glm::radians(-rotation.x);
		float yaw = glm::radians(-rotation.y);

		float cPitch = cos(pitch);
		float sPitch = sin(pitch);
		
		float cYaw = cos(yaw);
		float sYaw = sin(yaw);

		glm::vec3 forward(sYaw * cPitch,sPitch,cYaw * cPitch);
		forward = glm::normalize(forward);

		glm::mat4 view = glm::lookAt(position, position - forward, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(FOV), aspectRatio, zNear, zFar);

		proj[1][1] *= -1.0f;

		return proj * view;
	}
};

Camera camera;
float camera_speed = 5.0f;

void initGLFW() {
	if (!glfwInit()) {
		throw std::runtime_error("Failed to init GLFW.");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	if (!glfwVulkanSupported()) {
		throw std::runtime_error("Failed to support Vulkan.");
	}

	glfw_context.window = glfwCreateWindow(glfw_context.Window_Creation_Width, glfw_context.Window_Creation_Height, glfw_context.Window_Creation_Title, nullptr, nullptr);

	if (glfw_context.window == nullptr) {
		throw std::runtime_error("Failed to create a GLFW window.");
	}

	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

	int xPos = (mode->width - glfw_context.Window_Creation_Width) / 2;
	int yPos = (mode->height - glfw_context.Window_Creation_Height) / 2;

	glfwSetWindowPos(glfw_context.window, xPos, yPos);
}

void initVolk() {
	if (volkInitialize() != VK_SUCCESS) {
		throw std::runtime_error("Failed to init Volk.");
	}
}

void VulkanCreateInstance() {

	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	vkb::InstanceBuilder insBuilder;
	auto ins_ret = insBuilder
		.set_app_name("Vulkan 3D App")
		.enable_extensions(glfwExtensionCount, glfwExtensions)
		.require_api_version(1, 3, 0)
		.request_validation_layers()
		.use_default_debug_messenger()
		.build();

	if (!ins_ret) {
		throw std::runtime_error("Failed to create an Instance.");
	}

	vk_context.vkb_instance = ins_ret.value();

	volkLoadInstance(vk_context.vkb_instance.instance);
}

void GLFWCreateVulkanSurface() {
	if (glfwCreateWindowSurface(vk_context.vkb_instance.instance, glfw_context.window, nullptr, &vk_context.vk_surface) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Surface.");
	}
}

void VulkanSelectPhysicalDevice() {

	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.timelineSemaphore = VK_TRUE;

	VkPhysicalDeviceFeatures requiredFeatures{};
	requiredFeatures.samplerAnisotropy = VK_TRUE;

	std::vector<const char*> requiredExts = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	vkb::PhysicalDeviceSelector physicalDeviceSelector(vk_context.vkb_instance);
	auto selectorReturn = physicalDeviceSelector
		.set_surface(vk_context.vk_surface)
		.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
		.set_minimum_version(1, 3)
		.set_required_features_12(features12)
		.set_required_features_13(features13)
		.set_required_features(requiredFeatures)
		.require_dedicated_transfer_queue()
		.add_required_extensions(requiredExts)
		.select();

	if (!selectorReturn) {
		throw std::runtime_error("Failed to select a Physical Device.");
	}

	vk_context.vkb_physicalDevice = selectorReturn.value();
}

void VulkanCreateDevice() {

	vkb::DeviceBuilder deviceBuilder(vk_context.vkb_physicalDevice);
	auto devic_ret = deviceBuilder
		.build();

	if (!devic_ret) {
		throw std::runtime_error("Failed to create a Device.");
	}

	vk_context.vkb_device = devic_ret.value();

	volkLoadDevice(vk_context.vkb_device.device);
}

void VulkanGetQueue() {
	auto graphics_queue_ret = vk_context.vkb_device.get_queue(vkb::QueueType::graphics);

	if (!graphics_queue_ret) {
		throw std::runtime_error("Failed to get Graphic Queue.");
	}

	auto present_queue_ret = vk_context.vkb_device.get_queue(vkb::QueueType::present);

	if (!present_queue_ret) {
		throw std::runtime_error("Failed to get Present Queue.");
	}

	vk_context.vk_graphics_queue = graphics_queue_ret.value();
	vk_context.vk_present_queue = present_queue_ret.value();
}

void VulkanDestroyImageViews() {
	for (auto imageView : vk_context.vk_swapchain_image_views) {
		vkDestroyImageView(vk_context.vkb_device.device, imageView, nullptr);
	}
	vk_context.vk_swapchain_images.clear();
}

void VulkanCreateImageBuffer(
	VkImage &image,
	VkImageView &image_view,
	VmaAllocation &alloc,
	VkFormat format,
	VkImageUsageFlags usage,
	VkSampleCountFlagBits samples,
	VkImageAspectFlags aspectMask
) {
	vmaDestroyImage(vma_allocator, image, alloc);
	vkDestroyImageView(vk_context.vkb_device.device, image_view, nullptr);

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.pNext = nullptr;

	imageInfo.flags = 0;

	imageInfo.format = format;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = VkExtent3D{
		.width = vk_context.vk_swapchain_extent.width,
		.height = vk_context.vk_swapchain_extent.height,
		.depth = 1
	};

	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	imageInfo.arrayLayers = 1;
	imageInfo.samples = samples;
	imageInfo.mipLevels = 1;

	VmaAllocationCreateInfo imgAllocation{};
	imgAllocation.usage = VMA_MEMORY_USAGE_AUTO;
	imgAllocation.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (vmaCreateImage(vma_allocator, &imageInfo, &imgAllocation, &image, &alloc, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create an Image.");
	}

	VkImageViewCreateInfo imageViewInfo{};
	imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewInfo.pNext = nullptr;

	imageViewInfo.image = image;

	imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewInfo.format = format;

	imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	imageViewInfo.subresourceRange.aspectMask = aspectMask;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.baseMipLevel = 0;
	imageViewInfo.subresourceRange.layerCount = 1;
	imageViewInfo.subresourceRange.levelCount = 1;

	if (vkCreateImageView(vk_context.vkb_device.device, &imageViewInfo, nullptr, &image_view) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create an Image View.");
	}
}

void VulkanCreateDepthBuffer() {
	VulkanCreateImageBuffer(
		vk_context.vk_depth_buffer_image,
		vk_context.vk_depth_buffer_image_view,
		vk_context.vma_depth_buffer_allocation,
		vk_context.vk_depth_buffer_image_format,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		vk_context.msaa_samples,
		VK_IMAGE_ASPECT_DEPTH_BIT
	);
}

void VulkanCreateMSAAImages() {
	VulkanCreateImageBuffer(
		vk_context.vk_msaa_image,
		vk_context.vk_msaa_image_view,
		vk_context.vma_msaa_allocation,
		vk_context.vk_swapchain_image_format,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		vk_context.msaa_samples,
		VK_IMAGE_ASPECT_COLOR_BIT
	);
}

void VulkanCreateSwapchainAndImages() {

	vkDeviceWaitIdle(vk_context.vkb_device.device);

	int w, h;

	glfwGetFramebufferSize(glfw_context.window, &w, &h);

	while (w == 0 || h == 0) {
		glfwWaitEvents();
		glfwGetFramebufferSize(glfw_context.window, &w, &h);
	}

	VulkanDestroyImageViews();

	vk_context.vk_swapchain_extent = VkExtent2D(w, h);

	VkSurfaceFormatKHR swapchainDesiredFormat{};
	swapchainDesiredFormat.format = vk_context.vk_swapchain_image_format;
	swapchainDesiredFormat.colorSpace = vk_context.vk_swapchain_colorspace;

	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_context.vkb_device.physical_device, vk_context.vk_surface, &capabilities);

	// 2. Determine the best alpha flag available (defaulting to Opaque for performance)
	VkCompositeAlphaFlagBitsKHR chosen_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	if (!(capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
		if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
			chosen_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		}
		else if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
			chosen_alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
		}
		else if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
			chosen_alpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
		}
	}

	vkb::SwapchainBuilder swapchainBuilder(vk_context.vkb_device);
	auto swap_ret = swapchainBuilder
		.set_required_min_image_count(2)
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
		.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
		.set_clipped(true)
		.set_desired_extent(w, h)
		.set_desired_format(swapchainDesiredFormat)
		.set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		.set_composite_alpha_flags(chosen_alpha)
		.set_old_swapchain(vk_context.vkb_swapchain)
		.build();

	if (!swap_ret) {
		vk_context.vkb_swapchain.swapchain = VK_NULL_HANDLE;
	}

	vkb::destroy_swapchain(vk_context.vkb_swapchain);

	vk_context.vkb_swapchain = swap_ret.value();

	uint32_t imageCount = swap_ret.value().requested_min_image_count;

	vk_context.vk_swapchain_images.resize(imageCount);

	vkGetSwapchainImagesKHR(vk_context.vkb_device.device, vk_context.vkb_swapchain.swapchain, &imageCount, vk_context.vk_swapchain_images.data());

	vk_context.vk_swapchain_image_views.resize(vk_context.vk_swapchain_images.size());

	for (uint32_t i = 0; i < vk_context.vk_swapchain_images.size(); i++) {
		VkImageViewCreateInfo imageViewInfo{};
		imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewInfo.pNext = nullptr;

		imageViewInfo.image = vk_context.vk_swapchain_images[i];

		imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewInfo.format = vk_context.vk_swapchain_image_format;

		imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewInfo.subresourceRange.baseArrayLayer = 0;
		imageViewInfo.subresourceRange.baseMipLevel = 0;
		imageViewInfo.subresourceRange.layerCount = 1;
		imageViewInfo.subresourceRange.levelCount = 1;

		if (vkCreateImageView(vk_context.vkb_device.device, &imageViewInfo, nullptr, &vk_context.vk_swapchain_image_views[i]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create an Image View.");
		}
	}

	VulkanCreateDepthBuffer();
	VulkanCreateMSAAImages();
}

VkShaderModule VulkanCreateModule(const std::string& shader_path) {

	std::vector<char> shader_code = readFile(shader_path);

	VkShaderModuleCreateInfo shaderInfo{};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.pNext = nullptr;
	shaderInfo.flags = 0;

	shaderInfo.codeSize = shader_code.size();
	shaderInfo.pCode = reinterpret_cast<const uint32_t*>(shader_code.data());

	VkShaderModule shaderModule;

	if (vkCreateShaderModule(vk_context.vkb_device.device, &shaderInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Vertex Shader Module.");
	}

	return shaderModule;

}

void VulkanCreateGraphicsPipeline() {

	VkDescriptorSetLayoutBinding cameraDataBinding{};
	cameraDataBinding.binding = 0;
	cameraDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraDataBinding.descriptorCount = 1;
	cameraDataBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutBinding textureBinding{};
	textureBinding.binding = 0;
	textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureBinding.descriptorCount = 1;
	textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding textureUVScaleBinding{};
	textureUVScaleBinding.binding = 1;
	textureUVScaleBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	textureUVScaleBinding.descriptorCount = 1;
	textureUVScaleBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::vector<VkDescriptorSetLayoutBinding> globalVertexLayoutBindings{ cameraDataBinding };
	std::vector<VkDescriptorSetLayoutBinding> globalFragmentLayoutBindings{ textureBinding, textureUVScaleBinding };

	VkDescriptorSetLayoutCreateInfo globalVertexLayoutInfo{};
	globalVertexLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	globalVertexLayoutInfo.pNext = nullptr;

	globalVertexLayoutInfo.bindingCount = static_cast<uint32_t>(globalVertexLayoutBindings.size());
	globalVertexLayoutInfo.pBindings = globalVertexLayoutBindings.data();

	if (vkCreateDescriptorSetLayout(vk_context.vkb_device.device, &globalVertexLayoutInfo, nullptr, &vk_context.global_vertex_descriptor_set_layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Descriptor Set Layout.");
	}

	VkDescriptorSetLayoutCreateInfo globalFragmentLayoutInfo{};
	globalFragmentLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	globalFragmentLayoutInfo.pNext = nullptr;

	globalFragmentLayoutInfo.bindingCount = static_cast<uint32_t>(globalFragmentLayoutBindings.size());
	globalFragmentLayoutInfo.pBindings = globalFragmentLayoutBindings.data();

	if (vkCreateDescriptorSetLayout(vk_context.vkb_device.device, &globalFragmentLayoutInfo, nullptr, &vk_context.global_fragment_descriptor_set_layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Descriptor Set Layout.");
	}

	VkDescriptorSetLayout descriptorSetLayouts[] = { 
		vk_context.global_vertex_descriptor_set_layout,
		vk_context.global_fragment_descriptor_set_layout
	};

	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pNext = nullptr;

	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 10;

	if (vkCreateDescriptorPool(vk_context.vkb_device.device, &poolInfo, nullptr, &vk_context.descriptor_pool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create the Descriptor Pool.");
	}

	VkPushConstantRange modelMatrixPushConstant{};
	modelMatrixPushConstant.offset = 0;
	modelMatrixPushConstant.size = sizeof(MeshPushConstants);
	modelMatrixPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo graphicsPipelineLayoutInfo{};
	graphicsPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	graphicsPipelineLayoutInfo.pNext = nullptr;

	graphicsPipelineLayoutInfo.setLayoutCount = 2;
	graphicsPipelineLayoutInfo.pushConstantRangeCount = 1;
	graphicsPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
	graphicsPipelineLayoutInfo.pPushConstantRanges = &modelMatrixPushConstant;

	if (vkCreatePipelineLayout(vk_context.vkb_device.device, &graphicsPipelineLayoutInfo, nullptr, &vk_context.vk_graphics_pipeline_layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Graphics Pipeline Layout.");
	}

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
	colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendInfo.pNext = nullptr;

	colorBlendInfo.flags = 0;

	colorBlendInfo.logicOpEnable = VK_FALSE;
	colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;

	colorBlendInfo.attachmentCount = 1;
	colorBlendInfo.pAttachments = &colorBlendAttachment;

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
	depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilInfo.pNext = nullptr;

	depthStencilInfo.flags = 0;

	depthStencilInfo.front = {};
	depthStencilInfo.back = {};

	depthStencilInfo.depthTestEnable = VK_TRUE;
	depthStencilInfo.depthWriteEnable = VK_TRUE;
	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;

	depthStencilInfo.depthBoundsTestEnable = VK_FALSE;

	depthStencilInfo.maxDepthBounds = 1.0f;
	depthStencilInfo.minDepthBounds = 0.0f;

	depthStencilInfo.stencilTestEnable = VK_FALSE;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.pNext = nullptr;

	dynamicStateInfo.flags = 0;

	dynamicStateInfo.dynamicStateCount = 2;
	dynamicStateInfo.pDynamicStates = dynamicStates;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.pNext = nullptr;

	inputAssemblyInfo.flags = 0;

	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisamplingInfo{};
	multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingInfo.pNext = nullptr;

	multisamplingInfo.flags = 0;

	multisamplingInfo.rasterizationSamples = vk_context.msaa_samples;
	multisamplingInfo.sampleShadingEnable = VK_FALSE;
	multisamplingInfo.minSampleShading = 1.0f;
	multisamplingInfo.pSampleMask = nullptr;
	multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingInfo.alphaToOneEnable = VK_FALSE;

	VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
	rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationInfo.pNext = nullptr;

	rasterizationInfo.flags = 0;

	rasterizationInfo.depthClampEnable = VK_FALSE;
	rasterizationInfo.depthBiasConstantFactor = 0.0f;
	rasterizationInfo.depthBiasClamp = 0.0f;
	rasterizationInfo.depthBiasSlopeFactor = 0.0f;

	rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	rasterizationInfo.lineWidth = 1.0f;
	rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;

	VkShaderModule vertexShaderModule = VulkanCreateModule("shaders/vert.spv");
	VkShaderModule fragmentShaderModule = VulkanCreateModule("shaders/frag.spv");

	VkPipelineShaderStageCreateInfo vertexShaderStage{};
	vertexShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertexShaderStage.pNext = nullptr;

	vertexShaderStage.flags = 0;

	vertexShaderStage.module = vertexShaderModule;
	vertexShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	
	vertexShaderStage.pName = "main";
	vertexShaderStage.pSpecializationInfo = nullptr;

	VkPipelineShaderStageCreateInfo fragmentShaderStage{};
	fragmentShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragmentShaderStage.pNext = nullptr;

	fragmentShaderStage.flags = 0;

	fragmentShaderStage.module = fragmentShaderModule;
	fragmentShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

	fragmentShaderStage.pName = "main";
	fragmentShaderStage.pSpecializationInfo = nullptr;

	VkPipelineShaderStageCreateInfo shaderStages[] { 
		vertexShaderStage, 
		fragmentShaderStage 
	};

	VkVertexInputAttributeDescription positionAttribute{};
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT; // 3 Float
	positionAttribute.offset = offsetof(Vertex, position);
	positionAttribute.location = 0;
	positionAttribute.binding = 0;

	VkVertexInputAttributeDescription uvAttribute{};
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT; // 2 Float
	uvAttribute.offset = offsetof(Vertex,uv);
	uvAttribute.location = 1;
	uvAttribute.binding = 0;

	VkVertexInputAttributeDescription normalAttribute{};
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT; // 3 Float
	normalAttribute.offset = offsetof(Vertex, normal);
	normalAttribute.location = 2;
	normalAttribute.binding = 0;

	VkVertexInputBindingDescription vertexBindingDesc{};
	vertexBindingDesc.binding = 0;
	vertexBindingDesc.stride = sizeof(Vertex);
	vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[] = {
		positionAttribute,
		uvAttribute,
		normalAttribute
	};

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.pNext = nullptr;

	vertexInputInfo.flags = 0;

	vertexInputInfo.vertexAttributeDescriptionCount = 3;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;
	vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;

	VkPipelineTessellationStateCreateInfo tessellationInfo{};
	tessellationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
	tessellationInfo.pNext = nullptr;
	tessellationInfo.flags = 0;

	tessellationInfo.patchControlPoints = 0;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.pNext = nullptr;

	viewportInfo.flags = 0;

	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

	viewportInfo.pViewports = nullptr;
	viewportInfo.pScissors = nullptr;

	VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
	pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	pipelineRenderingInfo.pNext = nullptr;

	pipelineRenderingInfo.colorAttachmentCount = 1;
	pipelineRenderingInfo.pColorAttachmentFormats = &vk_context.vk_swapchain_image_format;

	pipelineRenderingInfo.depthAttachmentFormat = vk_context.vk_depth_buffer_image_format;
	pipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

	VkGraphicsPipelineCreateInfo graphicsPipelineInfo{};
	graphicsPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphicsPipelineInfo.flags = 0;

	graphicsPipelineInfo.basePipelineHandle = nullptr;
	graphicsPipelineInfo.basePipelineIndex = -1;

	graphicsPipelineInfo.layout = vk_context.vk_graphics_pipeline_layout;

	graphicsPipelineInfo.subpass = 0;
	graphicsPipelineInfo.renderPass = VK_NULL_HANDLE;

	graphicsPipelineInfo.pColorBlendState = &colorBlendInfo;
	graphicsPipelineInfo.pDepthStencilState = &depthStencilInfo;
	graphicsPipelineInfo.pDynamicState = &dynamicStateInfo;
	graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	graphicsPipelineInfo.pMultisampleState = &multisamplingInfo;
	graphicsPipelineInfo.pRasterizationState = &rasterizationInfo;

	graphicsPipelineInfo.stageCount = 2;
	graphicsPipelineInfo.pStages = shaderStages;

	graphicsPipelineInfo.pTessellationState = &tessellationInfo;
	graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
	graphicsPipelineInfo.pViewportState = &viewportInfo;

	graphicsPipelineInfo.pNext = &pipelineRenderingInfo;

	if (vkCreateGraphicsPipelines(vk_context.vkb_device.device, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, nullptr, &vk_context.vk_graphics_pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create the Graphics Pipeline.");
	}

	vkDestroyShaderModule(vk_context.vkb_device.device, vertexShaderModule, nullptr);
	vkDestroyShaderModule(vk_context.vkb_device.device, fragmentShaderModule, nullptr);

}

void VmaCreateMainAllocator() {
	VmaAllocatorCreateInfo allocatorInfo{};
	VmaVulkanFunctions vmaFuncs{};

	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.instance = vk_context.vkb_instance.instance;
	allocatorInfo.device = vk_context.vkb_device.device;
	allocatorInfo.physicalDevice = vk_context.vkb_physicalDevice.physical_device;

	vmaImportVulkanFunctionsFromVolk(&allocatorInfo,&vmaFuncs);

	allocatorInfo.pVulkanFunctions = &vmaFuncs;

	if (vmaCreateAllocator(&allocatorInfo, &vma_allocator) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a VMA Allocator.");
	}
}

VkCommandPool VulkanCreateCommandPool(VkDevice device, VkCommandPoolCreateFlags cmdPoolflags, uint32_t cmdPoolqueueFamilyIndex) {
	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	
	cmdPoolInfo.flags = cmdPoolflags;
	cmdPoolInfo.queueFamilyIndex = cmdPoolqueueFamilyIndex;
	
	VkCommandPool cmdPool;

	if (vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Command Pool.");
	}

	return cmdPool;
}

VkCommandBuffer VulkanCreateCommandBuffer(VkDevice device,VkCommandBufferLevel cmdBufferlevel,uint32_t count, VkCommandPool cmdPool) {

	VkCommandBuffer cmdBuffer;

	VkCommandBufferAllocateInfo cmdBufferAllocInfo{};
	cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdBufferAllocInfo.pNext = nullptr;

	cmdBufferAllocInfo.commandBufferCount = count;
	cmdBufferAllocInfo.commandPool = cmdPool;

	cmdBufferAllocInfo.level = cmdBufferlevel;

	if (vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &cmdBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Command Buffer.");
	}

	return cmdBuffer;

}

VkSemaphore VulkanCreateBinarySemaphore(VkDevice device) {

	VkSemaphore semaphore;

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreInfo.pNext = nullptr;
	semaphoreInfo.flags = 0;

	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Binary Semaphore.");
	}

	return semaphore;
}

VkSemaphore VulkanCreateTimelineSemapohre(VkDevice device) {
	VkSemaphore timelineSemaphore;

	VkSemaphoreTypeCreateInfo timelineSemaphoreTypeInfo{};
	timelineSemaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	timelineSemaphoreTypeInfo.pNext = nullptr;
	timelineSemaphoreTypeInfo.initialValue = 0;
	timelineSemaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

	VkSemaphoreCreateInfo timelineSemaphoreInfo{};
	timelineSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	timelineSemaphoreInfo.flags = 0;
	timelineSemaphoreInfo.pNext = &timelineSemaphoreTypeInfo;

	if (vkCreateSemaphore(device, &timelineSemaphoreInfo, nullptr, &timelineSemaphore) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a Timeline Semaphore.");
	}

	return timelineSemaphore;
}

void VulkanImageMemoryBarrier(
	VkCommandBuffer cmdbuffer,
	VkImage image,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStageMask,
	VkAccessFlags2 srcAccessMask,
	VkPipelineStageFlags2 dstStageMask,
	VkAccessFlags2 dstAccessMask,
	VkImageSubresourceRange subresourceRange
) {

	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.pNext = nullptr;

	barrier.image = image;

	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;

	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	barrier.srcStageMask = srcStageMask;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.dstAccessMask = dstAccessMask;

	barrier.subresourceRange = subresourceRange;

	VkDependencyInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	info.pNext = nullptr;

	info.imageMemoryBarrierCount = 1;
	info.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmdbuffer, &info);

}

void CreateTexture(Texture& tex, const std::string& path, float anisotropyLevel, VkFilter filter) {
	int texWidth, texHeight, nrChannels;
	unsigned char* data = stbi_load(path.c_str(), &texWidth, &texHeight, &nrChannels, STBI_rgb_alpha);
	if (!data) {
		throw std::runtime_error("Failed to load a Texture.");
	}

	uint32_t width = static_cast<uint32_t>(texWidth);
	uint32_t height = static_cast<uint32_t>(texHeight);

	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

	VkDeviceSize imageSize = width * height * 4;

	Buffer tempBuffer;
	CreateBuffer(
		tempBuffer,
		imageSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		VMA_MEMORY_USAGE_AUTO,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
	);

	memcpy(tempBuffer.mappedData, data, static_cast<size_t>(imageSize));

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.pNext = nullptr;

	imageInfo.imageType = VK_IMAGE_TYPE_2D;

	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	if (vmaCreateImage(vma_allocator, &imageInfo, &imgAllocInfo, &tex.image, &tex.alloc, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Texture Image.");
	}

	VkCommandPool tempPool = VulkanCreateCommandPool(vk_context.vkb_device.device, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 0);
	VkCommandBuffer tempCmdBuffer = VulkanCreateCommandBuffer(vk_context.vkb_device.device, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, tempPool);

	VkCommandBufferBeginInfo beginInfo{ 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(tempCmdBuffer, &beginInfo);

	// Init Barrier
	VulkanImageMemoryBarrier(
		tempCmdBuffer,
		tex.image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		0,
		VK_PIPELINE_STAGE_2_COPY_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1 , 0 , 1 }
	);

	VkBufferImageCopy2 copyRegion{};
	copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
	copyRegion.pNext = nullptr;

	copyRegion.bufferImageHeight = 0;
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;

	copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = 1;
	copyRegion.imageSubresource.mipLevel = 0;

	copyRegion.imageOffset = { 0 , 0, 0 };
	copyRegion.imageExtent.width = width;
	copyRegion.imageExtent.height = height;
	copyRegion.imageExtent.depth = 1;

	VkCopyBufferToImageInfo2 copyBufferImageInfo{};
	copyBufferImageInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
	copyBufferImageInfo.pNext = nullptr;

	copyBufferImageInfo.srcBuffer = tempBuffer.buffer;

	copyBufferImageInfo.dstImage = tex.image;
	copyBufferImageInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	copyBufferImageInfo.regionCount = 1;
	copyBufferImageInfo.pRegions = &copyRegion;

	vkCmdCopyBufferToImage2(tempCmdBuffer, &copyBufferImageInfo);

	uint32_t mipWidth = width;
	uint32_t mipHeight = height;

	// Map Init Barrier
	VulkanImageMemoryBarrier(
		tempCmdBuffer,
		tex.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_READ_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	for (uint32_t level = 1; level < mipLevels; level++) {

		// Mip Barrier #1
		VulkanImageMemoryBarrier(
			tempCmdBuffer,
			tex.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1}
		);

		VkImageBlit2 imageBlit{};
		imageBlit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
		imageBlit.pNext = nullptr;

		imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlit.srcSubresource.layerCount = 1;

		imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlit.dstSubresource.layerCount = 1;
		
		imageBlit.srcSubresource.mipLevel = level - 1;
		imageBlit.dstSubresource.mipLevel = level;

		imageBlit.srcOffsets[0] = { 0 , 0 , 0 };
		imageBlit.srcOffsets[1] = {
			static_cast<int32_t>(mipWidth),
			static_cast<int32_t>(mipHeight),
			1
		};

		mipWidth = std::max(1u, mipWidth >> 1);
		mipHeight = std::max(1u, mipHeight >> 1);

		imageBlit.dstOffsets[0] = { 0 , 0 , 0 };
		imageBlit.dstOffsets[1] = {
			static_cast<int32_t>(mipWidth),
			static_cast<int32_t>(mipHeight),
			1
		};

		VkBlitImageInfo2 imageBlitInfo{};
		imageBlitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
		imageBlitInfo.pNext = nullptr;

		imageBlitInfo.srcImage = tex.image;
		imageBlitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		imageBlitInfo.dstImage = tex.image;
		imageBlitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		imageBlitInfo.filter = VK_FILTER_LINEAR;

		imageBlitInfo.regionCount = 1;
		imageBlitInfo.pRegions = &imageBlit;

		vkCmdBlitImage2(tempCmdBuffer, &imageBlitInfo);
		// Mip Barrier #2
		VulkanImageMemoryBarrier(
			tempCmdBuffer,
			tex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, level , 1, 0, 1 }
		);

	}

	// Final Barrier
	VulkanImageMemoryBarrier(
		tempCmdBuffer,
		tex.image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_BLIT_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 }
	);

	vkEndCommandBuffer(tempCmdBuffer);

	VkCommandBufferSubmitInfo cmdBufferSubmitInfo{};
	cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdBufferSubmitInfo.pNext = nullptr;

	cmdBufferSubmitInfo.commandBuffer = tempCmdBuffer;
	cmdBufferSubmitInfo.deviceMask = 0;

	VkSubmitInfo2 submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.pNext = nullptr;

	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;

	vkQueueSubmit2(vk_context.vk_graphics_queue, 1, &submitInfo, nullptr);

	vkQueueWaitIdle(vk_context.vk_graphics_queue);

	VkImageViewCreateInfo imageViewInfo{};
	imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewInfo.pNext = nullptr;

	imageViewInfo.image = tex.image;

	imageViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;

	imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

	imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.baseMipLevel = 0;
	imageViewInfo.subresourceRange.layerCount = 1;
	imageViewInfo.subresourceRange.levelCount = mipLevels;

	if (vkCreateImageView(vk_context.vkb_device.device, &imageViewInfo, nullptr, &tex.image_view) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Texture Image View.");
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.pNext = nullptr;

	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = anisotropyLevel;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = static_cast<float>(mipLevels);

	if (vkCreateSampler(vk_context.vkb_device.device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Texture Sampler.");
	}

	vkDestroyCommandPool(vk_context.vkb_device.device, tempPool, nullptr);

	tempBuffer.free();

	stbi_image_free(data);
}

void initVulkan() {
	VulkanCreateInstance();

	GLFWCreateVulkanSurface();

	VulkanSelectPhysicalDevice();
	VulkanCreateDevice();

	VmaCreateMainAllocator();

	VulkanGetQueue();
	VulkanCreateSwapchainAndImages();
	VulkanCreateGraphicsPipeline();

	uint32_t graphicsQueueFamilyIndex = vk_context.vkb_device.get_queue_index(vkb::QueueType::graphics).value();
	
	for (int i = 0; i < vk_context.MAX_FRAMES_IN_FLIGHT; i++) {

		VkCommandPool cmd_pool = VulkanCreateCommandPool(vk_context.vkb_device.device,VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, graphicsQueueFamilyIndex);
		VkCommandBuffer cmd_buffer = VulkanCreateCommandBuffer(vk_context.vkb_device.device,VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, cmd_pool);
		vk_context.vk_cmd_pools.push_back(cmd_pool);
		vk_context.vk_cmd_buffers.push_back(cmd_buffer);

		VkSemaphore imageAvaliableSemaphore = VulkanCreateBinarySemaphore(vk_context.vkb_device.device);
		vk_context.vk_image_avaliable_semaphores.push_back(imageAvaliableSemaphore);

		VkSemaphore renderFinishedSemaphore = VulkanCreateBinarySemaphore(vk_context.vkb_device.device);
		vk_context.vk_render_finished_semaphores.push_back(renderFinishedSemaphore);
	} 
	
	vk_context.vk_timeline_semaphore = VulkanCreateTimelineSemapohre(vk_context.vkb_device.device);

}

const float sensitivity = 0.05f;
const float pitchClamp = 85.0f;
double lastX = 0, lastY = 0;

bool cursorlocked = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
	if (cursorlocked) {
		double xoffset = xpos - lastX;
		double yoffset = lastY - ypos; // reversed since y-coordinates range from bottom to top

		xoffset *= sensitivity;
		yoffset *= sensitivity;

		camera.rotation.y += (float)xoffset;
		camera.rotation.x += (float)yoffset;

		if (camera.rotation.y > 360.0f) {
			camera.rotation.y -= 360.0f;
		}
		else if (camera.rotation.y < -360.0f) {
			camera.rotation.y += 360.0f;
		}

		if (camera.rotation.x > pitchClamp) {
			camera.rotation.x = pitchClamp;
		}
		else if (camera.rotation.x < -pitchClamp) {
			camera.rotation.x = -pitchClamp;
		}
	}

	lastX = xpos;
	lastY = ypos;

}

uint32_t Framecounter = 0;
double lastTime = glfwGetTime();
double lastFrame = glfwGetTime();

bool escapePressedLastFrame = false;
bool f11PressedLastFrame = false;

bool fullscreen = false;

int windowedX = 0;
int windowedY = 0;
int windowedWidth = 800;
int windowedHeight = 600;

void processInputs(GLFWwindow* win) {

	float dt = (float)(glfwGetTime() - lastFrame);
	lastFrame = glfwGetTime();

	bool escapePressedNow = (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS);

	if (escapePressedNow && !escapePressedLastFrame) {

		if (cursorlocked) {
			cursorlocked = false;
			glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
		else {
			cursorlocked = true;
			glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	}

	escapePressedLastFrame = escapePressedNow;

	bool f11PressedNow = (glfwGetKey(win, GLFW_KEY_F11) == GLFW_PRESS);

	if (f11PressedNow && !f11PressedLastFrame) {

		if (fullscreen) {
			fullscreen = false;

			glfwSetWindowMonitor(glfw_context.window, NULL, windowedX, windowedY, windowedWidth, windowedHeight, 0);
		}
		else {
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);

			glfwGetWindowPos(glfw_context.window, &windowedX, &windowedY);
			glfwGetWindowSize(glfw_context.window, &windowedWidth, &windowedHeight);

			fullscreen = true;
			glfwSetWindowMonitor(glfw_context.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
		}
	}

	f11PressedLastFrame = f11PressedNow;

	glfwSetCursorPosCallback(win, mouse_callback);

	float yaw = glm::radians(-camera.rotation.y);

	float cYaw = cos(yaw);
	float sYaw = sin(yaw);
	
	glm::vec3 input_dir(0.0f);

	if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT)) {
		camera_speed = 12.0f;
	}
	else {
		camera_speed = 6.0f;
	}

	if (glfwGetKey(win, GLFW_KEY_W)) {
		input_dir.z -= cYaw;
		input_dir.x -= sYaw;
	}
	if (glfwGetKey(win, GLFW_KEY_S)) {
		input_dir.z += cYaw;
		input_dir.x += sYaw;
	}
	if (glfwGetKey(win, GLFW_KEY_D)) {
		input_dir.x += cYaw;
		input_dir.z -= sYaw;
	}
	if (glfwGetKey(win, GLFW_KEY_A)) {
		input_dir.x -= cYaw;
		input_dir.z += sYaw;
	}

	float speed_dt = camera_speed * dt;

	if (glfwGetKey(win, GLFW_KEY_E)) {
		camera.position.y += speed_dt;
	}
	if (glfwGetKey(win, GLFW_KEY_Q)) {
		camera.position.y -= speed_dt;
	}

	if (glm::length(input_dir) > 0.0f) {
		input_dir = glm::normalize(input_dir);
	}

	camera.position.z += speed_dt * input_dir.z;
	camera.position.x += speed_dt * input_dir.x;

}

Mesh cube;
Mesh ground;
Mesh customMesh;

Texture defaultTexture;
Texture sillyTexture;
Texture grassTexture;

Material defaultMaterial;
Material sillyMaterial;
Material grassMaterial;

void mainLoop() {

	glfwSetInputMode(glfw_context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(glfw_context.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}
	glfwSetCursorPos(glfw_context.window, lastX, lastY);

	while (!glfwWindowShouldClose(glfw_context.window)) {

		glfwPollEvents();
		processInputs(glfw_context.window);

		int currentFrame = vk_context.current_frame;
		int currentIndex = (currentFrame % vk_context.MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreWaitInfo timelineWaitInfo{};
		timelineWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		timelineWaitInfo.pNext = nullptr;

		timelineWaitInfo.semaphoreCount = 1;
		timelineWaitInfo.pSemaphores = &vk_context.vk_timeline_semaphore;
		timelineWaitInfo.pValues = &vk_context.timeline_values[currentIndex];

		vkWaitSemaphores(vk_context.vkb_device.device, &timelineWaitInfo, UINT64_MAX);

		VkCommandPool cmd_pool = vk_context.vk_cmd_pools[currentIndex];
		vkResetCommandPool(vk_context.vkb_device.device, cmd_pool, 0);

		vk_context.timeline_values[currentIndex] = currentFrame + 1;

		uint32_t ImageIndex;
		VkSemaphore image_avaliable_semaphore = vk_context.vk_image_avaliable_semaphores[currentIndex];
		VkSemaphore render_finished_semaphore = vk_context.vk_render_finished_semaphores[currentIndex];

		VkResult acquireResult = vkAcquireNextImageKHR(vk_context.vkb_device.device, vk_context.vkb_swapchain.swapchain, UINT64_MAX, image_avaliable_semaphore, VK_NULL_HANDLE, &ImageIndex);

		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {

			VulkanCreateSwapchainAndImages();

			return;
		}
		else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("Failed to Acquire Swapchain Image.");
		}

		VkCommandBufferBeginInfo cmdBufferBeginInfo{};
		cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBufferBeginInfo.pNext = nullptr;
		cmdBufferBeginInfo.flags = 0;
		cmdBufferBeginInfo.pInheritanceInfo = nullptr;

		VkCommandBuffer cmd_buffer = vk_context.vk_cmd_buffers[currentIndex];

		vkBeginCommandBuffer(vk_context.vk_cmd_buffers[currentIndex], &cmdBufferBeginInfo);

		// Render Barrier
		VulkanImageMemoryBarrier(
			cmd_buffer,
			vk_context.vk_swapchain_images[ImageIndex],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1 , 0 , 1 }
		);

		VkImageView swapchainImageView = vk_context.vk_swapchain_image_views[ImageIndex];

		VkRenderingAttachmentInfo colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.pNext = nullptr;

		colorAttachment.imageView = vk_context.vk_msaa_image_view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachment.resolveImageView = swapchainImageView;
		colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		colorAttachment.clearValue.color = { { 0.0f,0.15f,0.5f,1.0f } };

		VkRenderingAttachmentInfo depthAttachment{};
		depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.pNext = nullptr;

		depthAttachment.imageView = vk_context.vk_depth_buffer_image_view;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

		depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
		depthAttachment.resolveImageView = VK_NULL_HANDLE;
		depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.pNext = nullptr;

		renderingInfo.flags = 0;

		renderingInfo.layerCount = 1;
		renderingInfo.renderArea = { { 0,0 }, vk_context.vk_swapchain_extent };

		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;

		renderingInfo.pDepthAttachment = &depthAttachment;
		renderingInfo.pStencilAttachment = nullptr;

		vkCmdBeginRendering(cmd_buffer, &renderingInfo);

		vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_context.vk_graphics_pipeline);

		VkExtent2D swapchain_extent = vk_context.vk_swapchain_extent;

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;

		viewport.width = (float)swapchain_extent.width;
		viewport.height = (float)swapchain_extent.height;

		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0,0 };
		scissor.extent = swapchain_extent;
		vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

		// ACTUALLY DRAWING SHIT
		glm::mat4 VPMatrix = camera.getVPMatrix(viewport.width / viewport.height);

		memcpy(camera_uniform_buffer.mappedData, &VPMatrix, sizeof(glm::mat4));

		vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_context.vk_graphics_pipeline_layout, 0, 1, &globalVertexSet, 0, 0);

		glm::mat4 cubeModelMatirx = glm::mat4(1.0f);
		cubeModelMatirx = glm::rotate(cubeModelMatirx, glm::radians(((float)glfwGetTime()) * 60.0f), glm::vec3(0.5f, 1.0f, -0.7f));

		vkCmdPushConstants(cmd_buffer, vk_context.vk_graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &cubeModelMatirx);

		// Bind Texture
		sillyMaterial.bind(cmd_buffer, glm::vec2(1.0f));

		cube.draw(cmd_buffer);

		glm::mat4 groundModelMatrix = glm::mat4(1.0f);
		groundModelMatrix = glm::translate(groundModelMatrix, glm::vec3(0.0f, -2.0f, 0.0f));
		groundModelMatrix = glm::scale(groundModelMatrix, glm::vec3(50.0f, 1.0f, 50.0f));

		vkCmdPushConstants(cmd_buffer, vk_context.vk_graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &groundModelMatrix);

		// Bind Texture
		grassMaterial.bind(cmd_buffer, glm::vec2(20.0f));

		ground.draw(cmd_buffer);

		glm::mat4 customModelMatrix = glm::mat4(1.0f);
		customModelMatrix = glm::translate(customModelMatrix, glm::vec3(-4.0f, -0.9f, -4.0f));
		customModelMatrix = glm::rotate(customModelMatrix, glm::radians(25.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		customModelMatrix = glm::scale(customModelMatrix, glm::vec3(.35f));

		vkCmdPushConstants(cmd_buffer, vk_context.vk_graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &customModelMatrix);

		// Bind Texture
		defaultMaterial.bind(cmd_buffer, glm::vec2(8.0f));

		customMesh.draw(cmd_buffer);

		vkCmdEndRendering(cmd_buffer);

		// Present Barrier
		VulkanImageMemoryBarrier(
			cmd_buffer,
			vk_context.vk_swapchain_images[ImageIndex],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			VK_ACCESS_2_NONE,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1 , 0 , 1 }
		);

		vkEndCommandBuffer(cmd_buffer);

		VkSemaphoreSubmitInfo imageAvlSemaphoreSubmitInfo{};
		imageAvlSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		imageAvlSemaphoreSubmitInfo.pNext = nullptr;
		imageAvlSemaphoreSubmitInfo.semaphore = vk_context.vk_image_avaliable_semaphores[currentIndex];
		imageAvlSemaphoreSubmitInfo.value = 0;
		imageAvlSemaphoreSubmitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSemaphoreSubmitInfo timelineSemaphoreSubmitInfo{};
		timelineSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		timelineSemaphoreSubmitInfo.pNext = nullptr;
		timelineSemaphoreSubmitInfo.semaphore = vk_context.vk_timeline_semaphore;
		timelineSemaphoreSubmitInfo.value = vk_context.timeline_values[currentIndex];
		timelineSemaphoreSubmitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		VkSemaphoreSubmitInfo renderFinishedSemaphoreSubmitInfo{};
		renderFinishedSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		renderFinishedSemaphoreSubmitInfo.pNext = nullptr;
		renderFinishedSemaphoreSubmitInfo.semaphore = vk_context.vk_render_finished_semaphores[currentIndex];
		renderFinishedSemaphoreSubmitInfo.value = 0;
		timelineSemaphoreSubmitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSemaphoreSubmitInfo graphicsSemaphoreSubmitInfos[] = { renderFinishedSemaphoreSubmitInfo, timelineSemaphoreSubmitInfo };
		VkSemaphoreSubmitInfo waitSemaphoreSubmitInfos[] = { imageAvlSemaphoreSubmitInfo };

		VkCommandBufferSubmitInfo cmdBufferSubmitInfo{};
		cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		cmdBufferSubmitInfo.pNext = nullptr;

		cmdBufferSubmitInfo.commandBuffer = vk_context.vk_cmd_buffers[currentIndex];
		cmdBufferSubmitInfo.deviceMask = 0;

		VkCommandBufferSubmitInfo commandBufferSubmitInfos[] = { cmdBufferSubmitInfo };

		VkSubmitInfo2 submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submitInfo.pNext = nullptr;

		submitInfo.flags = 0;

		submitInfo.pWaitSemaphoreInfos = waitSemaphoreSubmitInfos;
		submitInfo.waitSemaphoreInfoCount = 1;

		submitInfo.pSignalSemaphoreInfos = graphicsSemaphoreSubmitInfos;
		submitInfo.signalSemaphoreInfoCount = 2;

		submitInfo.pCommandBufferInfos = commandBufferSubmitInfos;
		submitInfo.commandBufferInfoCount = 1;

		if (vkQueueSubmit2(vk_context.vk_graphics_queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("Failed to submit to Graphics Queue.");
		}

		VkSemaphore presentSemaphores[]{ vk_context.vk_render_finished_semaphores[currentIndex] };

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = nullptr;

		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = presentSemaphores;

		VkSwapchainKHR swapchains[] = { vk_context.vkb_swapchain.swapchain };

		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;

		presentInfo.pImageIndices = &ImageIndex;

		presentInfo.pResults = nullptr;

		VkResult presentResult = vkQueuePresentKHR(vk_context.vk_present_queue, &presentInfo);

		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
			VulkanCreateSwapchainAndImages();
		}
		else if (presentResult != VK_SUCCESS) {
			throw std::runtime_error("Failed to present to the Present Queue.");
		}

		vk_context.current_frame = vk_context.current_frame + 1;

		Framecounter++;

		if (glfwGetTime() - lastTime > 1.0f) {
			std::string newTitle = "Vulkan Window | FPS: " + std::to_string(Framecounter);
			glfwSetWindowTitle(glfw_context.window, newTitle.c_str());
			lastTime = glfwGetTime();
			Framecounter = 0;
		}

		glfwSwapBuffers(glfw_context.window);
	}
}

void cleanup() {

	vkDeviceWaitIdle(vk_context.vkb_device.device);

	VulkanDestroyImageViews();

	vkDestroyPipelineLayout(vk_context.vkb_device.device, vk_context.vk_graphics_pipeline_layout, nullptr);
	vkDestroyPipeline(vk_context.vkb_device.device, vk_context.vk_graphics_pipeline, nullptr);

	camera_uniform_buffer.free();

	defaultTexture.free();
	sillyTexture.free();
	grassTexture.free();

	defaultMaterial.free();
	sillyMaterial.free();
	grassMaterial.free();

	vkDestroyDescriptorPool(vk_context.vkb_device.device, vk_context.descriptor_pool, nullptr);

	vkDestroyDescriptorSetLayout(vk_context.vkb_device.device, vk_context.global_vertex_descriptor_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(vk_context.vkb_device.device, vk_context.global_fragment_descriptor_set_layout, nullptr);

	customMesh.free();
	cube.free();
	ground.free();

	vmaDestroyImage(vma_allocator, vk_context.vk_depth_buffer_image, vk_context.vma_depth_buffer_allocation);
	vkDestroyImageView(vk_context.vkb_device.device, vk_context.vk_depth_buffer_image_view, nullptr);

	vmaDestroyImage(vma_allocator, vk_context.vk_msaa_image, vk_context.vma_msaa_allocation);
	vkDestroyImageView(vk_context.vkb_device.device, vk_context.vk_msaa_image_view, nullptr);

	vmaDestroyAllocator(vma_allocator);

	for (int i = 0; i < vk_context.MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(vk_context.vkb_device.device, vk_context.vk_image_avaliable_semaphores[i], nullptr);
		vkDestroySemaphore(vk_context.vkb_device.device, vk_context.vk_render_finished_semaphores[i], nullptr);
		vkDestroyCommandPool(vk_context.vkb_device.device, vk_context.vk_cmd_pools[i], nullptr);
	}

	vkDestroySemaphore(vk_context.vkb_device.device, vk_context.vk_timeline_semaphore, nullptr);
	vk_context.vk_timeline_semaphore = VK_NULL_HANDLE;

	vk_context.vk_image_avaliable_semaphores.clear();
	vk_context.vk_render_finished_semaphores.clear();
	vk_context.vk_cmd_pools.clear();

	vkb::destroy_swapchain(vk_context.vkb_swapchain);
	vkb::destroy_device(vk_context.vkb_device);
	vkb::destroy_surface(vk_context.vkb_instance, vk_context.vk_surface);
	vkb::destroy_instance(vk_context.vkb_instance);

	glfwDestroyWindow(glfw_context.window);
	glfwTerminate();
}

int main() {

	initGLFW();
	initVolk();
	initVulkan();

	stbi_set_flip_vertically_on_load(true);

	camera.FOV = 60.0f;
	camera.position = glm::vec3(0.0f, 0.0f, 2.0f);

	cube.vertices = cube_vertices;
	cube.indices = cube_indices;
	cube.update();

	ground.vertices = plane_vertices;
	ground.indices = plane_indices;
	ground.update();

	LoadOBJMesh(customMesh, "models/utah_teapot.obj");

	VulkanCreateDescriptorSet(globalVertexSet, vk_context.global_vertex_descriptor_set_layout);

	CreateTexture(defaultTexture, "textures/default.png", 1.0f, VK_FILTER_LINEAR);
	CreateTexture(sillyTexture, "textures/konata.jpg", 4.0f, VK_FILTER_LINEAR);
	CreateTexture(grassTexture, "textures/grass.png", 8.0f, VK_FILTER_LINEAR);

	defaultMaterial.create(defaultTexture);
	sillyMaterial.create(sillyTexture);
	grassMaterial.create(grassTexture);

	CreateUniformBuffer(camera_uniform_buffer, sizeof(glm::mat4), globalVertexSet,0);

	mainLoop();

	cleanup();

	return 0;
}