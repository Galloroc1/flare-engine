/*
Copyright © 2011-2012 Clint Bellanger
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


#ifndef CAMPAIGN_MANAGER_H
#define CAMPAIGN_MANAGER_H

#include "CommonIncludes.h"
#include "ItemManager.h"
#include "Utils.h"

class EventComponent;
class StatBlock;

class CampaignManager {
public:
	typedef std::map<StatusID, std::pair<bool, std::string> > StatusMap;

	CampaignManager();
	~CampaignManager();

	StatusID registerStatus(const std::string& s);
	void setAll(const std::string& s);
	std::string getAll();
	bool checkStatus(const StatusID s);
	void setStatus(const StatusID s);
	void unsetStatus(const StatusID s);
	void resetAllStatuses();
	void getSetStatusStrings(std::vector<std::string>& status_strings);
	bool checkCurrency(int quantity);
	bool checkItem(ItemStack istack);
	void removeCurrency(int quantity);
	void removeItem(ItemStack istack);
	void rewardItem(ItemStack istack);
	void rewardCurrency(int amount);
	void rewardXP(float amount, bool show_message);
	void restoreHPMP(const std::string& s);
	bool checkAllRequirements(const EventComponent& ec);
	bool checkRequirementsInVector(const std::vector<EventComponent>& ec_vec);

	void randomStatusAppend(const StatusID s);
	void randomStatusClear();
	void randomStatusRoll();
	void randomStatusSet();
	void randomStatusUnset();

	std::queue<ItemStack> drop_stack;

	// Village-chief quests use campaign statuses, which are saved per character.
	std::string villageQuestRequest(const std::string& operation, const std::string& id);
	void notifyVillageKill(const StatBlock& enemy);
	void notifyVillageTrial();
	void notifyVillageTrialStart();
	void notifyVillageTrialStage(int stage);
	void notifyVillageDamage();
	void notifyVillageMap(const std::string& filename);
	void setVillageQuestion(const std::string& question);
	const std::string& villageQuestFeedback() const { return village_feedback; }
	std::string villageQuestSummary();
	void villageQuestControls(std::vector<std::string>& ids, std::vector<std::string>& labels, std::vector<std::string>& operations);

	float bonus_xp;		// Fractional XP points not yet awarded (e.g. killing 1 XP enemies with a +25% ring)

	static const bool XP_SHOW_MSG = true;

private:
	struct VillageQuest {
		std::string id, title, goal, type, clue, prerequisite;
		int target, item, gold, xp, reward_item, bonus_item;
		bool hidden;
		VillageQuest() : target(1), item(0), gold(0), xp(0), reward_item(0), bonus_item(0), hidden(false) {}
	};
	std::vector<VillageQuest> village_quests;
	bool village_quests_loaded;
	std::string village_question;
	std::string village_feedback;
	bool villageVisible(const VillageQuest& quest);
	bool villageBonusReady(const VillageQuest& quest);
	std::string claimVillageBonus(const VillageQuest& quest);
	void loadVillageQuests();
	int villageProgress(const VillageQuest& quest);
	void advanceVillageQuest(const VillageQuest& quest);
	StatusID villageStatus(const VillageQuest& quest, const std::string& suffix);
	StatusMap status;

	std::vector<StatusID> random_status_pool;
	StatusID random_status;
};


#endif
