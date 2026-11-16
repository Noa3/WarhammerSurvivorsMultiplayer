// Exercise the unchanged production pulse callback with fault-scripted engine
// boundaries. This proves orchestration only, not Unity/Rewired implementation.
#include "../../native/src/rewired_control_pulse.cpp"
#include <iostream>
#include <stdexcept>

namespace {
float current_x{},current_y{};
bool throw_reader{},fail_write{},fail_close{},attached=true;
unsigned writes{},closes{};
void reset(){current_x=current_y=0;throw_reader=fail_write=fail_close=false;attached=true;writes=closes=0;}
}
namespace wsm {
struct PlayerStateReader::Impl{};
PlayerStateReader::PlayerStateReader()=default;
PlayerStateReader::~PlayerStateReader()=default;
PlayerReadStatus PlayerStateReader::read(SceneStateSnapshot& result,std::string&) const {
    if(throw_reader)throw std::runtime_error("scripted reader exception");
    result.players.resize(2);result.players[0].input_id=0;result.players[1].input_id=1;
    return PlayerReadStatus::Captured;
}
struct RewiredControllerSession::Impl{};
RewiredControllerSession::RewiredControllerSession()=default;
RewiredControllerSession::~RewiredControllerSession()=default;
bool RewiredControllerSession::set_axes(float x,float y,std::string& error){
    ++writes;current_x=x;
    if(fail_write){current_y=0.5f;error="scripted partial setter failure";return false;}
    current_y=y;return true;
}
bool RewiredControllerSession::close(std::string& error){
    ++closes;if(fail_close){error="scripted cleanup failure";return false;}
    current_x=current_y=0;attached=false;return true;
}
RewiredOwnedControlState RewiredControllerSession::state() const {
    RewiredOwnedControlState result;result.player_id=1;result.controller_created=result.controller_attached=result.map_registered=result.cleanup_required=attached;
    result.horizontal=current_x;result.vertical=current_y;return result;
}
}
int main(){
    using namespace wsm;int failed=0,count=0;
    const auto check=[&](const char* name,bool ok){++count;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
    RewiredControllerSession controller;PlayerStateReader reader;std::string error;
    reset();controller.set_axes(0.75f,-0.25f,error);
    Pulse read_fault{GetForegroundWindow(),controller,reader};read_fault.stopping=true;read_fault.result.nonzero_written=true;throw_reader=true;
    for(int i=0;i<3;++i)pulse_tick(&read_fault);
    check("reader exception cannot block explicit zero",current_x==0&&current_y==0&&read_fault.result.neutralized);
    reset();controller.set_axes(0.75f,-0.25f,error);
    Pulse both_faults{GetForegroundWindow(),controller,reader};both_faults.stopping=true;both_faults.result.nonzero_written=true;throw_reader=fail_write=true;
    for(int i=0;i<3;++i)pulse_tick(&both_faults);
    check("reader and zero failure still invoke owned removal",closes>0&&!attached&&both_faults.result.resources_closed&&both_faults.result.neutralized);
    reset();Pulse later_fault{GetForegroundWindow(),controller,reader};later_fault.stopping=true;
    pulse_tick(&later_fault);check("ordinary zero positively confirmed",later_fault.result.neutralized&&current_x==0&&current_y==0);
    fail_write=fail_close=true;pulse_tick(&later_fault);
    check("later partial zero failure invalidates prior neutral proof",!later_fault.result.neutralized&&current_y!=0&&!later_fault.result.resources_closed);
    reset();Pulse fallback{GetForegroundWindow(),controller,reader};fallback.stopping=true;fail_write=true;
    pulse_tick(&fallback);check("zero setter failure has independent removal fallback",fallback.result.neutralized&&fallback.result.resources_closed&&!attached);
    reset();Pulse observation{GetForegroundWindow(),controller,reader};observation.stopping=true;
    neutralize_tick(&observation);const auto prior_writes=writes;
    fail_write=fail_close=true;observe_tick(&observation);
    check("post-neutral observation performs no axis writes",writes==prior_writes&&closes==0&&observation.result.neutralized&&current_x==0&&current_y==0);
    throw_reader=true;observe_tick(&observation);
    check("read-only observation failure is distinct from zero proof",observation.failed&&observation.result.neutralized&&writes==prior_writes);
    std::cout<<"Pulse fault checks: "<<count-failed<<" passed, "<<failed<<" failed, "<<count<<" total\n";return failed?1:0;
}
