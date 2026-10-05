// Uses actual engine startup, campaign logic, inventory and save files.
#define main flare_application_main
#include "../src/main.cpp"
#undef main
#include "../src/Avatar.h"
#include "../src/CampaignManager.h"
#include "../src/EventManager.h"
#include "../src/GameStatePlay.h"
#include "../src/MenuManager.h"
#include "../src/MenuInventory.h"
#include "../src/SharedGameResources.h"
#include "../src/MenuTalker.h"
#include "../src/NPC.h"
#include "../src/MapRenderer.h"
#include "../src/PowerManager.h"
#include "../src/LootManager.h"
#include "../src/Hazard.h"
#include "../src/EntityManager.h"
#include <cmath>
#include <cassert>
#include <iostream>

static bool success(const std::string& result) { return result.find("\"ok\":true") != std::string::npos; }
static int itemCount(ItemID item) {
    return menu->inv->inventory[MenuInventory::CARRIED].count(item) + menu->inv->inventory[MenuInventory::EQUIPMENT].count(item);
}
static void setEventStatus(const std::string& name) {
    Event event;
    event.components.push_back(EventComponent());
    EventComponent& status = event.components.back();
    status.type = EventComponent::SET_STATUS;
    status.s = name;
    status.status = camp->registerStatus(name);
    eventm->executeEvent(event);
}
static void startTrial() {
    camp->unsetStatus(camp->registerStatus("dnf_room_1_clear"));
    camp->unsetStatus(camp->registerStatus("dnf_room_2_clear"));
    camp->unsetStatus(camp->registerStatus("dnf_room_3_clear"));
    setEventStatus("dnf_normal");
}
static bool specialVisible() {
    return camp->villageQuestRequest("list", "").find("special_reward") != std::string::npos;
}
static void checkTrialLoot() {
    StatBlock boss;
    boss.load("enemies/dnf_guardian.txt");
    assert(boss.loot_count.x == 2 && boss.loot_count.y == 3);
    const int levels[] = {1, 9, 10, 14, 15, 19, 20};
    const char* tables[] = {"loot/dnf_boss.txt", "loot/dnf_clear.txt"};
    pc->stats.current[Stats::ITEM_FIND] = 0;
    srand(17431);
    for (size_t t = 0; t < 2; ++t) {
        for (size_t l = 0; l < 7; ++l) {
            pc->stats.level = levels[l];
            int set_drops = 0;
            int bow_drops = 0;
            int sun_armor_drops = 0;
            for (int attempt = 0; attempt < 1000; ++attempt) {
                std::string filename = tables[t];
                EventComponent table;
                std::vector<EventComponent> entries;
                loot->parseLoot(filename, &table, &entries);
                assert(!entries.empty());
                std::vector<ItemStack> drops;
                loot->checkLoot(entries, NULL, &drops);
                int equipment = 0;
                for (size_t i = 0; i < drops.size(); ++i) {
                    if (drops[i].item == eset->misc.currency_id) continue;
                    ++equipment;
                    const ItemID id = drops[i].item;
                    if (id == 1702) { ++bow_drops; assert(levels[l] == 20); }
                    if (id >= 1703 && id <= 1707) {
                        ++sun_armor_drops; ++set_drops;
                        assert(levels[l] == 20 && items->items[id]->set == 14);
                    }
                    if (id >= 1600 && id <= 1653) {
                        ++set_drops;
                        const ItemID first = levels[l] >= 20 ? 1636 : levels[l] >= 15 ? 1618 : 1600;
                        assert(levels[l] >= 10 && id >= first && id < first + 18);
                    }
                }
                assert(equipment == 1); // each roll must actually guarantee gear
            }
            if (levels[l] < 10) assert(set_drops == 0);
            else assert(set_drops > 520 && set_drops < 680);
            if (levels[l] == 20) assert(bow_drops > 15 && bow_drops < 85);
            else assert(bow_drops == 0);
            if (levels[l] == 20) assert(sun_armor_drops > 75 && sun_armor_drops < 200);
            else assert(sun_armor_drops == 0);
            std::cout << tables[t] << " level=" << levels[l] << " set_drops=" << set_drops << "/1000 bow_drops=" << bow_drops << "/1000" << std::endl;
        }
    }
    // Fixed currency is emitted once per boss, while all 3 equipment rolls remain guaranteed.
    pc->stats.level = 20;
    std::string filename = "loot/dnf_boss.txt";
    EventComponent table;
    std::vector<EventComponent> entries;
    loot->parseLoot(filename, &table, &entries);
    std::vector<ItemStack> drops;
    for (int roll = 0; roll < 3; ++roll) loot->checkLoot(entries, NULL, &drops);
    assert(drops.size() == 4);
}
static void discardQueuedHazards() {
    while (!powers->hazards.empty()) { delete powers->hazards.front(); powers->hazards.pop(); }
}
static Hazard* shootTestArrow() {
    discardQueuedHazards();
    assert(powers->activate(44, &pc->stats, pc->stats.pos, FPoint(15, 8)));
    assert(!powers->hazards.empty());
    Hazard* arrow = powers->hazards.front(); powers->hazards.pop();
    discardQueuedHazards();
    arrow->crit_chance = 0;
    for (size_t i = 0; i < arrow->damage.size(); ++i) arrow->damage[i].min = arrow->damage[i].max = 0;
    return arrow;
}
static void checkSunBow() {
    assert(items->isValid(1702) && items->items[1702]->power == 790);
    assert(powers->isValid(790) && powers->isValid(791));
    assert(powers->powers[790]->basic_true_hp_percent == 5);
    assert(powers->powers[790]->basic_meteor_chance == 10);
    pc->stats.level = 20;
    pc->stats.perfect_accuracy = true;
    pc->stats.character_class = "Scout";
    pc->stats.powers_passive.clear();
    menu->inv->inventory[MenuInventory::EQUIPMENT].clear();
    const int slot = menu->inv->getEquipSlotFromItem(1702, false);
    assert(slot >= 0);
    ItemStack bow; bow.item = 1702; bow.quantity = 1;
    assert(menu->inv->add(bow, MenuInventory::EQUIPMENT, slot, false, false));
    assert(items->items[1702]->gfx_hero == "sunbow_hero");
    assert(pc->getGfxFromType("main") == "sunbow_hero");
    const std::string original_base = pc->stats.gfx_base;
    const char* bases[] = {"male", "female", "female_dark", "chibi"};
    render_device->blankScreen();
    for (size_t base = 0; base < 4; ++base) {
        pc->stats.gfx_base = bases[base];
        pc->loadAnimations();
        const char* states[] = {"stance", "run", "shoot"};
        for (size_t state = 0; state < 3; ++state) {
            pc->setAnimation(states[state]);
            for (unsigned direction = 0; direction < 8; ++direction) {
                pc->stats.direction = static_cast<unsigned char>(direction);
                std::vector<Renderable> renders;
                pc->addRenders(renders);
                assert(!renders.empty() && renders[0].image);
                if (base == 0 && state == 0) {
                    Rect dest;
                    dest.x = 180 + (direction % 4) * 140 - renders[0].offset.x;
                    dest.y = 200 + (direction / 4) * 160 - renders[0].offset.y;
                    render_device->render(renders[0], dest);
                }
            }
        }
    }
    render_device->commitFrame();
    SDL_Renderer* bow_renderer = NULL;
    for (Uint32 id = 1; id <= 16 && !bow_renderer; ++id) {
        SDL_Window* window = SDL_GetWindowFromID(id);
        if (window) bow_renderer = SDL_GetRenderer(window);
    }
    assert(bow_renderer);
    int art_width, art_height;
    SDL_GetRendererOutputSize(bow_renderer, &art_width, &art_height);
    SDL_Surface* bow_image = SDL_CreateRGBSurfaceWithFormat(0, art_width, art_height, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(SDL_RenderReadPixels(bow_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, bow_image->pixels, bow_image->pitch) == 0);
    assert(SDL_SaveBMP(bow_image, "sunbow-equipped.bmp") == 0);
    SDL_FreeSurface(bow_image);
    pc->stats.gfx_base = original_base;
    pc->setAnimation("stance");
    pc->loadAnimations();
    pc->stats.powers_list_items.clear();
    menu->inv->applyItemStats();
    Hazard* arrow = shootTestArrow();
    assert(arrow->basic_true_hp_percent == 5 && arrow->basic_meteor_chance == 10 && arrow->basic_meteor_power == 791);
    Entity enemy;
    enemy.stats.load("enemies/lvl1_goblin_scamp.txt");
    enemy.loadAnimations();
    enemy.stats.pos = FPoint(15, 8);
    enemy.stats.current[Stats::HP_MAX] = 1000;
    enemy.stats.current[Stats::ABS_MIN] = enemy.stats.current[Stats::ABS_MAX] = 10000;
    enemy.stats.current[Stats::RETURN_DAMAGE] = 0;
    enemy.stats.hp = 1000;
    enemy.stats.cur_state = StatBlock::ENTITY_STANCE;
    arrow->basic_meteor_chance = 0;
    Hazard baseline(*arrow); baseline.basic_true_hp_percent = 0; baseline.basic_meteor_power = 0;
    enemy.takeHit(baseline);
    const float ordinary_hp = enemy.stats.hp;
    enemy.stats.hp = 1000;
    enemy.takeHit(*arrow);
    assert(std::fabs(ordinary_hp - enemy.stats.hp - 50) < 0.01f); // armor cannot reduce true damage
    delete arrow;
    discardQueuedHazards();

    // Force the probability roll for deterministic end-to-end meteor checks.
    arrow = shootTestArrow(); arrow->basic_meteor_chance = 100;
    enemy.stats.hp = 1000;
    enemy.takeHit(*arrow);
    assert(arrow->basic_meteor_rolled && powers->hazards.size() == 1);
    Hazard* meteor = powers->hazards.front(); powers->hazards.pop();
    assert(meteor->power_index == 791 && meteor->locked_target == &enemy);
    Entity bystander;
    assert(!bystander.takeHit(*meteor));
    assert(meteor->basic_true_hp_percent == 0 && meteor->basic_meteor_power == 0);
    // Piercing the next enemy does not create another meteor from this arrow.
    enemy.stats.hp = 1000;
    enemy.takeHit(*arrow);
    assert(powers->hazards.empty());
    delete arrow;
    enemy.stats.hp = 800;
    enemy.stats.current[Stats::ABS_MIN] = enemy.stats.current[Stats::ABS_MAX] = 0;
    const size_t resist_index = Stats::COUNT + eset->damage_types.indexToResist(meteor->power->base_damage);
    enemy.stats.current[resist_index] = 0;
    assert(!meteor->isDangerousNow()); // the fall animation delays damage until impact
    for (int frame = 0; frame < 60 && !meteor->isDangerousNow(); ++frame) meteor->logic();
    assert(meteor->isDangerousNow());
    enemy.takeHit(*meteor);
    assert(std::fabs(enemy.stats.hp - 760) < 0.01f); // 5% of landing HP, not maximum or firing HP
    enemy.stats.hp = 800;
    enemy.stats.current[resist_index] = 50;
    enemy.takeHit(*meteor);
    assert(std::fabs(enemy.stats.hp - 780) < 0.01f); // magic resistance reduces meteor damage
    assert(powers->hazards.empty()); // no recursive proc
    delete meteor;

    // Skills cannot gain the bow's basic-attack effects.
    pc->stats.mp = 1000;
    assert(powers->activate(770, &pc->stats, pc->stats.pos, enemy.stats.pos));
    assert(!powers->hazards.empty());
    while (!powers->hazards.empty()) {
        Hazard* skill = powers->hazards.front(); powers->hazards.pop();
        assert(skill->basic_true_hp_percent == 0 && skill->basic_meteor_power == 0);
        delete skill;
    }
    assert(powers->activate(779, &pc->stats, pc->stats.pos, enemy.stats.pos));
    while (!powers->hazards.empty()) {
        Hazard* skill = powers->hazards.front(); powers->hazards.pop();
        assert(skill->basic_true_hp_percent == 0 && skill->basic_meteor_power == 0);
        delete skill;
    }
    // An already-fired arrow retains its effects; the next arrow after unequipping does not.
    arrow = shootTestArrow();
    menu->inv->inventory[MenuInventory::EQUIPMENT].storage[slot].clear();
    assert(pc->getGfxFromType("main") != "sunbow_hero");
    pc->loadAnimations();
    pc->stats.powers_list_items.clear(); menu->inv->applyItemStats();
    Hazard* unequipped = shootTestArrow();
    assert(arrow->basic_true_hp_percent == 5 && unequipped->basic_true_hp_percent == 0);
    delete arrow; delete unequipped;
    std::cout << "PASS: Sun Bow equipment, true damage, meteor landing/resistance, no skill/recursive procs and unequip" << std::endl;
}
static void equipSunPieces(int count) {
    menu->inv->inventory[MenuInventory::EQUIPMENT].clear();
    pc->stats.effects.clearEffects();
    pc->stats.level = 20;
    pc->stats.character_class = "Scout";
    for (int i = 0; i < count; ++i) {
        ItemStack item(1702 + i, 1);
        int slot = menu->inv->getEquipSlotFromItem(item.item, false);
        assert(slot >= 0 && menu->inv->add(item, MenuInventory::EQUIPMENT, slot, false, false));
    }
    menu->inv->applyEquipment();
    pc->stats.effects.logic();
}
static void prepareSunEnemy(Entity& enemy, FPoint pos) {
    enemy.stats.load("enemies/lvl1_goblin_scamp.txt");
    enemy.loadAnimations();
    enemy.stats.pos = pos;
    enemy.stats.hp = enemy.stats.current[Stats::HP_MAX] = 1000;
    enemy.stats.current[Stats::ABS_MIN] = enemy.stats.current[Stats::ABS_MAX] = 0;
    enemy.stats.current[Stats::RETURN_DAMAGE] = 0;
    const size_t resist = Stats::COUNT + eset->damage_types.indexToResist(powers->powers[793]->base_damage);
    enemy.stats.current[resist] = 0;
}
static void checkSunSet() {
    assert(items->isValidSet(14) && items->item_sets[14]->items.size() == 6);
    for (ItemID id = 1702; id <= 1707; ++id)
        assert(items->isValid(id) && items->items[id]->set == 14 && items->items[id]->requires_level.get() == 20);
    assert(powers->powers[792]->basic_meteor_chance == 20);
    assert(powers->powers[792]->basic_execute_chance == 1);
    assert(powers->powers[792]->basic_storm_chance == 5);
    for (int count = 1; count <= 6; ++count) {
        equipSunPieces(count);
        bool full = std::find(pc->stats.powers_list_items.begin(), pc->stats.powers_list_items.end(), 792) != pc->stats.powers_list_items.end();
        assert(full == (count == 6));
        assert(pc->stats.effects.getAttackSpeed("shoot") == (count >= 4 ? 110 : 100));
        assert(pc->stats.effects.speed == (count == 6 ? 130 : 100));
        Hazard* arrow = shootTestArrow();
        assert(arrow->basic_meteor_chance == (count == 6 ? 20 : 10));
        assert(arrow->basic_execute_chance == (count == 6 ? 1 : 0));
        assert(arrow->basic_storm_chance == (count == 6 ? 5 : 0));
        delete arrow;
    }
    Entity enemy, nearby, distant, ally;
    prepareSunEnemy(enemy, FPoint(15, 8));
    prepareSunEnemy(nearby, FPoint(16.5f, 8.0f));
    prepareSunEnemy(distant, FPoint(1000, 1000));
    prepareSunEnemy(ally, FPoint(15, 9)); ally.stats.hero_ally = true;
    Hazard* arrow = shootTestArrow();
    arrow->basic_meteor_chance = 100;
    arrow->basic_execute_chance = arrow->basic_storm_chance = 0;
    assert(enemy.takeHit(*arrow));
    assert(powers->hazards.size() == 1);
    Hazard* meteor = powers->hazards.front(); powers->hazards.pop();
    assert(meteor->power_index == 793 && !meteor->locked_target && meteor->power->multitarget);
    assert(meteor->power->radius == 2.5f);
    assert(Utils::isWithinRadius(meteor->pos, meteor->power->radius, nearby.stats.pos));
    assert(!Utils::isWithinRadius(meteor->pos, 1.0f, nearby.stats.pos));
    for (int frame = 0; frame < 60 && !meteor->isDangerousNow(); ++frame) meteor->logic();
    assert(meteor->isDangerousNow());
    enemy.stats.hp = nearby.stats.hp = 1000;
    enemy.takeHit(*meteor); nearby.takeHit(*meteor);
    assert(enemy.stats.hp == 950 && nearby.stats.hp == 950);
    assert(powers->hazards.empty());
    delete meteor; delete arrow;

    // Storm targets every on-screen hostile once, ignoring allies and distant mobs.
    std::vector<Entity*> original_entities = entitym->entities;
    entitym->entities.clear();
    entitym->entities.push_back(&enemy); entitym->entities.push_back(&nearby);
    entitym->entities.push_back(&distant); entitym->entities.push_back(&ally);
    const FPoint camera = mapr->cam.pos;
    mapr->cam.warpTo(enemy.stats.pos);
    arrow = shootTestArrow();
    arrow->basic_storm_chance = 100;
    arrow->basic_meteor_chance = arrow->basic_execute_chance = 0;
    enemy.stats.hp = nearby.stats.hp = 1000;
    enemy.takeHit(*arrow);
    assert(powers->hazards.size() > 2 && powers->hazards.size() <= 18);
    int targets = 0, visuals = 0;
    while (!powers->hazards.empty()) {
        meteor = powers->hazards.front(); powers->hazards.pop();
        if (meteor->power_index == 795) {
            assert(meteor->power->beacon);
            const float hp = enemy.stats.hp;
            assert(!enemy.takeHit(*meteor) && enemy.stats.hp == hp);
            ++visuals; delete meteor; continue;
        }
        assert(meteor->power_index == 794 && (meteor->locked_target == &enemy || meteor->locked_target == &nearby));
        assert(meteor->basic_execute_chance == 0 && meteor->basic_storm_chance == 0);
        assert(!ally.takeHit(*meteor));
        assert(!distant.takeHit(*meteor));
        for (int frame = 0; frame < 60 && !meteor->isDangerousNow(); ++frame) meteor->logic();
        meteor->locked_target->stats.hp = 1000;
        meteor->locked_target->takeHit(*meteor);
        assert(meteor->locked_target->stats.hp == 950);
        ++targets; delete meteor;
    }
    assert(targets == 2 && visuals > 0 && powers->hazards.empty());
    enemy.takeHit(*arrow);
    assert(powers->hazards.empty()); // no repeat storm from piercing the next target
    delete arrow;
    entitym->entities = original_entities;
    mapr->cam.warpTo(camera);

    Entity boss;
    boss.stats.load("enemies/dnf_guardian.txt"); boss.loadAnimations();
    boss.stats.pos = enemy.stats.pos;
    boss.stats.hp = boss.stats.current[Stats::HP_MAX] = 100000;
    boss.stats.current[Stats::ABS_MIN] = boss.stats.current[Stats::ABS_MAX] = 10000;
    boss.stats.defeat_status = camp->registerStatus("sunset_test_boss_defeated");
    arrow = shootTestArrow();
    arrow->basic_execute_chance = 100;
    arrow->basic_meteor_chance = arrow->basic_storm_chance = 0;
    boss.takeHit(*arrow);
    assert(boss.stats.hp == 0 && boss.stats.effects.triggered_death);
    assert(camp->checkStatus(boss.stats.defeat_status));
    delete arrow;

    assert(powers->activate(770, &pc->stats, pc->stats.pos, enemy.stats.pos));
    while (!powers->hazards.empty()) {
        Hazard* skill = powers->hazards.front(); powers->hazards.pop();
        assert(skill->basic_execute_chance == 0 && skill->basic_storm_chance == 0 && skill->basic_meteor_chance == 0);
        delete skill;
    }
    // Removing one armor piece immediately removes every six-piece effect.
    const int armor_slot = menu->inv->getEquipSlotFromItem(1707, false);
    menu->inv->inventory[MenuInventory::EQUIPMENT].storage[armor_slot].clear();
    menu->inv->applyEquipment(); pc->stats.effects.logic();
    arrow = shootTestArrow();
    assert(arrow->basic_execute_chance == 0 && arrow->basic_storm_chance == 0 && arrow->basic_meteor_chance == 10);
    assert(pc->stats.effects.speed == 100);
    delete arrow;
    std::cout << "PASS: Sun set thresholds, six-piece chance/speed, enlarged AoE, full-screen targeting, boss execution, no recursion and removal" << std::endl;
}
static void checkProgress(const char* id, int expected) {
    std::string report = camp->villageQuestRequest("list", "");
    size_t begin = report.find(std::string("\"id\":\"") + id + "\"");
    assert(begin != std::string::npos);
    size_t end = report.find('}', begin);
    assert(report.substr(begin, end - begin).find("\"current\":" + std::to_string(expected)) != std::string::npos);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    settings = new Settings();
    settings->custom_path_data = argv[1];
    settings->safe_video = true;
    settings->audio = false;
    CmdLineArgs args;
    args.mod_list.push_back("empyrean_campaign");
    init(args);
    GameStatePlay game;
    game.resetGame();
    save_load->loadClass(1);
    save_load->setGameSlot(1);
    pc->stats.gfx_base = "female";
    pc->stats.gfx_head = "head_long";
    pc->stats.gfx_portrait = "images/portraits/female01.png";
    assert(success(camp->villageQuestRequest("list", "")));
    assert(camp->villageQuestRequest("list", "").find("old_story") == std::string::npos);
    assert(camp->villageQuestSummary().find("村长的旧事") == std::string::npos);
    assert(!specialVisible());
    assert(!success(camp->villageQuestRequest("accept", "old_story")));
    camp->setVillageQuestion("村长的过去是什么样的？");
    assert(!success(camp->villageQuestRequest("discover", "past"))); // trust is required
    assert(!success(camp->villageQuestRequest("accept", "invented")));
    assert(!success(camp->villageQuestRequest("submit", "goblins")));
    StatBlock enemy;
    enemy.categories.push_back("goblin");
    camp->notifyVillageKill(enemy);
    assert(success(camp->villageQuestRequest("accept", "goblins")));
    checkProgress("goblins", 0);
    enemy.hero_ally = true;
    camp->notifyVillageKill(enemy);
    checkProgress("goblins", 0);
    enemy.hero_ally = false;
    for (int i = 0; i < 3; ++i) camp->notifyVillageKill(enemy);
    checkProgress("goblins", 3);
    // Actual on-disk save and load round trip, rather than just copying counters.
    save_load->saveGame();
    camp->resetAllStatuses();
    checkProgress("goblins", 0);
    save_load->loadGame();
    checkProgress("goblins", 3);
    for (int i = 0; i < 2; ++i) camp->notifyVillageKill(enemy);
    checkProgress("goblins", 5);
    int gold = menu->inv->inventory[MenuInventory::CARRIED].count(eset->misc.currency_id);
    unsigned long xp = pc->stats.xp;
    assert(success(camp->villageQuestRequest("submit", "goblins")));
    assert(menu->inv->inventory[MenuInventory::CARRIED].count(eset->misc.currency_id) == gold + 100);
    assert(pc->stats.xp >= xp + 150);
    gold = menu->inv->inventory[MenuInventory::CARRIED].count(eset->misc.currency_id); xp = pc->stats.xp;
    assert(!success(camp->villageQuestRequest("submit", "goblins")));
    assert(menu->inv->inventory[MenuInventory::CARRIED].count(eset->misc.currency_id) == gold && pc->stats.xp == xp);

    camp->setVillageQuestion("今天有什么普通任务？");
    assert(!success(camp->villageQuestRequest("discover", "past"))); // model cannot invent the topic
    camp->setVillageQuestion("村长，聊聊过去的失踪冒险者吧。");
    assert(!success(camp->villageQuestRequest("discover", "invented")));
    assert(success(camp->villageQuestRequest("discover", "past")));
    assert(camp->villageQuestRequest("list", "").find("old_story") != std::string::npos);
    assert(itemCount(1150) == 0);
    mapr->load("maps/perdition_harbor_cave.txt");
    Event* chest = NULL;
    for (size_t i = 0; i < mapr->events.size(); ++i)
        if (mapr->events[i].location.x == 15 && mapr->events[i].location.y == 8 &&
            mapr->events[i].activate_type == Event::ACTIVATE_ON_TRIGGER) chest = &mapr->events[i];
    assert(chest);
    assert(!eventm->isActive(*chest));
    assert(success(camp->villageQuestRequest("accept", "old_story")));
    assert(!success(camp->villageQuestRequest("submit", "old_story")));
    assert(eventm->isActive(*chest));
    eventm->executeEvent(*chest);
    assert(itemCount(1150) == 1);
    assert(!eventm->isActive(*chest));
    assert(success(camp->villageQuestRequest("submit", "old_story")));
    assert(itemCount(1150) == 0 && itemCount(1700) == 1);
    save_load->saveGame();
    camp->resetAllStatuses();
    save_load->loadGame();
    assert(!success(camp->villageQuestRequest("submit", "old_story")));
    assert(itemCount(1700) == 1);

    assert(success(camp->villageQuestRequest("accept", "supplies")));
    ItemStack aloe; aloe.item = 751; aloe.quantity = 2;
    camp->rewardItem(aloe);
    assert(!success(camp->villageQuestRequest("submit", "supplies")));
    assert(menu->inv->inventory[MenuInventory::CARRIED].count(751) == 2);
    aloe.quantity = 1; camp->rewardItem(aloe);
    assert(success(camp->villageQuestRequest("submit", "supplies")));
    assert(menu->inv->inventory[MenuInventory::CARRIED].count(751) == 0);

    Event cleared;
    cleared.components.push_back(EventComponent());
    EventComponent& status = cleared.components.back();
    status.type = EventComponent::SET_STATUS;
    status.s = "dnf_room_3_clear";
    status.status = camp->registerStatus(status.s);
    camp->setStatus(status.status); // completion from an earlier run/save
    assert(success(camp->villageQuestRequest("accept", "trial")));
    eventm->executeEvent(cleared);
    checkProgress("trial", 0);
    camp->unsetStatus(status.status); // entering a new run clears the old status
    eventm->executeEvent(cleared);
    checkProgress("trial", 1);
    assert(success(camp->villageQuestRequest("submit", "trial")));
    gold = menu->inv->inventory[MenuInventory::CARRIED].count(eset->misc.currency_id);
    save_load->saveGame();
    camp->resetAllStatuses();
    save_load->loadGame();
    assert(!success(camp->villageQuestRequest("submit", "trial")));
    assert(menu->inv->inventory[MenuInventory::CARRIED].count(eset->misc.currency_id) == gold);

    // Special rewards stay hidden until an accepted, ordered, undamaged run.
    assert(!specialVisible());
    startTrial();
    setEventStatus("dnf_room_3_clear"); // skipping two floors is not flawless
    assert(!specialVisible());
    startTrial();
    setEventStatus("dnf_room_1_clear");
    setEventStatus("dnf_room_2_clear");
    pc->stats.hp = 100;
    pc->stats.takeDamage(1, false, Power::SOURCE_TYPE_NEUTRAL);
    pc->stats.hp = 100; // healing cannot erase damage history
    save_load->saveGame();
    camp->resetAllStatuses();
    save_load->loadGame();
    setEventStatus("dnf_room_3_clear");
    assert(!specialVisible());
    startTrial();
    setEventStatus("dnf_room_1_clear");
    setEventStatus("dnf_room_2_clear");
    camp->notifyVillageMap("maps/perdition_harbor.txt"); // leaving forfeits this run
    setEventStatus("dnf_room_3_clear");
    assert(!specialVisible());
    startTrial();
    pc->stats.takeDamage(0, false, Power::SOURCE_TYPE_NEUTRAL);
    setEventStatus("dnf_room_1_clear");
    setEventStatus("dnf_room_2_clear");
    save_load->saveGame();
    camp->resetAllStatuses();
    save_load->loadGame();
    setEventStatus("dnf_room_3_clear");
    assert(specialVisible());
    assert(itemCount(1701) == 0);
    gold = itemCount(eset->misc.currency_id); xp = pc->stats.xp;
    assert(success(camp->villageQuestRequest("submit", "trial"))); // works for already-claimed normal quest
    assert(itemCount(1701) == 1);
    assert(itemCount(eset->misc.currency_id) == gold && pc->stats.xp == xp);
    save_load->saveGame();
    camp->resetAllStatuses();
    save_load->loadGame();
    startTrial();
    setEventStatus("dnf_room_1_clear");
    setEventStatus("dnf_room_2_clear");
    setEventStatus("dnf_room_3_clear");
    assert(!success(camp->villageQuestRequest("submit", "trial")));
    assert(itemCount(1701) == 1);
    // Capture the actual quest panel, without a model connection or real save.
    camp->resetAllStatuses();
    setenv("FLARE_AI_BRIDGE", "/tmp/flare-test-no-model.py", 1);
    Entity entity;
    NPC chief(entity);
    assert(chief.load("npcs/village_chief.txt"));
    menu->talker->setNPC(&chief);
    menu->talker->chooseDialogNode(-1);
    render_device->blankScreen();
    menu->talker->render();
    render_device->commitFrame();
    SDL_Renderer* renderer = NULL;
    for (Uint32 id = 1; id <= 16 && !renderer; ++id) {
        SDL_Window* window = SDL_GetWindowFromID(id);
        if (window) renderer = SDL_GetRenderer(window);
    }
    assert(renderer);
    {
        int width, height;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        SDL_Surface* screenshot = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
        assert(screenshot);
        assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, screenshot->pixels, screenshot->pitch) == 0);
        assert(SDL_SaveBMP(screenshot, "village-chief-quests.bmp") == 0);
        SDL_FreeSurface(screenshot);
    }
    menu->talker->setNPC(NULL);
    checkTrialLoot();
    checkSunBow();
    checkSunSet();
    // Speed bonuses are engine percentages: 100 is normal, not an additive bonus.
    for (ItemSetID set = 5; set <= 13; ++set) {
        pc->stats.effects.clearEffects();
        std::vector<ItemSetID> sets(1, set);
        std::vector<int> counts(1, 4);
        menu->inv->applyItemSetBonuses(sets, counts);
        pc->stats.effects.logic();
        const int tier = static_cast<int>((set - 5) / 3);
        const float attack_speed[] = {106, 110, 114};
        const float move_speed[] = {135, 165, 200};
        if ((set - 5) % 3 == 0) assert(pc->stats.effects.getAttackSpeed("swing") == attack_speed[tier]);
        if ((set - 5) % 3 == 1) assert(pc->stats.effects.speed == move_speed[tier]);
    }
    std::cout << "PASS: quests, hidden discovery, actual chest event, item rewards, damage/retreat/order checks, flawless rewards and save/load" << std::endl;
    return 0;
}
