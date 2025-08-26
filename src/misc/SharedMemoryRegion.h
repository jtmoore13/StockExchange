#pragma once
#include <string>


class SharedMemoryRegion
{
public:
    SharedMemoryRegion(size_t size, const std::string& name, bool isManager);
    ~SharedMemoryRegion();

    void* Get() const;
    void SetShouldUnmap(bool shouldUnmap);

private:
    void Initialize_();

    std::string name_;
    const size_t size_ = 0;
    const bool isManager_ = false;

    int shmFd_ = -1;
    void* shmPtr_ = nullptr;

    bool shouldUnlink_ = false;
};
