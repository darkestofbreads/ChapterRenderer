#include "Raytracing.h"
#include "Renderer.h"

vk::AccelerationStructureKHR Renderer::BLASFromMesh(const uint32_t meshVerticesCount, const uint32_t primitiveCount, const uint32_t firstIndex, const uint32_t firstVertex) {
    // Prepare the geometry data
    const auto trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR()
        .setIndexData(indicesBuffer.address + firstIndex * sizeof(uint32_t))
        .setIndexType(vk::IndexType::eUint32)
        .setMaxVertex(meshVerticesCount)
        .setVertexData(bufferAddresses.vertexBufferAddress)
        .setVertexFormat(vk::Format::eR32G32B32Sfloat)
        .setVertexStride(sizeof(Vertex));

    const vk::AccelerationStructureGeometryDataKHR geometryData(trianglesData);
    const auto blasGeometry = vk::AccelerationStructureGeometryKHR()
        .setGeometryType(vk::GeometryTypeKHR::eTriangles)
        .setGeometry(geometryData)
        .setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    auto blasBuildGeometryInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
        .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
        .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
        .setGeometries(blasGeometry);

    const auto blasBuildSizes = device.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, blasBuildGeometryInfo, primitiveCount, dldid);
    const auto blasCreateInfo = vk::AccelerationStructureCreateInfoKHR()
        .setOffset(0)
        .setSize(blasBuildSizes.accelerationStructureSize)
        //.setBuffer(blasBuffers[i])
        .setType(vk::AccelerationStructureTypeKHR::eBottomLevel);

    auto BLAS = device.device.createAccelerationStructureKHR(blasCreateInfo, nullptr, dldid);
    blasBuildGeometryInfo.setDstAccelerationStructure(BLAS);

    const auto blasRangeInfo = vk::AccelerationStructureBuildRangeInfoKHR()
        .setPrimitiveCount(primitiveCount)
        .setPrimitiveOffset(0)
        .setFirstVertex(firstVertex)
        .setTransformOffset(0);

    const std::function func = [&] { graphCompCmdBuffers[0].buildAccelerationStructuresKHR(blasBuildGeometryInfo, &blasRangeInfo, dldid); };
    SubmitImmediate(func);

    return BLAS;
}

vk::AccelerationStructureKHR TLASFromBLAS(vk::AccelerationStructureKHR BLAS, const vk::Device device, const uint32_t primitiveCount, const vk::detail::DispatchLoaderDynamic& dldid) {
    const auto addrInfo = vk::AccelerationStructureDeviceAddressInfoKHR();
        //.setAccelerationStructure(*blasHandles[i]);
    const auto blasDeviceAddr = device.getAccelerationStructureAddressKHR(addrInfo, dldid);

    const auto instance = vk::AccelerationStructureInstanceKHR()
        .setMask(0xFF)
        //.setTransform(nullptr)
        .setAccelerationStructureReference(blasDeviceAddr);
    //instances.push_back(instance);

    auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR()
        //.setData = instanceAddr()
        .setArrayOfPointers(vk::False);
    vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

    const auto tlasGeometry = vk::AccelerationStructureGeometryKHR()
        .setGeometryType(vk::GeometryTypeKHR::eInstances)
        .setGeometry(geometryData);

    auto tlasBuildGeometryInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
        .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
        .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
        .setGeometries(tlasGeometry);

    const auto tlasBuildSizes = device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, tlasBuildGeometryInfo, primitiveCount, dldid);

    const auto tlasCreateInfo = vk::AccelerationStructureCreateInfoKHR()
        //.setBuffer(tlasBuffer)
        .setOffset(0)
        .setSize(tlasBuildSizes.accelerationStructureSize)
        .setType(vk::AccelerationStructureTypeKHR::eTopLevel);

    const auto tlas = device.createAccelerationStructureKHR(tlasCreateInfo, nullptr, dldid);
    tlasBuildGeometryInfo.setDstAccelerationStructure(tlas);

    const auto tlasRangeInfo = vk::AccelerationStructureBuildRangeInfoKHR()
        .setPrimitiveCount(primitiveCount)
        .setPrimitiveOffset(0)
        .setFirstVertex(0)
        .setTransformOffset(0);

    //auto cmd = beginSingleTimeCommands();
    //cmd->buildAccelerationStructuresKHR({ tlasBuildGeometryInfo }, { &tlasRangeInfo });
    //endSingleTimeCommands(*cmd);

    return tlas;
}