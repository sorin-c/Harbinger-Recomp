#include "world/portal.hpp"

namespace harbinger::world {

std::optional<PortalLink> portalLink(const giz::Template& t, const chk::GizmoRecord& g) {
    if (t.cls != giz::Portal || g.extra.size() < 14) return std::nullopt;
    auto word = [&](size_t at) { return uint16_t(g.extra[at] | g.extra[at + 1] << 8); };
    return PortalLink{word(2), word(4), word(6), word(10), word(12)};
}

PortalStep portalStep(const PortalLink& link, uint8_t& phase, uint16_t trigger) {
    if (trigger == link.inner) {
        if (phase == 1) {
            phase = 0;
            return PortalStep::Leave;
        }
        phase = 2;
        return PortalStep::WalkOut;
    }
    if (trigger != link.outer) return PortalStep::None;
    if (phase == 2) {
        phase = 0;
        return PortalStep::WalkedOut;
    }
    phase = 1;
    return PortalStep::WalkIn;
}

}  // namespace harbinger::world
