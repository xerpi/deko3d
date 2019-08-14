#include "dk_memblock.h"
#include "dk_device.h"

DkResult tag_DkMemBlock::initialize(uint32_t flags, void* storage, uint32_t size)
{
	uint32_t cpuAccess = (flags >> DkMemBlockFlags_CpuAccessShift) & DkMemAccess_Mask;
	uint32_t gpuAccess = (flags >> DkMemBlockFlags_GpuAccessShift) & DkMemAccess_Mask;
	flags &= ~(DkMemBlockFlags_CpuAccessMask | DkMemBlockFlags_GpuAccessMask);

	if (flags & DkMemBlockFlags_Image)
		return DkResult_NotImplemented;

	flags |= cpuAccess << DkMemBlockFlags_CpuAccessShift;
	flags |= gpuAccess << DkMemBlockFlags_GpuAccessShift;
	m_flags = flags;

	if (!storage)
	{
		m_ownedMem = allocMem(size, DK_MEMBLOCK_ALIGNMENT);
		if (!m_ownedMem)
			return DkResult_OutOfMemory;
		storage = m_ownedMem;
	}

	uint32_t bigPageSize = getDevice()->getGpuInfo().bigPageSize;
	if (R_FAILED(nvMapCreate(&m_mapObj, storage, size, bigPageSize, NvKind_Pitch, isCpuCached())))
		return DkResult_Fail;

	if (!isGpuNoAccess())
	{
		// Create pitch mapping
		if (!isCode())
		{
			// For non-code memory blocks, we can just let the system place it automatically.
			if (R_FAILED(nvAddressSpaceMap(getDevice()->getAddrSpace(),
				getHandle(), isGpuCached(), NvKind_Pitch, &m_gpuAddrPitch)))
				return DkResult_Fail;
		}
		else
		{
			auto& codeSeg = getDevice()->getCodeSeg();

			// Reserve a suitable chunk of address space in the code segment
			if (!codeSeg.allocSpace(size, m_gpuAddrPitch))
				return DkResult_Fail;

			// Create a fixed mapping on said chunk
			if (R_FAILED(nvAddressSpaceMapFixed(getDevice()->getAddrSpace(),
				getHandle(), isGpuCached(), NvKind_Pitch, m_gpuAddrPitch)))
			{
				codeSeg.freeSpace(m_gpuAddrPitch, size);
				m_gpuAddrPitch = DK_GPU_ADDR_INVALID;
				return DkResult_Fail;
			}

			// Retrieve the code segment offset
			m_codeSegOffset = codeSeg.calcOffset(m_gpuAddrPitch);
		}

		// TODO: create swizzled/compressed mappings for DkMemBlockFlags_Image
	}

	return DkResult_Success;
}

void tag_DkMemBlock::destroy()
{
	if (m_gpuAddrPitch != DK_GPU_ADDR_INVALID)
	{
		nvAddressSpaceUnmap(getDevice()->getAddrSpace(), m_gpuAddrPitch);
		if (isCode())
			getDevice()->getCodeSeg().freeSpace(m_gpuAddrPitch, getSize());
		m_gpuAddrPitch = DK_GPU_ADDR_INVALID;
	}

	nvMapClose(&m_mapObj); // does nothing if uninitialized
	if (m_ownedMem)
	{
		freeMem(m_ownedMem);
		m_ownedMem = nullptr;
	}
}

DkMemBlock dkMemBlockCreate(DkMemBlockMaker const* maker)
{
	DkMemBlock obj = nullptr;
#ifdef DEBUG
	if (maker->size & (DK_MEMBLOCK_ALIGNMENT-1))
		DkObjBase::raiseError(maker->device, DK_FUNC_ERROR_CONTEXT, DkResult_MisalignedSize);
	else if (uintptr_t(maker->storage) & (DK_MEMBLOCK_ALIGNMENT-1))
		DkObjBase::raiseError(maker->device, DK_FUNC_ERROR_CONTEXT, DkResult_MisalignedData);
	else
#endif
	obj = new(maker->device) tag_DkMemBlock(maker->device);
	if (obj)
	{
		DkResult res = obj->initialize(maker->flags, maker->storage, maker->size);
		if (res != DkResult_Success)
		{
			delete obj;
			obj = nullptr;
			DkObjBase::raiseError(maker->device, DK_FUNC_ERROR_CONTEXT, res);
		}
	}
	return obj;
}

void dkMemBlockDestroy(DkMemBlock obj)
{
	delete obj;
}

void* dkMemBlockGetCpuAddr(DkMemBlock obj)
{
	return obj->getCpuAddr();
}

DkGpuAddr dkMemBlockGetGpuAddr(DkMemBlock obj)
{
	return obj->getGpuAddrPitch();
}

uint32_t dkMemBlockGetSize(DkMemBlock obj)
{
	return obj->getSize();
}

DkResult dkMemBlockFlushCpuCache(DkMemBlock obj, uint32_t offset, uint32_t size)
{
	if (!obj->isCpuCached())
		return DkResult_Success;
	return DkResult_NotImplemented;
}

DkResult dkMemBlockInvalidateCpuCache(DkMemBlock obj, uint32_t offset, uint32_t size)
{
	if (!obj->isCpuCached())
		return DkResult_Success;
	return DkResult_NotImplemented;
}
