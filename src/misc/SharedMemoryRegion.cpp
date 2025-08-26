#include "SharedMemoryRegion.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <system_error>


SharedMemoryRegion::SharedMemoryRegion(size_t size, const std::string& name, bool isManager) :
    name_(name), 
    size_(size),
    isManager_(isManager)
{ 
    Initialize_();
}


SharedMemoryRegion::~SharedMemoryRegion() 
{ 
    if (shmPtr_) {
        munmap(shmPtr_, size_); // unmap region from process's address space
    }
    if (shmFd_ != -1) {
        close(shmFd_);
    }
     if (shouldUnlink_) {
        shm_unlink(name_.c_str());
    }
}


void* SharedMemoryRegion::Get() const 
{ 
    return shmPtr_; 
}


void SharedMemoryRegion::SetShouldUnmap(bool shouldUnmap)
{
    if (!isManager_) {
        return; // only the manager can shutdown
    }
    shouldUnlink_ = true;
}


void SharedMemoryRegion::Initialize_()
{
    // create a shared memory obj and return a handler (fd) to it
    const int flags = isManager_ ? (O_CREAT | O_RDWR) : O_RDWR;
    shmFd_ = shm_open(name_.c_str(), flags, 0666); // 0666 is read/write perms for everyone
    if (shmFd_ == -1) {
        /*
            If shm_open() fails here, it likely means the ExchangeServer hasn't started yet.
            The ExchangeServer is the "manager" of this shared memory, meaning it is the one
            responsible for creating the shared memory region using O_CREAT. 
            TLDR: the Exchange shouldn't run before the server.
        */
        throw std::system_error(errno, std::generic_category(), "shm_open failed");
    }

    // expand the size
    if (isManager_ && ftruncate(shmFd_, size_) == -1) {
        throw std::system_error(errno, std::generic_category(), "ftruncate failed");
    }

    // map the physical region of shared memory to the calling process's virtual address space
    shmPtr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    if (shmPtr_ == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "mmap failed");
    }
}