#pragma once
#include <stddef.h>
#include <stdint.h>
// One browser cache session; filenames are restricted to 8.3 names in MOSSWEB.
// Mount never formats. File operations serialize with LCD DMA. No public paths
// or URLs are passed to FatFS. The browser drains its worker before unmounting.
namespace storage_files {
struct File;
enum class Mode {Read,Create};
bool mount();
void unmount();
File* open(const char* name,Mode mode);
size_t read(File*,void*,size_t);
bool write(File*,const void*,size_t);
bool seek(File*,uint32_t);
uint32_t size(File*);
bool close(File*&);
bool remove(const char*);
bool rename(const char*,const char*);
}
