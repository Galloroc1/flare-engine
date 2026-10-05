/*
Copyright © 2011-2012 Clint Bellanger and morris989
Copyright © 2013-2014 Henrik Andersson
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
 * class MenuTalker
 */

#ifndef MENU_TALKER_H
#define MENU_TALKER_H

#include "CommonIncludes.h"
#include "Utils.h"
#include "Widget.h"

class CampaignManager;
class NPC;
class StatBlock;
class WidgetButton;
class WidgetLabel;
class WidgetScrollBox;
class WidgetInput;
class AIChatBridge;

class MenuTalker : public Menu {
private:
	class Action {
	public:
		static const int NO_NODE = -1;
		static const bool IS_VENDOR = true;

		Action();
		~Action();

		WidgetButton* btn;
		int node_id;
		bool is_vendor;
	};

	void createActionButtons(int node_id);
	void clearActionButtons();
	void createActionBuffer();
	void executeAction(size_t index);
	void nextDialog();
	void setupTabList();
	void addAction(const std::string& label, int node_id, bool is_vendor);
	void logicAI();
	void refreshAIBuffer();
	void submitAIQuestion();
	void setAIChatLayout(bool enabled);

	Sprite *portrait;
	std::string hero_name;
	std::string hero_class;

	int dialog_node;
	unsigned int event_cursor;
	bool first_interaction;

	Rect dialog_pos;
	Rect text_pos;
	Point text_offset;
	Rect portrait_he;
	Rect portrait_you;
	Rect default_window_area;
	Rect default_dialog_pos;
	Rect default_text_pos;
	Point default_text_offset;
	Rect default_portrait_he;
	Rect default_portrait_you;
	Point default_close_pos;
	Point default_advance_pos;
	int default_alignment;
	bool ai_chat_layout;

	std::string font_who;
	std::string font_dialog;

	WidgetLabel *label_name;
	WidgetScrollBox *textbox;
	WidgetInput *ai_input;
	WidgetButton *ai_send;
	std::vector<WidgetButton*> ai_quest_buttons;
	std::vector<std::string> ai_quest_ids, ai_quest_operations;
	void refreshQuestControls();
	AIChatBridge *ai_bridge;
	std::string ai_text;
	bool ai_waiting;
	bool ai_thinking;
	size_t ai_answer_chunks;
	size_t ai_answer_bytes;

	std::vector<Action> actions;

	Color topic_color_normal;
	Color topic_color_hover;
	Color topic_color_pressed;

	Color trade_color_normal;
	Color trade_color_hover;
	Color trade_color_pressed;

public:
	explicit MenuTalker();
	~MenuTalker();
	void align();

	NPC *npc;

	void chooseDialogNode(int requested_node);
	void logic();
	void render();
	void setHero(StatBlock &stats);
	void createBuffer();
	void setNPC(NPC* _npc);

	WidgetButton *advanceButton;
	WidgetButton *closeButton;

	bool npc_from_map;
};

#endif
