#include "Raytracing.h"

void Renderer::BuildSubMeshBLAS(const uint32_t meshVerticesCount, const uint32_t primitiveCount, const uint32_t firstIndex, const uint32_t firstVertex) {
    const auto identity = vk::TransformMatrixKHR()
        .setMatrix(std::array<std::array<float, 4>, 3>{
            {std::array{1.f, 0.f, 0.f, 0.f},
                std::array{0.f, 1.f, 0.f, 0.f},
                std::array{0.f, 0.f, 1.f, 0.f}}});

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

    const auto scratch = CreateAllocatedBuffer(device.device, allocator, blasBuildSizes.buildScratchSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_AUTO);
    blasBuildGeometryInfo.setScratchData(scratch.address);

    AccelerationStruct as;
    as.buffer = CreateAllocatedBuffer(device.device, allocator, blasBuildSizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            VMA_MEMORY_USAGE_AUTO);

    const auto blasCreateInfo = vk::AccelerationStructureCreateInfoKHR()
        .setOffset(0)
        .setSize(blasBuildSizes.accelerationStructureSize)
        .setBuffer(as.buffer.buffer)
        .setType(vk::AccelerationStructureTypeKHR::eBottomLevel);

    auto blasHandle = device.device.createAccelerationStructureKHR(blasCreateInfo, nullptr, dldid);
    blasBuildGeometryInfo.setDstAccelerationStructure(blasHandle);
    as.handle = blasHandle;

    const auto blasRangeInfo = vk::AccelerationStructureBuildRangeInfoKHR()
        .setPrimitiveCount(primitiveCount)
        .setPrimitiveOffset(0)
        .setFirstVertex(firstVertex)
        .setTransformOffset(0);

    const std::function func = [&] { graphCompCmdBuffers[0].buildAccelerationStructuresKHR(blasBuildGeometryInfo, &blasRangeInfo, dldid); };
    SubmitImmediate(func);

    const auto blasInstance = vk::AccelerationStructureInstanceKHR()
        .setAccelerationStructureReference(as.buffer.address)
        .setMask(0xFF)
        .setTransform(identity);
    as.instance = blasInstance;

    BLAccelerationStructs.emplace_back(as);
}
void Renderer::BuildTLAS(const uint32_t primitiveCount) {
    auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR()
        .setData(instanceAddr())
        .setArrayOfPointers(vk::False);
    vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

    const auto tlasGeometry = vk::AccelerationStructureGeometryKHR()
        .setGeometryType(vk::GeometryTypeKHR::eInstances)
        .setGeometry(geometryData);

    auto tlasBuildGeometryInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
        .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
        .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
        .setGeometries(tlasGeometry);

    const auto tlasBuildSizes = device.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, tlasBuildGeometryInfo, primitiveCount, dldid);

    const auto tlasCreateInfo = vk::AccelerationStructureCreateInfoKHR()
        .setBuffer(TLASBuffer.buffer)
        .setOffset(0)
        .setSize(tlasBuildSizes.accelerationStructureSize)
        .setType(vk::AccelerationStructureTypeKHR::eTopLevel);

    const auto tlas = device.device.createAccelerationStructureKHR(tlasCreateInfo, nullptr, dldid);
    tlasBuildGeometryInfo.setDstAccelerationStructure(tlas);

    const auto tlasRangeInfo = vk::AccelerationStructureBuildRangeInfoKHR()
        .setPrimitiveCount(primitiveCount)
        .setPrimitiveOffset(0)
        .setFirstVertex(0)
        .setTransformOffset(0);

    const std::function func = [&] { graphCompCmdBuffers[0].buildAccelerationStructuresKHR(tlasBuildGeometryInfo, &tlasRangeInfo, dldid); };
    SubmitImmediate(func);

    return tlas;
}