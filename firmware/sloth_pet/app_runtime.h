#pragma once
#include <stddef.h>
#include <stdint.h>
namespace sloth {
enum class WorkspaceOwner : uint8_t { Ui, Browser, Remote };
// Main-loop ownership only. No app can steal a workspace from another app;
// asynchronous workers must drain before their owner releases its lease.
class AppWorkspace {
 public:
  bool bind(void* memory,size_t bytes) {
    if(memory_||!memory||!bytes)return false;memory_=memory;bytes_=bytes;return true;
  }
  bool claim(WorkspaceOwner next) {
    if(!memory_||next==WorkspaceOwner::Ui||owner_!=WorkspaceOwner::Ui)return false;
    owner_=next;return true;
  }
  bool release(WorkspaceOwner previous) {
    if(previous==WorkspaceOwner::Ui||owner_!=previous)return false;
    owner_=WorkspaceOwner::Ui;return true;
  }
  WorkspaceOwner owner() const {return owner_;}
  size_t bytes() const {return bytes_;}
 private:
  void* memory_=nullptr;size_t bytes_=0;WorkspaceOwner owner_=WorkspaceOwner::Ui;
};
// Expensive, nonurgent work must not stall a foreground network session.
inline bool backgroundMaintenanceAllowed(bool remote,bool browser,bool storage) {
  return !remote&&!browser&&!storage;
}
}
