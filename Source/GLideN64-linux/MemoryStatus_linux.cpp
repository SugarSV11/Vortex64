// windows/MemoryStatus_windows.cpp for Linux: is the memory range mapped (the frame buffer copies
// to RDRAM check that the range they write lies in memory before they write it). mincore() fails
// with ENOMEM for a page that is not mapped; the RDRAM the emulator hands over is writable.
#include <MemoryStatus.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

bool isMemoryWritable(void * ptr, size_t byteCount)
{
	if (ptr == nullptr)
		return false;
	if (byteCount == 0)
		return true;
	const uintptr_t page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
	const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr) & ~(page - 1);
	const uintptr_t end = (reinterpret_cast<uintptr_t>(ptr) + byteCount + page - 1) & ~(page - 1);
	if (end < begin)
		return false;
	std::vector<unsigned char> vec((end - begin) / page);
	return mincore(reinterpret_cast<void *>(begin), end - begin, vec.data()) == 0;
}
