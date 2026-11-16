#include "wsm/upgrade_pass_hook.h"
#include <iostream>

int main() {
    int failures=0;
    const auto check=[&](const char* name,bool ok){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    wsm::UpgradePassHook hook;wsm::MainThreadGuard guard;wsm::PlayerStateReader reader;std::string error;
    check("install is unconditionally unavailable without quiescence",!hook.install(GetCurrentThreadId(),guard,reader,error)&&error=="Upgrade hook mutation disabled: no verified quiescent patch window");
    check("null module preparation refused",!hook.prepare(nullptr,error)&&!error.empty());
    check("unrelated own executable preparation refused",!hook.prepare(GetModuleHandleW(nullptr),error)&&!error.empty());
    auto state=hook.state();
    check("failed preparation has no patch ownership",!state.prepared&&!state.ownership_retained&&!state.last_verified_applied&&!state.protection_pending);
    check("remove is unconditionally unavailable without quiescence",!hook.remove(GetCurrentThreadId(),guard,reader,error)&&error=="Upgrade hook mutation disabled: no verified quiescent patch window");
    return failures?1:0;
}
