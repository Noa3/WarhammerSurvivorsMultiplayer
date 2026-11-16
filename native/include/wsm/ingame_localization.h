#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace wsm::i18n {

enum class Text {
    Tab, Title, Address, Port, Host, Join, Leave, Notice, Ready, NotConnected,
    LeaveUnavailable, HostUnavailable, JoinUnavailable, InvalidPort, MissingAddress,
    ActionRejected, NetworkMissing, NetworkUnavailable, HostingLan, HostingLocal,
    Connecting, Connected, HostLost, Refused, Timeout, Rejected, HostFull,
    Unreachable, PortInUse, Busy, Invalid, Internal, UiError,
    CameraOwn, CameraShared, CameraInactive, CameraActive, CameraSpectating, CameraUnavailable, Count
};

std::string normalize_language(std::string_view code);
std::string_view text(Text id, std::string_view language);
std::string format(Text id, std::string_view language,
                   std::initializer_list<std::pair<std::string_view, std::string>> values);

} // namespace wsm::i18n
