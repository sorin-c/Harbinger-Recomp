// The in-game view, as far as it is rebuilt: a level's floor, walls, gizmos
// and characters, drawn and animated from the original files, and the
// player's character walking on it with the camera following, and fighting
// the level's enemies hand to hand and at range.
#pragma once

#include <SDL3/SDL.h>

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/assets.hpp"
#include "app/cutscene.hpp"
#include "app/sound.hpp"
#include "world/bolts.hpp"
#include "world/combat.hpp"
#include "world/foes.hpp"
#include "world/inventory.hpp"
#include "world/item_catalog.hpp"
#include "world/level.hpp"
#include "world/loot.hpp"
#include "world/player.hpp"
#include "world/spawners.hpp"
#include "world/umbilical.hpp"
#include "world/story.hpp"
#include "world/walkmap.hpp"

namespace harbinger::app {

class WorldView {
public:
    // Loads level `id`, stands the player's character on its entry point
    // `entry` (0, "Default", for a new game), or at map point `at` (a testing
    // aid), and centres the camera on it. `inventory` is the character's
    // inventory screen layout (Hud::inventoryLayout); the starting items are
    // added to it.
    WorldView(Assets& assets, SDL_Renderer* ren, uint16_t levelId, world::Character character, int screenW,
              int screenH, world::Inventory::Layout inventory, uint16_t entry = 0,
              std::optional<world::WalkMap::Point> at = {});
    ~WorldView();
    WorldView(const WorldView&) = delete;
    WorldView& operator=(const WorldView&) = delete;

    // Where the level's trigger scripts play their sounds and voice lines.
    void setSound(SoundSystem* sound) {
        sound_ = sound;
        cutscene_->setSound(sound);
    }
    // A trigger script's cinematic mode: the player takes no orders, and
    // steer, walkTo, attack and use do nothing.
    bool cinematic() const { return cutscene_->cinematic(); }
    // The player takes no orders: a cinematic, an umbilical trip under way
    // or its list open, just arrived by umbilical, or talking to an NPC.
    bool locked() const { return cinematic() || frozen_ || menu_ || travel_ || umbilicals_->busy() || talking(); }
    // Closes a script's thought bubble: a click on it at view pixel (x, y),
    // or the close button (pad or key). False if nothing was closed.
    bool clickBubble(float x, float y) { return cutscene_->click(x, y); }
    bool closeBubble() { return cutscene_->dismiss(); }
    const Cutscene& cutscene() const { return *cutscene_; }
    // Set once the player has walked into a portal, picked an umbilical
    // destination, or a script or dialogue has called SetLevel: the level
    // and entry point to load next.
    std::optional<Cutscene::LevelChange> levelChange() const;
    // Call on the new view after a trip by umbilical (LevelChange::umbilical).
    void arriveByUmbilical();

    // Umbilicals (docs/umbilicals.md). The list of places, while the
    // shell has closed round the player and the game waits for a pick
    // (the Level Selection interface, level.itf); empty otherwise.
    const std::vector<world::UmbilicalCodes::Code>* travelMenu() const { return menu_ ? &*menu_ : nullptr; }
    // A line of the list picked (its index), or -1 to cancel.
    void chooseTravel(int index);
    // The player's codes, and the actions dialogue fires: Unlock Umbilical
    // (true if the code is new: "Player received umbilical code."), Clear
    // Umbilicals (codes().clear()) and Set Level.
    world::UmbilicalCodes& codes() { return *codes_; }
    const world::EntryLookup& entries() const { return entries_; }
    bool unlockCode(uint16_t level, uint16_t entry, bool gold);
    void setLevel(uint16_t level);
    // What the player takes from level to level: the inventory (with the
    // catalogue it looks items up in), hit points and ranged energy, and
    // the story (the quest it is on, what it has done).
    struct Carried {
        std::unique_ptr<world::ItemCatalog> catalog;
        std::unique_ptr<world::Inventory> inventory;
        double hitPoints = 0, energy = 0;
        std::unique_ptr<world::UmbilicalCodes> codes;
        std::unique_ptr<world::StoryFiles> storyFiles;
        std::unique_ptr<world::Story> story;
    };
    // Hands it over; this view is not used after.
    Carried takeCarried();
    void carry(Carried c);

    // Direct control from a stick or keys: walk towards screen direction
    // (dx, dy) (any length; 0, 0 stops).
    void steer(float dx, float dy);
    // Click to move: walk to the map point under view pixel (x, y). Drops a
    // gizmo the player was on the way to use.
    void walkTo(float x, float y);
    // The same to a map point (a testing aid).
    void walkToCell(double x, double y) {
        if (!cinematic()) player_->walkTo(x, y);
    }
    // Click to attack: if an enemy is drawn under view pixel (x, y), go and
    // hit it and return true.
    bool attackAt(float x, float y);
    // The pad's attack button: hit the nearest enemy within a few cells,
    // preferring those ahead. False if there is none.
    bool attackNearest();
    // The same with the ranged weapon (right click, the pad's Y / Triangle
    // or the left trigger, G): shoot the enemy under the pointer, or the
    // nearest within Player::kShootRange.
    bool shootAt(float x, float y);
    bool shootNearest();
    // Using gizmos (consoles, chests), picking up items and talking to
    // NPCs: walk up to the item, NPC or usable gizmo under view pixel
    // (x, y) and take, talk to or use it on arrival; false if there is none
    // there.
    bool useAt(float x, float y);
    // The same for the nearest item, else the nearest NPC, else the nearest
    // usable gizmo, within kUseRange cells of the player (pad and keys).
    bool useNearest();
    bool usePending() const { return pendingUse_ != nullptr || pendingPickup_ != 0 || pendingTalk_ != nullptr; }
    // Rebuild choices: how near a gizmo's use point the player must stand
    // to use it, and how far the use button looks for one, in map cells.
    static constexpr double kUseReach = 1.5, kUseRange = 3.0;
    // How near an NPC the player stops to talk (a rebuild choice).
    static constexpr double kTalkReach = 2.0;

    // The story: the quest and the conversation with an NPC, if one is
    // open (the world stands still meanwhile, a rebuild choice).
    world::Story& story() { return *story_; }
    const world::Story& story() const { return *story_; }
    bool talking() const { return story_->talking(); }
    // The messages along the top of the view ("Player received ...", the
    // dialogues' DisplayText), newest first (FUN_004e4520's list).
    struct Message {
        std::string text;
        uint64_t until = 0;  // elapsed ms; 0 = for good
    };
    const std::deque<Message>& messages() const { return messages_; }
    void say(const std::string& text, uint64_t forMs = kMessageMs);
    static constexpr uint64_t kMessageMs = 3000;  // what every caller passes
    static constexpr size_t kMaxMessages = 4;     // rebuild choice (the list's +0x12 is not read)
    // The map point under view pixel (x, y).
    world::WalkMap::Point mapPoint(float x, float y) const;
    void update(uint64_t nowMs);
    void draw() const;

    // The player died a while ago (a rebuild choice: 3 s, time for the
    // death animation): the game is over, any button starts the level again.
    bool gameOver() const { return player_->dead() && elapsedMs_ >= player_->diedAt() + 3000; }

    const world::Level& level() const { return *level_; }
    const world::Player& player() const { return *player_; }
    const world::Foes& foes() const { return *foes_; }
    const world::Loot& loot() const { return *loot_; }
    // What the player carries; the equipment it shows and fights with
    // follows the inventory's equipment slots.
    world::Inventory& inventory() { return *inventory_; }
    world::ItemCatalog& catalog() { return *catalog_; }
    // Uses a health pack off the counter; false if there is none.
    bool useHealthPack();
    // Where on the screen the view centres (the HUD moves it while the
    // inventory covers the right part).
    void setViewCentre(double x) { centreX_ = x; }
    const world::Spawners& spawners() const { return *spawners_; }
    const world::Bolts& bolts() const { return *bolts_; }

private:
    const Texture* tile(const chk::Layer& layer) const;
    void drawLayer(int x, int y, int layer) const;
    void drawGizmo(const chk::GizmoRecord& g) const;
    void drawAgent(const chk::AgentRecord& a) const;
    void drawPlayer() const;
    void drawTargetBar() const;
    void drawHealth() const;
    void drawBolts() const;
    int foeAt(float x, float y) const;
    void playSound(uint16_t id);
    void drawGroundItems() const;
    // Where a ground item's picture sits on the map screen, and the part of
    // its image to draw; nullptr if it has no picture.
    const Texture* groundItemPicture(const world::Loot::GroundItem& g, SDL_FRect& src, SDL_FRect& at) const;
    const world::Loot::GroundItem* itemAt(float x, float y) const;
    void approachItem(const world::Loot::GroundItem& g);
    void drawImage(const std::string& key, const render::RgbaImage* img, double left, double top,
                   bool lit = false) const;
    const chk::GizmoRecord* gizmoAt(float x, float y) const;
    void approach(const chk::GizmoRecord& g);
    bool occupied(const chk::GizmoRecord& g) const;
    void updateUmbilicals();
    const chk::AgentRecord* npcAt(float x, float y) const;
    void approachNpc(const chk::AgentRecord& a);
    void bindStory();
    void runAction(const dlg::Action& a);
    void give(uint16_t item, uint32_t amount);
    std::string itemName(uint16_t id) const;
    void drawMessages() const;

    Assets& assets_;
    SDL_Renderer* ren_;
    std::unique_ptr<world::Level> level_;
    uint16_t levelId_;
    std::unique_ptr<world::WalkMap> walk_;
    std::unique_ptr<world::Player> player_;
    world::Rng rng_;
    std::unique_ptr<world::Foes> foes_;
    std::unique_ptr<Cutscene> cutscene_;
    std::unique_ptr<world::Loot> loot_;
    std::unique_ptr<world::Spawners> spawners_;
    std::vector<bool> wasHeld_;  // per foe, last frame
    std::unique_ptr<world::Bolts> bolts_;
    world::EntryLookup entries_;
    std::unique_ptr<world::UmbilicalCodes> codes_;
    std::unique_ptr<world::Umbilicals> umbilicals_;
    std::optional<std::vector<world::UmbilicalCodes::Code>> menu_;
    std::optional<Cutscene::LevelChange> travel_;
    bool frozen_ = false;          // arrived by umbilical: no control until the shell opens
    bool poweredChecked_ = false;
    SoundSystem* sound_ = nullptr;
    uint32_t pendingPickup_ = 0;  // a ground item the player is walking to (Loot::GroundItem::id)
    std::unique_ptr<world::ItemCatalog> catalog_;
    std::unique_ptr<world::Inventory> inventory_;
    uint64_t equipRev_ = 0;
    bool wasCinematic_ = false;
    const chk::GizmoRecord* pendingUse_ = nullptr;  // walking up to use it
    const chk::AgentRecord* pendingTalk_ = nullptr;  // an NPC the player is walking up to
    world::Character character_;
    std::unique_ptr<world::StoryFiles> storyFiles_;
    std::unique_ptr<world::Story> story_;
    std::vector<bool> wasDead_;  // per foe, last frame
    std::deque<Message> messages_;
    mutable std::map<std::string, Texture> messageTextures_;
    int screenW_, screenH_;
    double centreX_;
    double camX_ = 0, camY_ = 0;  // map screen position at the centre of the view
    uint64_t startMs_ = 0, elapsedMs_ = 0;  // animation clock, from the first update
    // GPU copies of tile images, by brush, tile id (centre tiles are cut per id).
    mutable std::map<std::pair<uint16_t, uint16_t>, Texture> tiles_;
    // Visible gizmos and agents. The original draws them with a depth
    // buffer; the rebuild orders whole pictures by comparing depths where
    // they overlap (see draw()).
    struct Object {
        double depth;  // the order key against walls, and between pictures that do not overlap
        const chk::GizmoRecord* gizmo;
        const chk::AgentRecord* agent;
        const world::Level::DepthMap* shape = nullptr;  // a gizmo's
        int left = 0, top = 0;  // where a gizmo's shape sits, map screen pixels
    };
    std::vector<Object> objects_;
    std::vector<std::vector<size_t>> behind_;  // per gizmo: the gizmos it hides (they draw first)
    // A picture placed on the map: an agent's pose, or a whole gizmo.
    struct Sprite {
        const render::RgbaImage* image = nullptr;
        int left = 0, top = 0;
    };
    Sprite agentSprite(const chk::AgentRecord* a) const;  // nullptr: the player
    // An agent's picture: as its spawner shows it, else as the foes do.
    world::Level::Pose agentPose(const chk::AgentRecord& a) const;
    // +1 if gizmo object `o` is in front of a sprite standing at map depth
    // `depth` where they overlap, -1 if behind, 0 if they do not overlap.
    int compare(const Object& o, const Sprite& s, double depth) const;
    mutable std::vector<size_t> order_;  // the last draw order (objects_ indices; objects_.size() = the player)
    mutable std::map<std::string, Texture> objectTextures_;  // by image path
};

}  // namespace harbinger::app
