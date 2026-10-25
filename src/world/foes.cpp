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
        case Foes::Policy::Attack: return 2;  // move, attack
        case Foes::Policy::Wander: return 2;  // move, idle
        case Foes::Policy::Move: return 1;
        case Foes::Policy::Flee: return 1;
        case Foes::Policy::None: return 0;
    }
    return 0;
}

int halfCell(double v) { return int(std::floor(v * 2)); }

}  // namespace

Foes::Foes(Vfs& vfs, const Ini& ini, Level& level, const WalkMap& walk, Rng& rng)
    : level_(level), walk_(walk), rng_(rng), sprites_(vfs) {
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
        f.shoots = f.ranged && !shoot.empty();
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
        }
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
        // within the attack policy's range (the distance squared against
        // plug+0x220), and not blocked (plug+0x4b; what sets it is not
        // traced: the rebuild checks the line of fire).
        if (dx * dx + dy * dy > f.attackRange2) return false;
        return lineOfFire(walk_, f.x, f.y, q.x, q.y, kMuzzleGuess + 0.1);
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

void Foes::moveTo(Foe& f, double x, double y, uint64_t nowMs) {
    f.path = walk_.findPath({f.x, f.y}, {x, y}, f.limit);
    f.mind.action = Action::Move;
    setState(f, f.path.empty() ? agt::Idle : agt::Move, nowMs);
}

void Foes::wake(int id, uint64_t nowMs) {
    Foe& f = foes_[size_t(id)];
    const uint16_t cls = tmpl(f).cls;
    if (f.dead || (cls != 3 && cls != 10 && cls != 12)) return;
    // GVantirAwakePolicy_v2: the goal is 2 cells along the facing;
    // GBlindMoveAction_v1 finds a path there as a move does (FUN_004239f0).
    startPolicy(f, Policy::Move, Quarry{}, nowMs, f.x + facingCos(f.facing) * 2.0, f.y + facingSin(f.facing) * 2.0);
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
}

bool Foes::startPolicy(Foe& f, Policy p, const Quarry& q, uint64_t nowMs, double gx, double gy) {
    Mind& m = f.mind;
    // FUN_004e50c0: asking for the policy already running (on the same
    // target) changes nothing; FUN_004e5140 (moves, wandering) always
    // starts afresh.
    if ((p == Policy::Attack || p == Policy::Flee) && m.policy == p) return false;
    m.policy = p;
    m.step = 0;
    m.goalX = gx, m.goalY = gy;
    prepareStep(f, q, nowMs);
    return m.policy != Policy::None;
}

void Foes::prepareStep(Foe& f, const Quarry& q, uint64_t nowMs) {
    // FUN_004bd1b0: the policy's slot 2 readies action `step`, which then
    // starts; one that cannot start passes on to the next (FUN_004bd160).
    Mind& m = f.mind;
    for (;;) {
        if (m.step >= stepCount(m.policy)) {
            m.policy = Policy::None;
            m.action = Action::Idle;
            f.path.clear();
            setState(f, agt::Idle, nowMs);
            return;
        }
        bool started = false;
        switch (m.policy) {
            case Policy::Move:
                moveTo(f, m.goalX, m.goalY, nowMs);
                started = !f.path.empty();
                break;
            case Policy::Attack:
                if (m.step == 0) {
                    // GAttackPolicy_v2: already in reach, straight to the blow.
                    if (!q.alive || inReach(f, q)) break;
                    m.goalX = q.x, m.goalY = q.y;
                    moveTo(f, q.x, q.y, nowMs);
                    started = !f.path.empty();
                } else if (q.alive && inReach(f, q) &&
                           (f.shoots ? canFire(f) : f.armed && !anims(f, agt::Melee).empty())) {
                    // A shooter short of energy does not start: the policy
                    // ends and the next think starts it again.
                    startSwing(f, q, nowMs);
                    started = true;
                }
                break;
            case Policy::Wander:
                if (m.step == 0) {
                    // GWanderPolicy_v2: up to 32 steps either side of the
                    // facing, 2-4 cells ahead; else one cell along the facing.
                    int turn = int(rng_.next() % 33);
                    if (rng_.next() % 100 > 49) turn = -turn;
                    const uint8_t h = uint8_t(f.facing + turn);
                    const double dist = double(rng_.next() % 3 + 2);
                    double d = dist;
                    auto chosen = pickHeading(f, h, d);
                    if (!chosen) chosen = pickHeading(f, f.facing, d = 1.0);
                    if (!chosen) break;
                    moveTo(f, f.x + facingCos(*chosen) * d, f.y + facingSin(*chosen) * d, nowMs);
                    started = !f.path.empty();
                } else {
                    m.action = Action::Idle;
                    setState(f, agt::Idle, nowMs);
                    started = true;
                }
                break;
            case Policy::Flee:
                if (fleePoint(f, q)) {
                    moveTo(f, m.goalX, m.goalY, nowMs);
                    started = !f.path.empty();
                }
                break;
            case Policy::None: break;
        }
        if (started) return;
        ++m.step;
    }
}

void Foes::nextStep(Foe& f, const Quarry& q, uint64_t nowMs) {
    ++f.mind.step;
    prepareStep(f, q, nowMs);
}

std::optional<uint8_t> Foes::pickHeading(const Foe& f, uint8_t heading, double dist) {
    // FUN_00426bd0: the heading and seven more around it, each with a
    // preference; the most preferred whose point `dist` ahead can be walked
    // to in a straight line (FUN_00423aa0), a tie going either way.
    static constexpr int kTurn[8] = {0, 32, -32, 64, -64, 96, -96, 128};
    static constexpr int kWeight[8] = {10, 8, 8, 6, 6, 4, 4, 2};
    std::optional<uint8_t> best;
    int bestWeight = 0;
    for (int i = 0; i < 8; ++i) {
        if (kWeight[i] < bestWeight) break;
        const uint8_t h = uint8_t(heading + kTurn[i]);
        const double tx = f.x + facingCos(h) * dist, ty = f.y + facingSin(h) * dist;
        if (!walk_.passableAt(tx, ty, f.limit)) continue;
        if (!walk_.lineClear(halfCell(f.x), halfCell(f.y), halfCell(tx), halfCell(ty), f.limit)) continue;
        if (best && kWeight[i] == bestWeight && rng_.next() % 100 >= 50) continue;
        best = h, bestWeight = kWeight[i];
    }
    return best;
}

bool Foes::fleePoint(Foe& f, const Quarry& q) {
    // FUN_00427810: within 6 cells of the threat (36 squared), 1.5 cells
    // (GFleePolicy_v9) away from it.
    const double dx = f.x - q.x, dy = f.y - q.y;
    if (dx * dx + dy * dy > 36.0) return false;
    const auto h = pickHeading(f, facingTowards(dx, dy), 1.5);
    if (!h) return false;
    f.mind.goalX = f.x + facingCos(*h) * 1.5;
    f.mind.goalY = f.y + facingSin(*h) * 1.5;
    return true;
}

bool Foes::engage(Foe& f, const Quarry& q, bool mayFlee, uint64_t nowMs) {
    // FUN_004fa850: courage +1; a guard or a meditator stops being one.
    Mind& m = f.mind;
    m.emotion[0] = uint8_t(std::min(255, m.emotion[0] + 1));
    if (m.role == 0 || m.role == 15) m.role = 2;
    if (!mayFlee && q.visibility > 240) return startPolicy(f, Policy::Flee, q, nowMs);  // FUN_004fa8d0
    // FUN_004fa9a0: a new target starts the fight at state 1.
    if (!m.engaged) m.engaged = true, m.combat = 1;
    if (tmpl(f).flags[2] == 1) mayFlee = false;
    const bool hurt = f.hitPoints < 0.4 * f.maxHitPoints;  // slot 0x98 against 0.4 (0x5570c8)
    if (m.combat == 1 && mayFlee && m.emotion[0] < 220 && hurt) m.combat = 3;
    if (m.combat == 3) {
        if (m.emotion[0] < 251) return startPolicy(f, Policy::Flee, q, nowMs);
        m.combat = 1;
    }
    // Thinker 1 (stationary) takes GStationaryAttackPolicy (0xf) instead;
    // the rebuild uses the attack policy for all.
    return startPolicy(f, Policy::Attack, q, nowMs);
}

bool Foes::decide(Foe& f, const Quarry& q, uint64_t nowMs) {
    // GMechanizedThinker_v4 (predators, mechanized) and GTrooperThinker_v4:
    // look for a target; fight it, or (predators) flee it when hurt.
    Mind& m = f.mind;
    const bool provoked = m.provoked && q.alive;
    m.provoked = false;
    if (provoked || sees(f, q)) return engage(f, q, m.thinker != 7, nowMs);
    // FUN_004fac70: a target just lost is gone after once; the agent heads
    // for where it is now.
    if (m.engaged) {
        m.engaged = false, m.combat = 0;  // GRoleMind_v14
        if (q.alive) return startPolicy(f, Policy::Move, q, nowMs, q.x, q.y);
    }
    return false;
}

void Foes::rest(Foe& f, const Quarry& q, uint64_t nowMs) {
    // The thinkers' slot 5 when there is nothing to do: a guard stands
    // (GIThinker_v5, the idle action); a meditator wanders; anyone else
    // wanders when restless (instinct 1 over 40). Troopers (GBossThinker_v5)
    // wander half the time and otherwise calm down by 40 (where they then go,
    // FUN_00423e40, is not read; the rebuild has them stand).
    Mind& m = f.mind;
    bool wander = m.role == 15;
    if (m.role != 0 && m.role != 15 && m.emotion[1] > 40) {
        if (m.thinker == 7 && rng_.next() % 100 >= 50) m.emotion[1] = uint8_t(m.emotion[1] - 40);
        else wander = true;
    }
    if (wander && startPolicy(f, Policy::Wander, q, nowMs)) return;
    m.policy = Policy::None;
    m.action = Action::Idle;
    f.path.clear();
    setState(f, agt::Idle, nowMs);
}

void Foes::tickPolicy(Foe& f, const Quarry& q, uint64_t nowMs) {
    // GMind_v3 -> the policy's slot 5, every think the thinker leaves it be.
    Mind& m = f.mind;
    if (m.policy != Policy::Attack || m.step != 0) return;
    // GAttackPolicy_v5: on the way, start the blow once in reach; follow a
    // target that has moved.
    if (q.alive && inReach(f, q)) {
        nextStep(f, q, nowMs);
    } else if (q.alive && (q.x != m.goalX || q.y != m.goalY)) {
        m.goalX = q.x, m.goalY = q.y;
        moveTo(f, q.x, q.y, nowMs);
    }
}

void Foes::think(Foe& f, const Quarry& q, uint64_t nowMs) {
    // GRoleMind_v3: every action can be interrupted, so the thinker always
    // gets its say; if it starts nothing new, the policy does.
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
            if (f.mind.policy == Policy::Attack && q.alive && inReach(f, q)) f.path.clear();
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
