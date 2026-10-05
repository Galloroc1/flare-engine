/*
Copyright © 2011-2012 Clint Bellanger
Copyright © 2012 Stefan Beller
Copyright © 2013 Henrik Andersson
Copyright © 2012-2016 Justin Jacobs

This file is part of FLARE.

FLARE is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

FLARE is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
FLARE.  If not, see http://www.gnu.org/licenses/
*/

/**
 * class CampaignManager
 *
 * Contains data for story mode
 */

#include "Avatar.h"
#include "CampaignManager.h"
#include "CommonIncludes.h"
#include "EngineSettings.h"
#include "EventManager.h"
#include "FileParser.h"
#include <sstream>
#include "MapRenderer.h"
#include "Menu.h"
#include "MenuManager.h"
#include "MenuInventory.h"
#include "MessageEngine.h"
#include "SharedGameResources.h"
#include "SharedResources.h"
#include "StatBlock.h"
#include "UtilsMath.h"
#include "UtilsParsing.h"

CampaignManager::CampaignManager()
	: bonus_xp(0.0)
	, village_quests_loaded(false)
	, random_status(0) {
}

StatusID CampaignManager::registerStatus(const std::string& s) {
	if (s.empty())
		return 0;

	StatusID new_id = Utils::hashString(s);

	// check if this status was already registered
	StatusMap::iterator it;
	it = status.find(new_id);
	if (it != status.end())
		return it->first;

	// register a new status
	status[new_id].first = false;
	status[new_id].second = s;
	return new_id;
}

/**
 * Take the savefile campaign= and convert to status array
 */
void CampaignManager::setAll(const std::string& s) {
	std::string str = s + ',';
	std::string token;
	while (!str.empty()) {
		token = Parse::popFirstString(str);
		if (!token.empty())
			setStatus(registerStatus(token));
	}
}

/**
 * Convert status array to savefile campaign= (status csv)
 */
std::string CampaignManager::getAll() {
	std::string output("");

	StatusMap::iterator it;
	for (it = status.begin(); it != status.end(); ++it) {
		if (it->second.first)
			output += it->second.second;

		StatusMap::iterator temp = it;
		++temp;
		if (temp != status.end() && temp->second.first) {
			output += ',';
		}
	}
	return output;
}

bool CampaignManager::checkStatus(const StatusID s) {
	StatusMap::iterator it;
	it = status.find(s);
	if (it != status.end() && it->second.first)
		return true;

	return false;
}

void CampaignManager::setStatus(const StatusID s) {
	// if it's already set, don't set it again
	if (checkStatus(s)) return;

	status[s].first = true;
	pc->stats.check_title = true;
}

void CampaignManager::unsetStatus(const StatusID s) {
	// if it's already unset, don't unset it again
	if (!checkStatus(s)) return;

	status[s].first = false;
	pc->stats.check_title = true;
}

void CampaignManager::resetAllStatuses() {
	StatusMap::iterator it;
	for (it = status.begin(); it != status.end(); ++it) {
		it->second.first = false;
	}
}

void CampaignManager::getSetStatusStrings(std::vector<std::string>& status_strings) {
	StatusMap::iterator it;
	for (it = status.begin(); it != status.end(); ++it) {
		if (it->second.first)
			status_strings.push_back(it->second.second);
	}
}

bool CampaignManager::checkCurrency(int quantity) {
	return menu->inv->inventory[MenuInventory::CARRIED].contain(eset->misc.currency_id, quantity);
}

bool CampaignManager::checkItem(ItemStack istack) {
	int carried = menu->inv->inventory[MenuInventory::CARRIED].count(istack.item);
	int equipped = menu->inv->inventory[MenuInventory::EQUIPMENT].count(istack.item);
	return (carried + equipped >= istack.quantity);
}

void CampaignManager::removeCurrency(int quantity) {
	int max_amount = std::min(quantity, menu->inv->currency);

	if (max_amount > 0) {
		menu->inv->removeCurrency(max_amount);
		pc->logMsg(msg->getv("%d %s removed.", max_amount, eset->loot.currency.c_str()), Avatar::MSG_UNIQUE);
		items->playSound(eset->misc.currency_id);
	}
}

void CampaignManager::removeItem(ItemStack istack) {
	if (istack.empty())
		return;

	if (istack.item == eset->misc.currency_id) {
		removeCurrency(istack.quantity);
		return;
	}

	int item_count = menu->inv->inventory[MenuInventory::CARRIED].count(istack.item) + menu->inv->inventory[MenuInventory::EQUIPMENT].count(istack.item);
	int max_amount = std::min(item_count, istack.quantity);

	if (menu->inv->remove(istack.item, max_amount)) {
		if (max_amount > 1)
			pc->logMsg(msg->getv("%s x%d removed.", items->getItemName(istack.item).c_str(), max_amount), Avatar::MSG_UNIQUE);
		else if (max_amount == 1)
			pc->logMsg(msg->getv("%s removed.", items->getItemName(istack.item).c_str()), Avatar::MSG_UNIQUE);

		if (max_amount > 0)
			items->playSound(istack.item);
	}
}

void CampaignManager::rewardItem(ItemStack istack) {
	if (istack.empty())
		return;

	menu->inv->add(istack, MenuInventory::CARRIED, ItemStorage::NO_SLOT, MenuInventory::ADD_PLAY_SOUND, MenuInventory::ADD_AUTO_EQUIP);

	if (istack.item == eset->misc.currency_id) {
		pc->logMsg(msg->getv("You receive %d %s.", istack.quantity, eset->loot.currency.c_str()), Avatar::MSG_UNIQUE);
	}
	else {
		if (istack.quantity > 1)
			pc->logMsg(msg->getv("You receive %s x%d.", items->getItemName(istack.item).c_str(), istack.quantity), Avatar::MSG_UNIQUE);
		else if (istack.quantity == 1)
			pc->logMsg(msg->getv("You receive %s.", items->getItemName(istack.item).c_str()), Avatar::MSG_UNIQUE);
	}
}

void CampaignManager::rewardCurrency(int amount) {
	ItemStack stack;
	stack.item = eset->misc.currency_id;
	stack.quantity = amount;

	rewardItem(stack);
}

void CampaignManager::rewardXP(float amount, bool show_message) {
	if (pc->block_xp_gain)
		return;

	bonus_xp += (amount * (100.0f + static_cast<float>(pc->stats.get(Stats::XP_GAIN)))) / 100.0f;

	int whole_xp = static_cast<int>(bonus_xp);
	pc->stats.addXP(whole_xp);
	bonus_xp -= static_cast<float>(whole_xp); // remainder

	pc->stats.refresh_stats = true;

	if (show_message)
		pc->logMsg(msg->getv("You receive %d XP.", static_cast<int>(amount)), Avatar::MSG_UNIQUE);
}

void CampaignManager::restoreHPMP(const std::string& s) {
	std::string restore_str = s;
	std::string restore_mode = Parse::popFirstString(restore_str);

	while (!restore_mode.empty()) {
		if (restore_mode == "hp") {
			pc->stats.hp = pc->stats.get(Stats::HP_MAX);
			pc->logMsg(msg->get("HP restored."), Avatar::MSG_UNIQUE);
		}
		else if (restore_mode == "mp") {
			pc->stats.mp = pc->stats.get(Stats::MP_MAX);
			pc->logMsg(msg->get("MP restored."), Avatar::MSG_UNIQUE);
		}
		else if (restore_mode == "hpmp") {
			pc->stats.hp = pc->stats.get(Stats::HP_MAX);
			pc->stats.mp = pc->stats.get(Stats::MP_MAX);
			pc->logMsg(msg->get("HP and MP restored."), Avatar::MSG_UNIQUE);
		}
		else if (restore_mode == "status") {
			pc->stats.effects.clearNegativeEffects(Effect::RESIST_ALL);
			pc->logMsg(msg->get("Negative effects removed."), Avatar::MSG_UNIQUE);
		}
		else if (restore_mode == "all") {
			pc->stats.hp = pc->stats.get(Stats::HP_MAX);
			pc->stats.mp = pc->stats.get(Stats::MP_MAX);
			pc->stats.effects.clearNegativeEffects(Effect::RESIST_ALL);
			pc->logMsg(msg->get("HP and MP restored, negative effects removed"), Avatar::MSG_UNIQUE);

			for (size_t i = 0; i < eset->resource_stats.list.size(); ++i) {
				pc->stats.resource_stats[i] = pc->stats.getResourceStat(i, EngineSettings::ResourceStats::STAT_BASE);
				pc->logMsg(eset->resource_stats.list[i].text_log_restore, Avatar::MSG_UNIQUE);
			}
		}
		else {
			for (size_t i = 0; i < eset->resource_stats.list.size(); ++i) {
				if (restore_mode == eset->resource_stats.list[i].ids[EngineSettings::ResourceStats::STAT_BASE]) {
					pc->stats.resource_stats[i] = pc->stats.getResourceStat(i, EngineSettings::ResourceStats::STAT_BASE);
					pc->logMsg(eset->resource_stats.list[i].text_log_restore, Avatar::MSG_UNIQUE);
				}
			}
		}

		restore_mode = Parse::popFirstString(restore_str);
	}
}

bool CampaignManager::checkAllRequirements(const EventComponent& ec) {
	if (ec.type == EventComponent::REQUIRES_STATUS) {
		if (checkStatus(ec.status))
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_NOT_STATUS) {
		if (!checkStatus(ec.status))
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_CURRENCY) {
		if (checkCurrency(ec.data[0].Int))
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_NOT_CURRENCY) {
		if (!checkCurrency(ec.data[0].Int))
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_ITEM) {
		if (checkItem(ItemStack(ec.id, ec.data[0].Int)))
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_NOT_ITEM) {
		if (!checkItem(ItemStack(ec.id, ec.data[0].Int)))
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_LEVEL) {
		if (pc->stats.level >= ec.data[0].Int)
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_NOT_LEVEL) {
		if (pc->stats.level < ec.data[0].Int)
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_CLASS) {
		if (pc->stats.character_class == ec.s)
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_NOT_CLASS) {
		if (pc->stats.character_class != ec.s)
			return true;
	}
	else if (ec.type == EventComponent::REQUIRES_TILE) {
		size_t index = static_cast<size_t>(distance(mapr->layernames_hashed.begin(), find(mapr->layernames_hashed.begin(), mapr->layernames_hashed.end(), ec.id)));
		if (mapr && index < mapr->layers.size() && ec.data[0].Int >= 0 && ec.data[0].Int < mapr->w && ec.data[1].Int >= 0 && ec.data[1].Int < mapr->h)
			if (mapr->layers[index][ec.data[0].Int][ec.data[1].Int] == static_cast<unsigned short>(ec.data[2].Int))
				return true;
	}
	else if (ec.type == EventComponent::REQUIRES_NOT_TILE) {
		size_t index = static_cast<size_t>(distance(mapr->layernames_hashed.begin(), find(mapr->layernames_hashed.begin(), mapr->layernames_hashed.end(), ec.id)));
		if (mapr && index < mapr->layers.size() && ec.data[0].Int >= 0 && ec.data[0].Int < mapr->w && ec.data[1].Int >= 0 && ec.data[1].Int < mapr->h)
			if (mapr->layers[index][ec.data[0].Int][ec.data[1].Int] != static_cast<unsigned short>(ec.data[2].Int))
				return true;
	}
	else {
		// Event component is not a requirement check
		// treat it as if the "requirement" was met
		return true;
	}

	// requirement check failed
	return false;
}

bool CampaignManager::checkRequirementsInVector(const std::vector<EventComponent>& ec_vec) {
	for (size_t i = 0; i < ec_vec.size(); ++i) {
		if (!checkAllRequirements(ec_vec[i]))
			return false;
	}

	return true;
}

void CampaignManager::randomStatusAppend(const StatusID s) {
	if (std::find(random_status_pool.begin(), random_status_pool.end(), s) == random_status_pool.end()) {
		if (random_status_pool.empty())
			random_status = s;

		random_status_pool.push_back(s);
	}
}

void CampaignManager::randomStatusClear() {
	random_status_pool.clear();
	random_status = 0;
}

void CampaignManager::randomStatusRoll() {
	if (random_status_pool.empty())
		return;

	random_status = random_status_pool[Math::randBetween(0, static_cast<int>(random_status_pool.size()) - 1)];
}

void CampaignManager::randomStatusSet() {
	if (random_status_pool.empty())
		return;

	setStatus(random_status);
}

void CampaignManager::randomStatusUnset() {
	if (random_status_pool.empty())
		return;

	unsetStatus(random_status);
}

CampaignManager::~CampaignManager() {
	Utils::logInfo("Cleaning up: CampaignManager");
}

namespace {
std::string questJsonString(const std::string& value) {
    std::string result = "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c >= 32) result += c;
    }
    return result + "\"";
}
}

void CampaignManager::loadVillageQuests() {
    if (village_quests_loaded) return;
    village_quests_loaded = true;
    FileParser file;
    if (!file.open("engine/village_quests.txt", FileParser::MOD_FILE, FileParser::ERROR_NONE)) return;
    while (file.next()) {
        if (file.section != "quest") continue;
        if (file.new_section) village_quests.push_back(VillageQuest());
        VillageQuest& q = village_quests.back();
        if (file.key == "id") q.id = file.val;
        else if (file.key == "title") q.title = file.val;
        else if (file.key == "goal") q.goal = file.val;
        else if (file.key == "type") q.type = file.val;
        else if (file.key == "target") q.target = std::max(1, Parse::toInt(file.val));
        else if (file.key == "item") q.item = Parse::toInt(file.val);
        else if (file.key == "gold") q.gold = std::max(0, Parse::toInt(file.val));
        else if (file.key == "xp") q.xp = std::max(0, Parse::toInt(file.val));
        else if (file.key == "hidden") q.hidden = Parse::toBool(file.val);
        else if (file.key == "clue") q.clue = file.val;
        else if (file.key == "prerequisite") q.prerequisite = file.val;
        else if (file.key == "reward_item") q.reward_item = Parse::toInt(file.val);
        else if (file.key == "bonus_item") q.bonus_item = Parse::toInt(file.val);
        else file.error("Unknown village quest key: %s", file.key.c_str());
    }
}

bool CampaignManager::villageVisible(const VillageQuest& q) {
    return !q.hidden || checkStatus(villageStatus(q, "discovered"));
}

void CampaignManager::setVillageQuestion(const std::string& question) {
    village_question = question;
}

void CampaignManager::notifyVillageTrialStart() {
    setStatus(registerStatus("chief_trial_run_active"));
    unsetStatus(registerStatus("chief_trial_run_damaged"));
    unsetStatus(registerStatus("chief_trial_run_room_1"));
    unsetStatus(registerStatus("chief_trial_run_room_2"));
    unsetStatus(registerStatus("chief_trial_run_eligible"));
    if (checkStatus(registerStatus("chief_trial_accepted")))
        setStatus(registerStatus("chief_trial_run_eligible"));
}

void CampaignManager::notifyVillageDamage() {
    if (checkStatus(registerStatus("chief_trial_run_active")))
        setStatus(registerStatus("chief_trial_run_damaged"));
}

void CampaignManager::notifyVillageMap(const std::string& filename) {
    const bool trial_map = filename == "maps/dnf_arena_1.txt" || filename == "maps/dnf_arena_2.txt" ||
        filename == "maps/dnf_arena_3.txt" || filename == "maps/dnf_room_1.txt" ||
        filename == "maps/dnf_room_2.txt" || filename == "maps/dnf_room_3.txt";
    if (!trial_map) unsetStatus(registerStatus("chief_trial_run_active"));
}

void CampaignManager::notifyVillageTrialStage(int stage) {
    if (!checkStatus(registerStatus("chief_trial_run_active"))) return;
    if (stage == 1) setStatus(registerStatus("chief_trial_run_room_1"));
    else if (stage == 2 && checkStatus(registerStatus("chief_trial_run_room_1")))
        setStatus(registerStatus("chief_trial_run_room_2"));
    else if (stage == 3) {
        if (checkStatus(registerStatus("chief_trial_run_room_2")) &&
            checkStatus(registerStatus("chief_trial_run_eligible")) &&
            !checkStatus(registerStatus("chief_trial_run_damaged")) &&
            !checkStatus(registerStatus("chief_trial_flawless_claimed"))) {
            setStatus(registerStatus("chief_trial_flawless_ready"));
            pc->logMsg("试炼守卫发现了你的特殊表现，回村找村长聊聊。", Avatar::MSG_UNIQUE);
        }
        unsetStatus(registerStatus("chief_trial_run_active"));
    }
}

bool CampaignManager::villageBonusReady(const VillageQuest& q) {
    return q.bonus_item > 0 && checkStatus(registerStatus("chief_trial_flawless_ready")) &&
        !checkStatus(registerStatus("chief_trial_flawless_claimed"));
}

std::string CampaignManager::claimVillageBonus(const VillageQuest& q) {
    if (!villageBonusReady(q)) return "";
    setStatus(registerStatus("chief_trial_flawless_claimed"));
    ItemStack reward; reward.item = q.bonus_item; reward.quantity = 1;
    rewardItem(reward);
    return " 无伤通过三层试炼，获得隐藏奖励：" + items->getItemName(q.bonus_item) + "。";
}

StatusID CampaignManager::villageStatus(const VillageQuest& q, const std::string& suffix) {
    return registerStatus("chief_" + q.id + "_" + suffix);
}

int CampaignManager::villageProgress(const VillageQuest& q) {
    if (checkStatus(villageStatus(q, "claimed"))) return q.target;
    if (!checkStatus(villageStatus(q, "accepted"))) return 0;
    if (q.type == "collect") return std::min(q.target, menu->inv->inventory[MenuInventory::CARRIED].count(q.item));
    for (int i = q.target; i > 0; --i)
        if (checkStatus(villageStatus(q, "progress_" + std::to_string(i)))) return i;
    return 0;
}

void CampaignManager::advanceVillageQuest(const VillageQuest& q) {
    if (!checkStatus(villageStatus(q, "accepted")) || checkStatus(villageStatus(q, "claimed"))) return;
    int count = villageProgress(q);
    if (count >= q.target) return;
    if (count > 0) unsetStatus(villageStatus(q, "progress_" + std::to_string(count)));
    setStatus(villageStatus(q, "progress_" + std::to_string(count + 1)));
    if (count + 1 == q.target) {
        setStatus(villageStatus(q, "ready"));
        pc->logMsg(q.title + "：目标完成，回村找村长提交。", Avatar::MSG_UNIQUE);
    }
}

void CampaignManager::notifyVillageKill(const StatBlock& enemy) {
    if (enemy.hero_ally) return;
    bool goblin = false;
    for (size_t i = 0; i < enemy.categories.size(); ++i)
        if (enemy.categories[i].find("goblin") != std::string::npos) goblin = true;
    if (!goblin) return;
    loadVillageQuests();
    for (size_t i = 0; i < village_quests.size(); ++i)
        if (village_quests[i].type == "kill") advanceVillageQuest(village_quests[i]);
}

void CampaignManager::notifyVillageTrial() {
    loadVillageQuests();
    for (size_t i = 0; i < village_quests.size(); ++i)
        if (village_quests[i].type == "trial") advanceVillageQuest(village_quests[i]);
}

std::string CampaignManager::villageQuestRequest(const std::string& operation, const std::string& id) {
    loadVillageQuests();
    bool ok = operation == "list";
    std::string message = ok ? "任务进度已更新。" : "未知任务或操作。";
    if (operation == "discover") {
        const char* words[] = {"过去", "旧事", "往事", "失踪", "冒险者"};
        bool mentioned = false;
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
            if (village_question.find(words[i]) != std::string::npos) mentioned = true;
        for (size_t i = 0; i < village_quests.size(); ++i) {
            VillageQuest& q = village_quests[i];
            if (!q.hidden || q.clue != id) continue;
            if (villageVisible(q)) { ok = true; message = "这条线索已告诉过你，查看委托即可。"; }
            else if (!mentioned) message = "玩家还没有询问相关往事，暂时没有新的线索。";
            else if (!q.prerequisite.empty() && !checkStatus(registerStatus(q.prerequisite)))
                message = "先替村子处理村外威胁，完成并交付委托后再聊往事吧。";
            else {
                setStatus(villageStatus(q, "discovered"));
                ok = true;
                message = "以前有位冒险者失踪了。他最后去过堕落港洞穴，入口附近的旧箱子里也许还留着他的信物。你愿意替我找回来吗？";
                pc->logMsg("发现了新的村长委托。", Avatar::MSG_UNIQUE);
            }
        }
    }
    for (size_t i = 0; i < village_quests.size(); ++i) {
        VillageQuest& q = village_quests[i];
        if (q.id != id || !villageVisible(q)) continue;
        if (operation == "accept") {
            if (checkStatus(villageStatus(q, "claimed"))) message = "这个任务已经领取过奖励。";
            else if (checkStatus(villageStatus(q, "accepted"))) message = "这个任务已经接取。";
            else {
                setStatus(villageStatus(q, "accepted"));
                ok = true; message = "已接取：" + q.title + "。";
                pc->logMsg(message, Avatar::MSG_UNIQUE);
            }
        }
        else if (operation == "submit") {
            if (checkStatus(villageStatus(q, "claimed")) && villageBonusReady(q)) {
                ok = true; message = "特别验收完成。" + claimVillageBonus(q);
                pc->logMsg(message, Avatar::MSG_UNIQUE);
            }
            else if (checkStatus(villageStatus(q, "claimed"))) message = "奖励已经领取，不能重复提交。";
            else if (!checkStatus(villageStatus(q, "accepted"))) message = "请先接取这个任务。";
            else if (villageProgress(q) < q.target) message = "实际进度不足，暂时不能提交。";
            else {
                // All mutations run on the game thread; claimed prevents duplicate rewards.
                setStatus(villageStatus(q, "claimed"));
                if (q.type == "collect") { ItemStack stack; stack.item = q.item; stack.quantity = q.target; removeItem(stack); }
                rewardCurrency(q.gold);
                rewardXP(static_cast<float>(q.xp), XP_SHOW_MSG);
                if (q.reward_item > 0) { ItemStack reward; reward.item = q.reward_item; reward.quantity = 1; rewardItem(reward); }
                ok = true; message = "任务验收完成：" + q.title + "，奖励已发放。";
                if (q.hidden && q.clue == "past")
                    message += " 这正是那位冒险者留下的信物。谢谢你替我找回这段往事，守望之戒请收下，愿它保护你的旅途。";
                message += claimVillageBonus(q);
                pc->logMsg(message, Avatar::MSG_UNIQUE);
            }
        }
    }
    std::ostringstream out;
    out << "{\"ok\":" << (ok ? "true" : "false") << ",\"message\":" << questJsonString(message) << ",\"quests\":[";
    bool first_quest = true;
    for (size_t i = 0; i < village_quests.size(); ++i) {
        VillageQuest& q = village_quests[i];
        if (!villageVisible(q)) continue;
        int count = villageProgress(q);
        std::string state = checkStatus(villageStatus(q, "claimed")) ? "已领取奖励" :
            !checkStatus(villageStatus(q, "accepted")) ? "可接取" : count >= q.target ? "可提交" : "进行中";
        if (!first_quest) out << ',';
        first_quest = false;
        out << "{\"id\":" << questJsonString(q.id) << ",\"title\":" << questJsonString(q.title)
            << ",\"goal\":" << questJsonString(q.goal) << ",\"state\":" << questJsonString(state)
            << ",\"current\":" << count << ",\"target\":" << q.target << ",\"reward_gold\":" << q.gold << ",\"reward_xp\":" << q.xp;
        if (q.reward_item > 0) out << ",\"reward_item\":" << questJsonString(items->getItemName(q.reward_item));
        if (q.bonus_item > 0 && checkStatus(registerStatus("chief_trial_flawless_ready")))
            out << ",\"special_reward\":" << questJsonString(items->getItemName(q.bonus_item))
                << ",\"special_reward_state\":" << questJsonString(villageBonusReady(q) ? "可领取" : "已领取");
        out << '}';
    }
    out << "]}";
    village_feedback = message;
    return out.str();
}

std::string CampaignManager::villageQuestSummary() {
    loadVillageQuests();
    std::ostringstream out;
    out << "## 村长的委托\n";
    for (size_t i = 0; i < village_quests.size(); ++i) {
        VillageQuest& q = village_quests[i];
        if (!villageVisible(q)) continue;
        int progress = villageProgress(q);
        std::string state = checkStatus(villageStatus(q, "claimed")) ? "已领奖" :
            !checkStatus(villageStatus(q, "accepted")) ? "未接取" : progress >= q.target ? "可提交" : "进行中";
        out << "\n### " << (i + 1) << ". " << q.title << "（" << state << "）\n" << q.goal
            << "\n进度：" << progress << "/" << q.target << " · 奖励：" << q.gold << "金币、" << q.xp << "经验\n";
        if (q.reward_item > 0) out << "装备奖励：" << items->getItemName(q.reward_item) << "\n";
        if (q.bonus_item > 0 && checkStatus(registerStatus("chief_trial_flawless_ready")))
            out << "发现特殊奖励：" << items->getItemName(q.bonus_item) << (villageBonusReady(q) ? "（可领取）" : "（已领取）") << "\n";
    }
    return out.str();
}

void CampaignManager::villageQuestControls(std::vector<std::string>& ids, std::vector<std::string>& labels, std::vector<std::string>& operations) {
    loadVillageQuests();
    ids.clear(); labels.clear(); operations.clear();
    for (size_t i = 0; i < village_quests.size(); ++i) {
        VillageQuest& q = village_quests[i];
        if (!villageVisible(q)) continue;
        std::string action = villageBonusReady(q) && checkStatus(villageStatus(q, "claimed")) ? "submit" :
            checkStatus(villageStatus(q, "claimed")) ? "done" :
            !checkStatus(villageStatus(q, "accepted")) ? "accept" : villageProgress(q) >= q.target ? "submit" : "list";
        std::string label = q.hidden ? "往事" : q.type == "kill" ? "哥布林" : q.type == "collect" ? "芦荟" : "试炼";
        label += action == "done" ? "·已领奖" : action == "accept" ? "·接取" : action == "submit" ? "·提交" : "·进度";
        ids.push_back(q.id); labels.push_back(label); operations.push_back(action);
    }
    if (!checkStatus(registerStatus("chief_old_story_discovered"))) {
        ids.push_back("past"); labels.push_back("聊聊往事"); operations.push_back("discover");
    }
}
