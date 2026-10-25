#include "world/foes.hpp"

#include <algorithm>
#include <cmath>

#include "formats/mgr.hpp"
#include "world/bolts.hpp"
#include "world/motion.hpp"

namespace harbinger::world {

namespace {

// The box's half-size: template+0x20 (slot 0x90) + 0.1 (0x54a308).
double halfSize(const agt::Template& t) { return t.d20 + 0.1; }

// The policies' action counts (GIAIPolicy_v1 of each: FUN_004bd0c0).
int stepCount(Foes::Policy p) {
    switch (p) {
        case Foes::Policy::Attack: return 2;     // move, attack
        case Foes::Policy::LongRange: return 2;  // move, attack
        case Foes::Policy::Wander: return 2;     // move, idle
        case Foes::Policy::Move: return 1;
        case Foes::Policy::Flee: return 1;       // GMovePolicy_v1's list
        case Foes::Policy::HardFlee: return 1;
        case Foes::Policy::Flank: return 3;      // blind move, blind turn, blind idle
        case Foes::Policy::Wait: return 2;       // blind turn, blind idle
        case Foes::Policy::Awake: return 1;      // its BlindMove (the Awake step is the spawner's)
        case Foes::Policy::None: return 0;
    }
    return 0;
}

int halfCell(double v) { return int(std::floor(v * 2)); }

}  // namespace

Foes::Foes(Vfs& vfs, const Ini& ini, Level& level, const WalkMap& walk, Rng& rng)
    : level_(level), walk_(walk), rng_(rng), sprites_(vfs), rooms_(level.map(), rng) {
    mgr::Index weapons;
    if (const auto data = vfs.read(ini.require("Paths", "Weapons"))) weapons = mgr::parse(*data);
    for (const auto& a : level.map().agents) {
        if (!level.agentVisible(a)) continue;
        const agt::Template& t = level.agentTemplate(a);
        if (!isEnemyClass(t.cls)) continue;
        Foe f;
        f.record = &a;
        f.maxHitPoints = f.hitPoints = std::max(1, rollHitPoints(t, rng));
        f.x = a.x, f.y = a.y;
        f.facing = a.facing;
        const auto idle = t.animsFor(agt::Idle);
        f.variant = idle.empty() ? 0 : a.instance % idle.size();  // as Level::agentPose
        for (const auto& an : t.anims)
            if (an.kind == agt::Move && !f.speed) f.speed = walkSpeed(an.param);
        const int size = t.stats.size() > 6 ? t.stats[6] : 1;
        f.limit = WalkMap::limitFor(std::clamp(size, 0, 2));
        // GAgentPlug_v100: an agent's rating is its level (stat 0) times 5.
        f.rating = t.stats.empty() ? 0 : t.stats[0] * 5;
        // Stat 4 names the melee weapon (FUN_004213a0 hands it to the blow).
        if (t.stats.size() > 4 && t.stats[4])
            if (const std::string* w = weapons.find(t.stats[4]))
                if (const auto data = vfs.read(*w)) {
                    f.weapon = wpn::parse(*data);
                    f.armed = true;
                }
        // Stat 3 names the ranged weapon (FUN_00406a60 keeps it at
        // template+0x40, which FUN_0041d4e0 hands to FUN_00526740); every
        // stat 3 the levels use names a weapon with a ranged tail.
        if (t.stats.size() > 3 && t.stats[3])
            if (const std::string* w = weapons.find(t.stats[3]))
                if (const auto data = vfs.read(*w)) {
                    wpn::Weapon r = wpn::parse(*data);
                    if (r.ranged) {
                        f.energy = r.ranged->maxEnergy;
                        f.ranged = std::move(r);
                    }
                }
        const auto shoot = t.animsFor(agt::Shoot);
        f.canShoot = f.ranged && !shoot.empty();
        f.canMelee = f.armed && !t.animsFor(agt::Melee).empty();
        // FUN_0041d4e0: the holder starts in ranged mode when it has a
        // ranged weapon.
        f.shoots = f.canShoot;
        if (!shoot.empty()) {
            const double range = shoot[0]->param / 90.51;
            f.range2 = range * range;
            f.attackRange2 = 0.25 * f.range2;
        }
        // FUN_00486b90: the melee animation's parameter / 90.51 is how far
        // past the target's radius a blow reaches (plug+0x230); 0 means the
        // agent's own radius + 0.1. (The original picks one of the state's
        // animations at random; the rebuild reads the first.)
        const auto melee = t.animsFor(agt::Melee);
        const double param = melee.empty() ? 0 : melee[0]->param / 90.51;
        f.reach = param == 0 ? t.d20 + 0.1 : param;
        // The thinkers' slot 2: GIThinker_v2 sets the three instincts to 0,
        // 0, 127; the animal, predator, trooper and mechanized ones
        // (0x517530) then set courage to 80 and restlessness to rand % 200.
        Mind& m = f.mind;
        m.thinker = t.thinker;
        m.emotion[2] = 127;
        if (m.thinker == 5 || m.thinker == 6 || m.thinker == 7 || m.thinker == 10) {
            m.emotion[0] = 80;
            m.emotion[1] = uint8_t(rng.next() % 200);
        } else if (m.thinker == 8 || m.thinker == 9) {
            // GBossThinker_v2 (commanders, bosses): both at 255.
            m.emotion[0] = m.emotion[1] = 255;
        }
        // GBugZapperPlug_v18 (record slot 18) fills the mind's roles from the
        // record's words; the mind plays role 0 (mind+0x68 stays 0: nothing
        // found sets it otherwise).
        if (!a.slots.empty()) m.role = a.slots[0];
        // GRoleMind_v1: role 1 rests in stasis (action 9), which never ends
        // and cannot be interrupted (GStasisAction_v9 -> slot 0x170, false).
        if (m.role == 1) m.action = Action::Stasis;
        foes_.push_back(f);
    }
}

const Foes::Foe* Foes::find(int id) const {
    return id >= 0 && size_t(id) < foes_.size() ? &foes_[size_t(id)] : nullptr;
}

int Foes::at(double x, double y) const {
    int best = -1;
    double bestD = 1e9;
    for (size_t i = 0; i < foes_.size(); ++i) {
        const Foe& f = foes_[i];
        if (f.dead || f.held) continue;
        const double h = halfSize(tmpl(f));
        const double dx = std::fabs(x - f.x), dy = std::fabs(y - f.y);
        if (dx > h || dy > h) continue;
        if (dx + dy < bestD) bestD = dx + dy, best = int(i);
    }
    return best;
}

int Foes::nearest(double x, double y, double range, std::optional<uint8_t> facing) const {
    int best = -1;
    double bestScore = 1e9;
    for (size_t i = 0; i < foes_.size(); ++i) {
        const Foe& f = foes_[i];
        if (f.dead || f.held) continue;
        const double dx = f.x - x, dy = f.y - y, d = std::hypot(dx, dy);
        if (d > range) continue;
        double score = d;
        if (facing && d > 1e-9) {
            // Up to twice as far for a foe straight ahead as for one behind.
            const double ahead = (dx * facingCos(*facing) + dy * facingSin(*facing)) / d;
            score = d * (1.5 - 0.5 * ahead);
        }
        if (score < bestScore) bestScore = score, best = int(i);
    }
    return best;
}

std::optional<Strike> Foes::blow(int id, double fromX, double fromY, double toX, double toY, int rating,
                                 const wpn::Weapon& weapon, uint64_t nowMs) {
    if (id < 0 || size_t(id) >= foes_.size()) return std::nullopt;
    Foe& f = foes_[size_t(id)];
    if (f.dead || f.held) return std::nullopt;
    const agt::Template& t = tmpl(f);
    if (!segmentTouchesBox(fromX, fromY, toX, toY, f.x, f.y, halfSize(t))) return std::nullopt;
    const Strike s = strike(rating, weapon, t, rng_, {wpn::Melee});
    hurt(id, s, nowMs);
    return s;
}

void Foes::hurt(int id, const Strike& s, uint64_t nowMs) {
    if (id < 0 || size_t(id) >= foes_.size()) return;
    Foe& f = foes_[size_t(id)];
    if (f.dead || f.held) return;
    if (s.hit) {
        f.hitPoints -= s.damage;
        // FUN_004a2590: damage reaching the hit points kills (GAmpEnemyPlug_v61
        // -> FUN_00420a00).
        if (f.hitPoints <= 0) {
            f.dead = true;
            f.diedAt = nowMs;
            f.path.clear();
            f.mind.policy = Policy::None;
        } else {
            // GRoleMind_v8 (being attacked): courage +60, and the attacker,
            // if hostile, is fought (slot 0x44).
            f.mind.emotion[0] = uint8_t(std::min(255, f.mind.emotion[0] + 60));
            f.mind.provoked = true;
        }
    }
}

std::vector<const anm::Animation*> Foes::anims(const Foe& f, agt::State s) const {
    const auto list = tmpl(f).animsFor(s);
    if (list.empty()) return {};
    const anm::Animation* a = sprites_.anim(list[f.variant % list.size()]->path);
    if (!a) return {};
    return {a};
}

void Foes::setState(Foe& f, agt::State s, uint64_t nowMs) {
    if (s == f.state) return;
    f.state = s;
    f.stateStart = nowMs;
    f.loops = 0;
    // FUN_00407950 picks one of the state's animations at random.
    const size_t n = tmpl(f).animsFor(s).size();
    f.variant = n > 1 ? rng_.next() % n : 0;
}

bool Foes::sees(const Foe& f, const Quarry& q) const {
    // FUN_00424700: an agent other than itself, whose visibility is above
    // the looker's threshold (instinct 2), within the sight range (the
    // distance squared against the range squared), in line of sight
    // (FUN_0044cb80, a table of half cells under 16 apart on both axes) and
    // of a faction it is hostile to (GEnemyPlug_v97: 1, 2 and 9, so always
    // the player).
    if (!q.alive || q.visibility <= f.mind.emotion[2]) return false;
    const double range = f.mind.role == 15 ? kSightMeditating : kSight;
    const double dx = q.x - f.x, dy = q.y - f.y;
    if (dx * dx + dy * dy > range * range) return false;
    const int ax = halfCell(f.x), ay = halfCell(f.y), bx = halfCell(q.x), by = halfCell(q.y);
    if (std::abs(bx - ax) >= 16 || std::abs(by - ay) >= 16) return false;
    // The table is built by FUN_0044c330 / FUN_0044c510: rays from each
    // half cell stop at the first half cell whose height byte is over 3 (a
    // wall or a tall gizmo; low clutter and floor do not block). The rebuild
    // casts the one ray from the looker to the player.
    const int dx2 = bx - ax, dy2 = by - ay;
    const bool alongX = std::abs(dy2) < std::abs(dx2);
    const int steps = alongX ? std::abs(dx2) : std::abs(dy2);
    for (int i = 1; i <= steps; ++i) {
        const int x = alongX ? ax + (dx2 > 0 ? i : -i) : ax + int(std::floor(double(dx2) * i / steps + 0.5));
        const int y = alongX ? ay + int(std::floor(double(dy2) * i / steps + 0.5)) : ay + (dy2 > 0 ? i : -i);
        if (!walk_.inside(x, y) || walk_.at(x, y).height > 3) return false;
    }
    return true;
}

bool Foes::inReach(const Foe& f, const Quarry& q) const {
    const double dx = q.x - f.x, dy = q.y - f.y;
    if (f.shoots) {
        // FUN_00421940 with the holder in ranged mode (GAmpEnemyPlug_v4):
        // out of reach while a bolt of its was stopped by a wall (plug+0x4b);
        // otherwise the distance squared against plug+0x228 for the
        // LongRangeAttack policy (its slot 8) and against +0x220 (a quarter
        // of it) for the others.
        if (f.fireBlocked) return false;
        const double r2 = f.mind.policy == Policy::LongRange ? f.range2 : f.attackRange2;
        return dx * dx + dy * dy <= r2;
    }
    // GEnemyPlug_v3 (0x486c50): out of reach when (target radius + reach)^2
    // is less than the distance squared.
    const double r = q.radius + f.reach;
    return dx * dx + dy * dy <= r * r;
}

bool Foes::canFire(const Foe& f) const {
    // GAgentPlug_v54 -> GRangedWeapon_v6: the energy covers a shot.
    return f.ranged && f.energy >= f.ranged->ranged->cost;
}

bool Foes::moveTo(Foe& f, double x, double y, uint64_t nowMs) {
    // GMoveAction_v1 / GBlindMoveAction_v1 (one function): a path by
    // FUN_004239f0; none and the action fails at once, after telling the
    // mind (slot 13, GRoleMind_v13).
    f.path = walk_.findPath({f.x, f.y}, {x, y}, f.limit);
    f.mind.action = Action::Move;
    // GAgentPlug_v80: walking clears the fire-blocked flag (plug+0x4b).
    if (!f.path.empty()) f.fireBlocked = false;
    setState(f, f.path.empty() ? agt::Idle : agt::Move, nowMs);
    if (!f.path.empty() || std::hypot(x - f.x, y - f.y) < 0.25) return true;
    // GRoleMind_v13: 1 -> 2 (attack from afar), 2 and 4 -> 6, 3 -> 4 (flee
    // hard), 5 -> 0, 6 -> 7, 7 -> 8.
    int& c = f.mind.combat;
    switch (c) {
        case 1: c = 2; break;
        case 2: case 4: c = 6; break;
        case 3: c = 4; break;
        case 5: c = 0; break;
        case 6: c = 7; break;
        case 7: c = 8; break;
        default: break;
    }
    return false;
}

void Foes::wake(int id, uint64_t nowMs) {
    Foe& f = foes_[size_t(id)];
    if (f.dead) return;
    // GVantirPlug_v1 / GCimDronePlug_v1 (GIActivatable slot 1): every role
    // becomes 2 and the stasis ends.
    f.mind.role = 2;
    if (f.mind.action == Action::Stasis) f.mind.action = Action::Idle;
    const uint16_t cls = tmpl(f).cls;
    if (cls != 3 && cls != 10 && cls != 12) return;
    // GVantirAwakePolicy_v2: the goal is 2 cells along the facing;
    // GBlindMoveAction_v1 finds a path there as a move does (FUN_004239f0).
    startPolicy(f, Policy::Awake, Quarry{}, nowMs, f.x + facingCos(f.facing) * 2.0, f.y + facingSin(f.facing) * 2.0);
}

bool Foes::order(uint16_t instance, Order o, double x, double y, uint64_t nowMs) {
    for (Foe& f : foes_) {
        if (f.record->instance != instance) continue;
        if (f.dead) return false;  // FUN_004e4d70: done at once
        Mind& m = f.mind;
        m.policy = Policy::None;
        m.scripted = true;
        f.swing = Swing{};
        if (o == Order::Move) {
            moveTo(f, x, y, nowMs);
        } else {
            // GBlindTurnAction_v1: the agent's slot 0xdc turns it towards
            // the point; its rate is not read, the rebuild turns at once.
            f.path.clear();
            if (x != f.x || y != f.y) f.facing = facingTowards(x - f.x, y - f.y);
            m.action = Action::Idle;
            setState(f, agt::Idle, nowMs);
            m.scripted = false;
        }
        return true;
    }
    return false;
}

bool Foes::ordered(uint16_t instance) const {
    for (const Foe& f : foes_)
        if (f.record->instance == instance) return !f.dead && f.mind.scripted;
    return false;
}

void Foes::startSwing(Foe& f, const Quarry& q, uint64_t nowMs) {
    // GAttackAction_v1 -> slot 0xe8: turn to the target (GAgentPlug_v58)
    // and play the melee animation, or with the holder in ranged mode the
    // shoot animation.
    f.path.clear();
    if (q.x != f.x || q.y != f.y) f.facing = facingTowards(q.x - f.x, q.y - f.y);
    f.aimX = q.x, f.aimY = q.y;  // for shoot animations without a -2 tag (turrets)
    f.mind.action = Action::Attack;
    setState(f, f.shoots ? agt::Shoot : agt::Melee, nowMs);
    f.swing.start(nowMs);
}

void Foes::fire(int id, Foe& f, const Swing::Result& r) {
    // As the player's shot (GAgentPlug_v14 in ranged mode -> v19 ->
    // FUN_00520bc0): the muzzle is the tag point; an aim point nearer than
    // 2 cells is pushed out to 2 cells along the facing; the shot costs
    // the weapon's energy.
    const double mx = f.x + r.blow->x, my = f.y + r.blow->y;
    double ax = f.aimX, ay = f.aimY;
    if ((ax - f.x) * (ax - f.x) + (ay - f.y) * (ay - f.y) < 4.0) {
        const uint8_t h = facingTowards(ax - f.x, ay - f.y);
        ax = f.x + facingCos(h) * 2.0, ay = f.y + facingSin(h) * 2.0;
    }
    shots_.push_back({id, mx, my, r.height, facingTowards(ax - mx, ay - my)});
    f.energy = std::max(0.0, f.energy - f.ranged->ranged->cost);
    // GAgentPlug_v19: a shot whose line to the aim point is blocked
    // (FUN_00402120 at the muzzle's height + 0.1) marks an agent that can
    // walk (it has state 2 animations) as blocked (plug+0x4b), which keeps
    // its target out of reach until it walks again.
    if (!lineOfFire(walk_, mx, my, f.aimX, f.aimY, r.height + 0.1) && !tmpl(f).animsFor(agt::Move).empty())
        f.fireBlocked = true;
}

bool Foes::startPolicy(Foe& f, Policy p, const Quarry& q, uint64_t nowMs, double gx, double gy) {
    Mind& m = f.mind;
    // FUN_004e50c0 (the fighting and fleeing policies, which have a target):
    // asking for the policy already running changes nothing. FUN_004e5140
    // (moves, wandering) always starts afresh.
    const bool targeted = p == Policy::Attack || p == Policy::LongRange || p == Policy::Flee ||
                          p == Policy::HardFlee || p == Policy::Flank || p == Policy::Wait;
    if (targeted && m.policy == p) return false;
    m.policy = p;
    m.step = 0;
    m.goalX = gx, m.goalY = gy;
    prepareStep(f, q, nowMs);
    return m.policy != Policy::None;
}

void Foes::endPolicy(Foe& f, uint64_t nowMs) {
    Mind& m = f.mind;
    m.policy = Policy::None;
    m.action = Action::Idle;
    m.blind = false;
    f.path.clear();
    setState(f, agt::Idle, nowMs);
}

void Foes::prepareStep(Foe& f, const Quarry& q, uint64_t nowMs) {
    // FUN_004bd1b0: the policy's slot 2 readies action `step`, which then
    // starts (FUN_004e51b0). An action that fails at once (a move with no
    // path) ends the policy; one that has nothing to do passes on to the
    // next step (FUN_004bd160).
    Mind& m = f.mind;
    for (int guard = 0; guard < 8; ++guard) {
        if (m.step >= stepCount(m.policy)) {
            endPolicy(f, nowMs);
            return;
        }
        m.blind = false;
        bool started = false, failed = false;
        auto move = [&](double x, double y) {
            if (moveTo(f, x, y, nowMs) && !f.path.empty()) started = true;
            else if (!f.path.empty() || std::hypot(x - f.x, y - f.y) >= 0.25) failed = true;
        };
        auto turnToTarget = [&] {
            // GBlindTurnAction_v1: slot 0xdc turns the agent to the target;
            // the rate is not read, the rebuild turns at once.
            f.path.clear();
            if (q.x != f.x || q.y != f.y) f.facing = facingTowards(q.x - f.x, q.y - f.y);
            m.action = Action::Turn;
            setState(f, agt::Idle, nowMs);
            started = true;
        };
        auto idle = [&] {
            f.path.clear();
            m.action = Action::Idle;
            setState(f, agt::Idle, nowMs);
            started = true;
        };
        switch (m.policy) {
            case Policy::Move: move(m.goalX, m.goalY); break;
            case Policy::Awake:
                m.blind = true;
                move(m.goalX, m.goalY);
                break;
            case Policy::Attack:
            case Policy::LongRange: {
                // GAttackPolicy_v2 with the reach (FUN_00421940): no target
                // ends the policy; a shooter short of energy (GAmpEnemyPlug_v4
                // gives 3) tells the mind (slot 15, GRoleMind_v15: combat
                // state 7, or 8 after 7) and ends it; in reach goes straight
                // to the blow; out of reach on the attack step goes back to
                // the move.
                if (!q.alive) {
                    m.step = stepCount(m.policy);
                    continue;
                }
                if (f.shoots && !canFire(f)) {
                    m.combat = m.combat == 7 ? 8 : 7;
                    m.step = stepCount(m.policy);
                    continue;
                }
                const bool reach = inReach(f, q);
                if (m.step == 0) {
                    if (reach) break;
                    m.goalX = q.x, m.goalY = q.y;
                    move(q.x, q.y);
                } else if (!reach) {
                    m.step = 0;
                    continue;
                } else if (f.shoots || f.canMelee) {
                    startSwing(f, q, nowMs);
                    started = true;
                }
                break;
            }
            case Policy::Wander:
                if (m.step == 0) {
                    // GWanderPolicy_v2: up to 32 steps either side of the
                    // facing, 2-4 cells ahead; else one cell along the facing.
                    int turn = int(rng_.next() % 33);
                    if (rng_.next() % 100 > 49) turn = -turn;
                    const uint8_t h = uint8_t(f.facing + turn);
                    double d = double(rng_.next() % 3 + 2);
                    auto chosen = pickHeading(f, h, d, q);
                    if (!chosen) chosen = pickHeading(f, f.facing, d = 1.0, q);
                    if (!chosen) break;
                    move(f.x + facingCos(*chosen) * d, f.y + facingSin(*chosen) * d);
                } else {
                    idle();
                }
                break;
            case Policy::Flee:
            case Policy::HardFlee:
                // GFleePolicy_v2: no point to flee to ends the policy.
                if (!fleePoint(f, q, m.policy == Policy::HardFlee)) {
                    m.step = stepCount(m.policy);
                    continue;
                }
                move(m.goalX, m.goalY);
                break;
            case Policy::Flank:
                m.blind = true;
                if (m.step == 0) {
                    // GFlankPolicy_v2: no side step ends the policy.
                    if (!q.alive || !flankPoint(f, q)) {
                        m.step = stepCount(m.policy);
                        continue;
                    }
                    move(m.goalX, m.goalY);
                } else if (m.step == 1) {
                    turnToTarget();
                } else {
                    idle();
                }
                break;
            case Policy::Wait:
                // GWaitPolicy: [BlindTurn, BlindIdle] at the target.
                m.blind = true;
                if (m.step == 0) turnToTarget();
                else idle();
                break;
            case Policy::None: break;
        }
        if (started) return;
        if (failed) {
            endPolicy(f, nowMs);
            return;
        }
        ++m.step;
    }
    endPolicy(f, nowMs);
}

void Foes::nextStep(Foe& f, const Quarry& q, uint64_t nowMs) {
    ++f.mind.step;
    prepareStep(f, q, nowMs);
    // GRoleMind_v1: a policy that runs out asks the thinker, then rests.
    if (f.mind.policy == Policy::None && !decide(f, q, nowMs)) rest(f, q, nowMs);
}

bool Foes::hostileTo(const Foe& f, uint16_t faction) const {
    // GEnemyPlug_v97 (slot 0x184).
    return faction == 1 || faction == 9 || (faction == 2 && tmpl(f).flags[2] == 1);
}

bool Foes::occupied(const Foe& f, double x, double y, const Quarry& q) const {
    // FUN_00424f60: another agent seen by it (visibility above its
    // instinct 2) stands within the two radii of the point.
    const double own = tmpl(f).d20;
    auto within = [&](double ax, double ay, double r) {
        const double dx = ax - x, dy = ay - y, rr = r + own;
        return dx * dx + dy * dy <= rr * rr;
    };
    if (q.alive && q.visibility > f.mind.emotion[2] && within(q.x, q.y, q.radius)) return true;
    for (const Foe& g : foes_) {
        if (&g == &f || g.dead || g.held) continue;
        // Enemies' own visibility is not tracked: it never drops below its
        // base (128), above every instinct 2 (127).
        if (within(g.x, g.y, tmpl(g).d20)) return true;
    }
    return false;
}

std::optional<uint8_t> Foes::pickHeading(const Foe& f, uint8_t heading, double dist, const Quarry& q, int straight) {
    // FUN_00426bd0: the heading and seven more around it, each with a
    // preference (the straight one's lowered by `straight`); the most
    // preferred whose point `dist` ahead can be walked to in a straight line
    // (FUN_00423aa0) and is not taken by another agent (FUN_00424f60), a tie
    // going either way.
    static constexpr int kTurn[8] = {0, 32, -32, 64, -64, 96, -96, 128};
    int weight[8] = {10 - straight, 8, 8, 6, 6, 4, 4, 2};
    std::optional<uint8_t> best;
    int bestWeight = 0;
    for (int i = 0; i < 8; ++i) {
        if (weight[i] < bestWeight) break;
        const uint8_t h = uint8_t(heading + kTurn[i]);
        const double tx = f.x + facingCos(h) * dist, ty = f.y + facingSin(h) * dist;
        if (!walk_.passableAt(tx, ty, f.limit)) continue;
        if (!walk_.lineClear(halfCell(f.x), halfCell(f.y), halfCell(tx), halfCell(ty), f.limit)) continue;
        if (occupied(f, tx, ty, q)) continue;
        if (best && weight[i] <= bestWeight && (weight[i] < bestWeight || rng_.next() % 100 >= 50)) continue;
        best = h, bestWeight = weight[i];
    }
    return best;
}

bool Foes::fleePoint(Foe& f, const Quarry& q, bool hard) {
    // FUN_00427810: within 6 cells of the threat (36 squared), unless the
    // agent already faces away from it (its facing plus the policy's slot 8:
    // 0, or 5 fleeing hard), a step away (slot 9: 1.5 cells, 0.75 fleeing
    // hard).
    const double dx = f.x - q.x, dy = f.y - q.y;
    if (dx * dx + dy * dy > 36.0) return false;
    const uint8_t away = facingTowards(dx, dy);
    if (uint8_t(f.facing + (hard ? 5 : 0)) == away) return false;
    const double dist = hard ? 0.75 : 1.5;
    const auto h = pickHeading(f, away, dist, q);
    if (!h) return false;
    f.mind.goalX = f.x + facingCos(*h) * dist;
    f.mind.goalY = f.y + facingSin(*h) * dist;
    return true;
}

bool Foes::flankPoint(Foe& f, const Quarry& q) {
    // GFlankPolicy_v2, step 0: inside its attack range (slot 0x1ac,
    // plug+0x220) a step away from the target, the straight heading
    // weighted 6 so the diagonals (8) win; outside it a step towards the
    // target weighted 1, nearly always a diagonal; the step is 1.25 cells.
    const double dx = q.x - f.x, dy = q.y - f.y;
    const bool inside = dx * dx + dy * dy < f.attackRange2;
    const uint8_t heading = inside ? facingTowards(-dx, -dy) : facingTowards(dx, dy);
    const auto h = pickHeading(f, heading, 1.25, q, inside ? 4 : 9);
    if (!h) return false;
    f.mind.goalX = f.x + facingCos(*h) * 1.25;
    f.mind.goalY = f.y + facingSin(*h) * 1.25;
    return true;
}

std::optional<std::pair<double, double>> Foes::flock(const Foe& f) const {
    // FUN_00424700's friends: every agent in sight (the sight square, the
    // line of sight) that it is not hostile to pulls or pushes it
    // (FUN_00423750): along each axis the distance in quarter cells, up to
    // 8, picks a value from the table at 0x54df78 (-1 up close, most pull
    // at 1.25 cells, none at 2), interpolated. The original takes the
    // distances unsigned, so the push and pull do not depend on which side
    // the friend is on. The sum starts at the agent's facing; the agent
    // then goes 0.75 cells (0x54dfe0) along it.
    static constexpr double kTable[9] = {-1.0, -0.63, -0.29, 0.29, 0.68, 0.91, 0.27, 0.09, 0.0};
    const double range = f.mind.role == 15 ? kSightMeditating : kSight;
    double vx = facingCos(f.facing), vy = facingSin(f.facing);
    bool found = false;
    for (const Foe& g : foes_) {
        if (&g == &f || g.dead || g.held) continue;
        if (std::fabs(g.x - f.x) > range || std::fabs(g.y - f.y) > range) continue;
        if (hostileTo(f, tmpl(g).faction)) continue;
        Quarry at;
        at.x = g.x, at.y = g.y, at.alive = true, at.visibility = 128;
        if (!sees(f, at)) continue;
        found = true;
        const double ax = std::fabs(f.x - g.x) * 4, ay = std::fabs(f.y - g.y) * 4;
        if (ax > 8 || ay > 8) continue;
        auto lerp = [](double v) {
            const int i = std::min(7, int(v));
            return kTable[i] + (v - i) * (kTable[i + 1] - kTable[i]);
        };
        vx += lerp(ax), vy += lerp(ay);
    }
    if (!found) return std::nullopt;
    return std::make_pair(f.x + vx * 0.75, f.y + vy * 0.75);
}

bool Foes::switchWeapon(Foe& f, const Quarry& q, uint64_t nowMs) {
    // FUN_00423160 (the thinkers call it on every target they see): not
    // during an attack. Every 1.5 s a 40% chance of a random switch (a miss
    // restarts the wait). Melee goes ranged only on that chance and beyond
    // 1 cell; ranged goes melee within 2.75 cells (7.5625 squared), on the
    // chance, or out of energy.
    if (!f.canShoot || !f.canMelee || f.mind.action == Action::Attack) return false;
    const double dx = q.x - f.x, dy = q.y - f.y, d2 = dx * dx + dy * dy;
    if (!f.switchedAt) f.switchedAt = nowMs;
    bool chance = false;
    if (nowMs - f.switchedAt > 1500) {
        if (rng_.next() % 100 > 59) chance = true;
        else f.switchedAt = nowMs;
    }
    if (!f.shoots) {
        if (d2 <= 1.0 || !chance) return false;
        f.shoots = true;
    } else {
        if (!chance && d2 >= 7.5625 && canFire(f)) return false;
        f.shoots = false;
    }
    f.switchedAt = nowMs;
    return true;
}

bool Foes::fightAs(Foe& f, const Quarry& q, bool mayFlee, uint64_t nowMs) {
    // FUN_004fa9a0: a new target starts the fight at state 1; template+0xc
    // = 1 never flees; then by the combat state.
    Mind& m = f.mind;
    if (!m.engaged) m.engaged = true, m.combat = 1;
    if (tmpl(f).flags[2] == 1) mayFlee = false;
    const bool hurt = f.hitPoints < 0.4 * f.maxHitPoints;  // slot 0x98 against 0.4 (0x5570c8)
    const bool scared = m.emotion[0] < 220 && hurt && mayFlee;
    // Thinker 1 (stationary) takes GStationaryAttackPolicy (0xf) in place
    // of the attack and long range policies; the rebuild uses the attack
    // policy for it.
    switch (m.combat) {
        case 1:
            if (scared) return m.combat = 3, startPolicy(f, Policy::Flee, q, nowMs);
            break;
        case 2:
            if (scared) return m.combat = 3, startPolicy(f, Policy::Flee, q, nowMs);
            return startPolicy(f, m.thinker == 1 ? Policy::Attack : Policy::LongRange, q, nowMs);
        case 3:
            if (m.emotion[0] < 251) return startPolicy(f, Policy::Flee, q, nowMs);
            m.combat = 1;
            break;
        case 4:
            if (m.emotion[0] < 251) return startPolicy(f, Policy::HardFlee, q, nowMs);
            m.combat = 1;
            break;
        case 7: m.combat = 8; return startPolicy(f, Policy::Flank, q, nowMs);
        case 8: m.combat = 0; return startPolicy(f, Policy::Wait, q, nowMs);
        default: m.combat = 0; return startPolicy(f, Policy::Wander, q, nowMs);
    }
    return startPolicy(f, Policy::Attack, q, nowMs);
}

bool Foes::engage(Foe& f, const Quarry& q, bool trooper, uint64_t nowMs) {
    // FUN_004fa850: courage +1; a guard or a meditator stops being one.
    Mind& m = f.mind;
    m.emotion[0] = uint8_t(std::min(255, m.emotion[0] + 1));
    if (m.role == 0 || m.role == 15) m.role = 2;
    if (!trooper) return fightAs(f, q, true, nowMs);
    // FUN_004fa8d0: a target shining brighter than 240 is fled; state 6
    // looks for a shut door with a console within 5 cells (FUN_004254a0, to
    // open it: not rebuilt) and without one becomes 5. A target whose base
    // visibility is under 128 would also alert the agent's friends in sight
    // (FUN_004249d0); the player's is 128.
    if (q.visibility > 240) return startPolicy(f, Policy::Flee, q, nowMs);
    if (m.combat == 6) m.combat = 5;
    return fightAs(f, q, false, nowMs);
}

bool Foes::decide(Foe& f, const Quarry& q, uint64_t nowMs) {
    // The thinkers' slot 4 (GTrooperThinker_v4 and GBossThinker_v4 for
    // troopers, commanders and bosses, GMechanizedThinker_v4 for the rest):
    // a target in sight (none while the action is blind) is fought, the
    // weapon switched first (FUN_00423160).
    Mind& m = f.mind;
    const bool trooper = m.thinker == 7 || m.thinker == 8 || m.thinker == 9;
    const bool provoked = m.provoked && q.alive;
    m.provoked = false;
    if (provoked || (!m.blind && sees(f, q))) {
        switchWeapon(f, q, nowMs);
        return engage(f, q, trooper, nowMs);
    }
    // FUN_004fac70: a target no longer seen is forgotten (GRoleMind_v14);
    // unless blind, the agent heads for where it is now.
    if (m.engaged) {
        m.engaged = false, m.combat = 0;
        if (!m.blind && q.alive) return startPolicy(f, Policy::Move, q, nowMs, q.x, q.y);
    }
    // Neither guarding nor meditating: keep near friends in sight (troopers
    // look for a shut door to open first, FUN_004254a0: not rebuilt).
    if (m.role != 0 && m.role != 15)
        if (const auto p = flock(f)) return startPolicy(f, Policy::Move, q, nowMs, p->first, p->second);
    return false;
}

void Foes::rest(Foe& f, const Quarry& q, uint64_t nowMs) {
    // The thinkers' slot 5 when there is nothing to do. A guard stands
    // (GIThinker_v5: the idle action); a meditator meditates (action 0xe);
    // anyone else wanders when restless (instinct 1 over 40; plug+0x47,
    // which also forces it, is not traced). Troopers, commanders and bosses
    // (GBossThinker_v5) wander only half the time; otherwise they calm down
    // by 40 and walk to the least visited beacon of a room next to theirs
    // (FUN_00423e40), wandering if there is none. Animals (GAnimalThinker_v5)
    // wander when restless whatever their role.
    Mind& m = f.mind;
    m.policy = Policy::None;
    m.blind = false;
    const bool animal = m.thinker == 5;
    if (!animal && m.role == 15) {
        f.path.clear();
        m.action = Action::Meditate;
        setState(f, tmpl(f).animsFor(agt::State(1)).empty() ? agt::Idle : agt::State(1), nowMs);
        return;
    }
    if ((animal || m.role != 0) && m.emotion[1] > 40) {
        const bool trooper = m.thinker == 7 || m.thinker == 8 || m.thinker == 9;
        bool started = false;
        if (trooper && rng_.next() % 100 >= 50) {
            m.emotion[1] = uint8_t(m.emotion[1] - 40);
            const int b = rooms_.leastVisitedNeighbour(rooms_.at(f.x, f.y));
            if (b >= 0) started = startPolicy(f, Policy::Move, q, nowMs, rooms_.beacon(b).x, rooms_.beacon(b).y);
            else started = startPolicy(f, Policy::Wander, q, nowMs);
        } else {
            started = startPolicy(f, Policy::Wander, q, nowMs);
        }
        if (started) return;
    }
    m.policy = Policy::None;
    m.action = Action::Idle;
    f.path.clear();
    setState(f, agt::Idle, nowMs);
}

void Foes::tickPolicy(Foe& f, const Quarry& q, uint64_t nowMs) {
    // GMind_v3 -> the policy's slot 5, every think the thinker leaves it be.
    Mind& m = f.mind;
    if ((m.policy != Policy::Attack && m.policy != Policy::LongRange) || m.step != 0) return;
    // GAttackPolicy_v5 on the way: no target, or a shooter out of energy
    // (which tells the mind, slot 15), ends the policy; in reach starts the
    // blow; a target that has moved is followed.
    if (!q.alive || (f.shoots && !canFire(f))) {
        if (q.alive) m.combat = m.combat == 7 ? 8 : 7;
        endPolicy(f, nowMs);
    } else if (inReach(f, q)) {
        nextStep(f, q, nowMs);
    } else if (q.x != m.goalX || q.y != m.goalY) {
        m.goalX = q.x, m.goalY = q.y;
        if (!moveTo(f, q.x, q.y, nowMs)) endPolicy(f, nowMs);
    }
}

void Foes::think(Foe& f, const Quarry& q, uint64_t nowMs) {
    // GRoleMind_v3: an action that can be interrupted (all but stasis)
    // lets the thinker have its say; if it starts nothing new, the policy
    // does.
    if (f.mind.action == Action::Stasis) return;
    if (!decide(f, q, nowMs)) tickPolicy(f, q, nowMs);
}

std::optional<Foes::Hit> Foes::act(int id, Foe& f, const Quarry& q, uint64_t nowMs) {
    const uint64_t dt = f.lastMs ? nowMs - f.lastMs : 0;
    f.lastMs = nowMs;
    // FUN_00521e00 (GAgentPlug_v6): the ranged weapon's energy comes back
    // at its recharge rate a second, up to its maximum.
    if (f.ranged)
        f.energy = std::min<double>(f.ranged->ranged->maxEnergy, f.energy + dt * 0.001 * f.ranged->ranged->recharge);
    bool finished = false;
    std::optional<Hit> hit;
    switch (f.mind.action) {
        case Action::Stasis:
            // GStasisAction_v2 keeps restarting the clock: the Awake
            // animation (GStasisAction_v4: state 7) holds its first frame.
            f.stateStart = nowMs;
            break;
        case Action::Meditate:
            // GMeditateAction_v2: the animation loops; the action never ends.
            break;
        case Action::Turn:
            finished = true;
            break;
        case Action::Idle: {
            // The idle action lasts one round of the idle animation
            // (GAgentPlug_v79 -> FUN_0041dd10).
            const auto a = anims(f, agt::Idle);
            const uint64_t frames = a.empty() || a[0]->directions.empty() ? 0 : a[0]->directions[0].frames.size();
            const uint64_t loopMs = std::max<uint64_t>(1, frames) * Level::kAgentFrameMs;
            if ((nowMs - std::min(nowMs, f.stateStart)) / loopMs > f.loops) ++f.loops, finished = true;
            break;
        }
        case Action::Move:
            // The rebuild stops a walk to a target once in reach of it, so
            // an enemy does not walk into the player.
            if ((f.mind.policy == Policy::Attack || f.mind.policy == Policy::LongRange) && q.alive && inReach(f, q))
                f.path.clear();
            followPath(f.path, f.x, f.y, f.facing, f.speed * double(dt));
            if (f.path.empty()) finished = true;
            break;
        case Action::Attack: {
            const auto a = anims(f, f.shoots ? agt::Shoot : agt::Melee);
            auto turn = [&] {
                if (q.x != f.x || q.y != f.y) f.facing = facingTowards(q.x - f.x, q.y - f.y);
                f.aimX = q.x, f.aimY = q.y;
            };
            // Another round while the target is in reach (and, shooting,
            // the energy lasts).
            auto goOn = [&] { return q.alive && inReach(f, q) && (!f.shoots || canFire(f)); };
            const auto r = f.swing.advance(a, f.facing, nowMs, Level::kAgentFrameMs, [&] {
                if (!goOn()) return false;
                turn();
                return true;
            });
            if (r.aim && f.shoots) {
                const auto p = leadAim(f.x, f.y, q.x, q.y, q.vx, q.vy, q.dest, f.boltSpeed, *r.aim);
                f.aimX = p.x, f.aimY = p.y;
            }
            if (r.blow && f.shoots) {
                if (canFire(f)) fire(id, f, r);
            } else if (r.blow && q.alive && q.defender && f.armed) {
                // FUN_004a2590: the segment to the blow point against the
                // target's square.
                const double bx = f.x + r.blow->x, by = f.y + r.blow->y;
                if (segmentTouchesBox(f.x, f.y, bx, by, q.x, q.y, q.radius + 0.1))
                    hit = Hit{id, strike(f.rating, f.weapon, *q.defender, rng_)};
            }
            if (r.ended) {
                // GAmpEnemyPlug_v82: at the end of the animation the attack
                // goes on while the target is in reach.
                if (goOn()) {
                    turn();
                    f.swing.start(nowMs);
                    f.stateStart = nowMs;
                } else {
                    finished = true;
                }
            }
            break;
        }
    }
    if (finished && f.mind.scripted) {
        // The script's order is done (agent+0x1f0); the agent stands.
        f.mind.scripted = false;
        f.mind.action = Action::Idle;
        f.path.clear();
        setState(f, agt::Idle, nowMs);
    } else if (finished) {
        if (f.mind.policy != Policy::None) nextStep(f, q, nowMs);
        else if (!decide(f, q, nowMs)) rest(f, q, nowMs);  // GRoleMind_v1
    }
    return hit;
}

std::vector<Foes::Hit> Foes::update(uint64_t nowMs, const Quarry& player, bool cinematic) {
    std::vector<Hit> hits;
    for (size_t i = 0; i < foes_.size(); ++i) {
        Foe& f = foes_[i];
        if (f.dead || f.held) continue;
        Mind& m = f.mind;
        if (cinematic && !m.scripted) {
            f.lastMs = nowMs;  // the walk picks up from here afterwards
            continue;
        }
        // GMind_v6: the first call only starts the clock.
        if (m.scripted) {
            // Carrying out an order: the mind waits.
        } else if (!m.lastThink) {
            m.lastThink = std::max<uint64_t>(nowMs, 1);
            f.stateStart = nowMs;
        } else if (nowMs - m.lastThink >= kThinkMs) {
            m.lastThink = nowMs;
            think(f, player, nowMs);
        }
        if (auto h = act(int(i), f, player, nowMs)) hits.push_back(*h);
    }
    return hits;
}

WalkMap::Point Foes::position(const chk::AgentRecord& a) const {
    for (const auto& f : foes_)
        if (f.record == &a) return {f.x, f.y};
    return {a.x, a.y};
}

Level::Pose Foes::pose(const chk::AgentRecord& a, uint64_t nowMs) {
    const Foe* f = nullptr;
    for (const auto& x : foes_)
        if (x.record == &a) f = &x;
    if (!f) return level_.agentPose(a, nowMs);
    Level::Pose pose;
    // Dead foes play the death animation once and keep its last frame.
    const agt::State state = f->dead ? agt::Die : f->state;
    const auto list = tmpl(*f).animsFor(state);
    if (list.empty()) {
        if (f->dead) return pose;  // nothing to show: the body is gone
        return level_.agentPose(a, nowMs);
    }
    const size_t variant = f->dead ? a.instance % list.size() : f->variant % list.size();
    const anm::Animation* anim = sprites_.anim(list[variant]->path);
    if (!anim || anim->directions.empty()) return pose;
    const size_t dir = anm::directionFor(f->facing, anim->directions.size());
    if (dir >= anim->directions.size() || anim->directions[dir].frames.empty()) return pose;
    const size_t frames = anim->directions[0].frames.empty() ? anim->directions[dir].frames.size()
                                                              : anim->directions[0].frames.size();
    const uint64_t start = f->dead ? f->diedAt : f->state == agt::Melee || f->state == agt::Shoot ? f->swing.startMs() : f->stateStart;
    const size_t step = size_t((nowMs - std::min(nowMs, start)) / Level::kAgentFrameMs);
    const size_t frame = std::min(f->dead ? std::min(step, frames - 1) : step % frames,
                                  anim->directions[dir].frames.size() - 1);
    pose.key = "foe#" + std::to_string(a.instance) + "#" + std::to_string(int(state)) + "#" +
               std::to_string(variant) + "#" + std::to_string(dir) + "#" + std::to_string(frame);
    pose.image = sprites_.compose(pose.key, {anim}, dir, frame);
    if (!pose.image) return pose;
    pose.anchorX = anim->hotspot ? anim->hotspot->first : pose.image->width / 2;
    pose.anchorY = anim->hotspot ? anim->hotspot->second : pose.image->height;
    return pose;
}

}  // namespace harbinger::world
