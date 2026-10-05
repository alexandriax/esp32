#include "../firmware/sloth_pet/app_runtime.h"
#include "../firmware/sloth_pet/pet_timeline.h"
#include "../firmware/sloth_pet/pet_state.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
int main(){
 using namespace sloth;AppWorkspace workspace;char memory[32];
 assert(!workspace.claim(WorkspaceOwner::Browser));assert(workspace.bind(memory,sizeof(memory)));
 assert(workspace.claim(WorkspaceOwner::Browser));assert(!workspace.claim(WorkspaceOwner::Remote));
 assert(!workspace.release(WorkspaceOwner::Remote));assert(workspace.owner()==WorkspaceOwner::Browser);
 assert(workspace.release(WorkspaceOwner::Browser));assert(workspace.claim(WorkspaceOwner::Remote));
 assert(!backgroundMaintenanceAllowed(true,false,false));assert(!backgroundMaintenanceAllowed(false,true,false));
 assert(!backgroundMaintenanceAllowed(false,false,true));assert(backgroundMaintenanceAllowed(false,false,false));
 assert(workspace.release(WorkspaceOwner::Remote));
 for(bool sleeping:{false,true}){
  PetState active,paused;PetTimeline continuous,deferred;
  if(sleeping){active.act(Action::Nap);paused.act(Action::Nap);}
  const uint32_t epoch=1790899200;const uint32_t start=UINT32_MAX-900;
  continuous.begin(start,epoch,epoch);deferred.begin(start,epoch,epoch);
  for(uint32_t t=1;t<=7200;++t)active.advance(continuous.advance(start+t*1000));
  paused.advance(deferred.advance(start+7200000));
  const auto a=active.snapshot(),b=paused.snapshot();assert(!memcmp(&a,&b,sizeof(a)));
  assert(continuous.savedUtc()==deferred.savedUtc());assert(!deferred.advance(start+7200000));
 }
 puts("App workspace: exclusive leases, foreground scheduling and two-hour pet catch-up across rollover passed");
}
