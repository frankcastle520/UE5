# Use the generated header in the "debug" folder because we want the option to print specific errors
with open(r'./debug/vulkan_profiles.hpp', 'r') as f:
    file_data = f.read()

    # Add pragma used everywhere to skip about casting Vulkan function pointers
    insert_index = file_data.find("#ifndef VULKAN_PROFILES_HPP_")
    data = file_data[:insert_index] + "#pragma warning(push)\n#pragma warning(disable : 4191) // warning C4191: 'type cast': unsafe conversion\n\n" + file_data[insert_index:] + "\n#pragma warning(pop) // restore 4191\n"
    
    # Add our namespace in front of functions assumed by the generation scripts
    data = data.replace("vkGetInstanceProcAddr(","VulkanRHI::vkGetInstanceProcAddr(")
    data = data.replace("vkEnumerateInstanceExtensionProperties(","VulkanRHI::vkEnumerateInstanceExtensionProperties(")
    data = data.replace("vkCreateInstance(","VulkanRHI::vkCreateInstance(")
    data = data.replace("vkEnumerateDeviceExtensionProperties(","VulkanRHI::vkEnumerateDeviceExtensionProperties(")
    data = data.replace("vkGetPhysicalDeviceProperties(","VulkanRHI::vkGetPhysicalDeviceProperties(")
    data = data.replace("vkCreateDevice(","VulkanRHI::vkCreateDevice(")
    
    # Deal with ImportVulkanFunctions_Static compilation
    data = data.replace("(PFN_vkGetInstanceProcAddr)vkGetInstanceProcAddr;","(PFN_vkGetInstanceProcAddr)VulkanRHI::vkGetInstanceProcAddr;")
    data = data.replace("(PFN_vkGetDeviceProcAddr)vkGetDeviceProcAddr;","(PFN_vkGetDeviceProcAddr)VulkanRHI::vkGetDeviceProcAddr;")
    data = data.replace("(PFN_vkEnumerateInstanceVersion)vkEnumerateInstanceVersion;","(PFN_vkEnumerateInstanceVersion)nullptr; // not used")
    data = data.replace("(PFN_vkEnumerateInstanceExtensionProperties)vkEnumerateInstanceExtensionProperties;","(PFN_vkEnumerateInstanceExtensionProperties)VulkanRHI::vkEnumerateInstanceExtensionProperties;")
    data = data.replace("(PFN_vkEnumerateDeviceExtensionProperties)vkEnumerateDeviceExtensionProperties;","(PFN_vkEnumerateDeviceExtensionProperties)VulkanRHI::vkEnumerateDeviceExtensionProperties;")
    data = data.replace("(PFN_vkGetPhysicalDeviceFeatures2)vkGetPhysicalDeviceFeatures2;","(PFN_vkGetPhysicalDeviceFeatures2)VulkanRHI::vkGetPhysicalDeviceFeatures2;")
    data = data.replace("(PFN_vkGetPhysicalDeviceProperties2)vkGetPhysicalDeviceProperties2;","(PFN_vkGetPhysicalDeviceProperties2)VulkanRHI::vkGetPhysicalDeviceProperties2;")
    data = data.replace("(PFN_vkGetPhysicalDeviceFormatProperties2)vkGetPhysicalDeviceFormatProperties2;","(PFN_vkGetPhysicalDeviceFormatProperties2)VulkanRHI::vkGetPhysicalDeviceFormatProperties2;")
    data = data.replace("(PFN_vkGetPhysicalDeviceQueueFamilyProperties2)vkGetPhysicalDeviceQueueFamilyProperties2;","(PFN_vkGetPhysicalDeviceQueueFamilyProperties2)VulkanRHI::vkGetPhysicalDeviceQueueFamilyProperties2;")
    data = data.replace("(PFN_vkCreateInstance)vkCreateInstance;","(PFN_vkCreateInstance)VulkanRHI::vkCreateInstance;")
    data = data.replace("(PFN_vkCreateDevice)vkCreateDevice;","(PFN_vkCreateDevice)VulkanRHI::vkCreateDevice;")

    # Fix a build error !?
    data = data.replace("}#pragma warning(push)"," }\n#pragma warning(push)")
    
# Write final header with text namespaces added
with open(r'./include/vulkan_profiles_ue.h', 'w') as f:
    f.write(data)
