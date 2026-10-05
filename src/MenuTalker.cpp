/*
Copyright © 2011-2012 Clint Bellanger and morris989
Copyright © 2012 Stefan Beller
Copyright © 2013-2014 Henrik Andersson
Copyright © 2013 Kurt Rinnert
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

#include "Avatar.h"
#include "AIChatBridge.h"
#include "CampaignManager.h"
#include "Entity.h"
#include "FileParser.h"
#include "FontEngine.h"
#include "InputState.h"
#include "Menu.h"
#include "MenuInventory.h"
#include "MenuManager.h"
#include "MenuTalker.h"
#include "MenuVendor.h"
#include "MessageEngine.h"
#include "NPC.h"
#include "RenderDevice.h"
#include "Settings.h"
#include "SharedResources.h"
#include "SharedGameResources.h"
#include "StatBlock.h"
#include "UtilsParsing.h"
#include "WidgetButton.h"
#include "WidgetInput.h"
#include "WidgetLabel.h"
#include "WidgetScrollBox.h"

namespace {
struct ChatDisplayLine {
	std::string text;
	bool heading;
	ChatDisplayLine(const std::string& _text, bool _heading)
		: text(_text), heading(_heading) {}
};

std::string stripInlineMarkdown(const std::string& text) {
	std::string out;
	for (size_t i = 0; i < text.size();) {
		bool image = text.compare(i, 2, "![") == 0;
		bool link = text[i] == '[';
		if ((image || link)) {
			size_t label_start = i + (image ? 2 : 1);
			size_t label_end = text.find("](", label_start);
			size_t target_end = label_end == std::string::npos ? std::string::npos : text.find(')', label_end + 2);
			if (label_end != std::string::npos && target_end != std::string::npos) {
				if (image) out += "【" + text.substr(label_start, label_end - label_start) + "】";
				else out += text.substr(label_start, label_end - label_start);
				i = target_end + 1;
				continue;
			}
		}
		if (text.compare(i, 2, "**") == 0 || text.compare(i, 2, "__") == 0 || text.compare(i, 2, "~~") == 0) {
			i += 2;
			continue;
		}
		if (text[i] == '`' || text[i] == '*' || text[i] == '_' || text[i] == '~') {
			++i;
			continue;
		}
		out += text[i++];
	}
	return out;
}

std::vector<ChatDisplayLine> formatChatMarkdown(const std::string& text) {
	std::vector<ChatDisplayLine> lines;
	bool in_code_block = false;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t')) line.erase(line.size() - 1);
		size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos) {
			lines.push_back(ChatDisplayLine("", false));
		}
		else {
			line.erase(0, first);
			if (line.compare(0, 3, "```") == 0 || line.compare(0, 3, "~~~") == 0) {
				in_code_block = !in_code_block;
			}
			else if (line.find_first_not_of("-=*_ ") == std::string::npos && line.size() >= 3) {
				// Markdown horizontal rule: keep a little separation, not the raw marker.
				lines.push_back(ChatDisplayLine("", false));
			}
			else {
				bool heading = false;
				bool emphasized = line.find("**") != std::string::npos || line.find("__") != std::string::npos;
				std::string prefix;
				size_t hashes = 0;
				while (hashes < line.size() && hashes < 6 && line[hashes] == '#') ++hashes;
				if (hashes > 0 && hashes < line.size() && line[hashes] == ' ') {
					heading = true;
					line.erase(0, hashes + 1);
					prefix = "◆ ";
				}
				else if (line.compare(0, 2, "> ") == 0) {
					line.erase(0, 2);
					prefix = "│ ";
				}
				else if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
					line.erase(0, 2);
					prefix = "• ";
				}
				else {
				size_t digit_end = 0;
				while (digit_end < line.size() && line[digit_end] >= '0' && line[digit_end] <= '9') ++digit_end;
					if (digit_end > 0 && digit_end + 1 < line.size() && line[digit_end] == '.' && line[digit_end + 1] == ' ') {
						prefix = line.substr(0, digit_end + 2);
					line.erase(0, digit_end + 2);
				}
				}
				if (in_code_block) prefix = "  ";
				line = prefix + stripInlineMarkdown(line);
				lines.push_back(ChatDisplayLine(line, heading || emphasized));
			}
		}
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return lines;
}
}

MenuTalker::Action::Action()
	: btn(NULL)
	, node_id(0)
	, is_vendor(false)
{}

MenuTalker::Action::~Action()
{}

MenuTalker::MenuTalker()
	: Menu()
	, portrait(NULL)
	, dialog_node(-1)
	, event_cursor(0)
	, first_interaction(false)
	, ai_chat_layout(false)
	, font_who("font_regular")
	, font_dialog("font_regular")
	, ai_waiting(false)
	, ai_thinking(false)
	, ai_answer_chunks(0)
	, ai_answer_bytes(0)
	, topic_color_normal(font->getColor(FontEngine::COLOR_MENU_BONUS))
	, topic_color_hover(font->getColor(FontEngine::COLOR_WIDGET_NORMAL))
	, topic_color_pressed(font->getColor(FontEngine::COLOR_WIDGET_DISABLED))
	, trade_color_normal(font->getColor(FontEngine::COLOR_MENU_BONUS))
	, trade_color_hover(font->getColor(FontEngine::COLOR_WIDGET_NORMAL))
	, trade_color_pressed(font->getColor(FontEngine::COLOR_WIDGET_DISABLED))
	, npc(NULL)
	, advanceButton(new WidgetButton(WidgetButton::DIR_RIGHT_FILE))
	, closeButton(new WidgetButton(WidgetButton::CLOSE_FILE))
	, npc_from_map(true)
{
	// Load config settings
	FileParser infile;
	// @CLASS MenuTalker|Description of menus/talker.txt
	if(infile.open("menus/talker.txt", FileParser::MOD_FILE, FileParser::ERROR_NORMAL)) {
		while(infile.next()) {
			if (parseMenuKey(infile.key, infile.val))
				continue;

			// @ATTR close|point|Position of the close button.
			if(infile.key == "close") {
				Point pos = Parse::toPoint(infile.val);
				closeButton->setBasePos(pos.x, pos.y, Utils::ALIGN_TOPLEFT);
			}
			// @ATTR advance|point|Position of the button to advance dialog.
			else if(infile.key == "advance") {
				Point pos = Parse::toPoint(infile.val);
				advanceButton->setBasePos(pos.x, pos.y, Utils::ALIGN_TOPLEFT);
			}
			// @ATTR dialogbox|rectangle|Position and dimensions of the text box graphics.
			else if (infile.key == "dialogbox") dialog_pos = Parse::toRect(infile.val);
			// @ATTR dialogtext|rectangle|Rectangle where the dialog text is placed.
			else if (infile.key == "dialogtext") text_pos = Parse::toRect(infile.val);
			// @ATTR text_offset|point|Margins for the left/right and top/bottom of the dialog text.
			else if (infile.key == "text_offset") text_offset = Parse::toPoint(infile.val);
			// @ATTR portrait_he|rectangle|Position and dimensions of the NPC portrait graphics.
			else if (infile.key == "portrait_he") portrait_he = Parse::toRect(infile.val);
			// @ATTR portrait_you|rectangle|Position and dimensions of the player's portrait graphics.
			else if (infile.key == "portrait_you") portrait_you = Parse::toRect(infile.val);
			// @ATTR font_who|predefined_string|Font style to use for the name of the currently talking person.
			else if (infile.key == "font_who") font_who = infile.val;
			// @ATTR font_dialog|predefined_string|Font style to use for the dialog text.
			else if (infile.key == "font_dialog") font_dialog = infile.val;

			// @ATTR topic_color_normal|color|The normal color for topic text.
			else if (infile.key == "topic_color_normal") topic_color_normal = Parse::toRGB(infile.val);
			// @ATTR topic_color_hover|color|The color for topic text when highlighted.
			else if (infile.key == "topic_color_hover") topic_color_hover = Parse::toRGB(infile.val);
			// @ATTR topic_color_pressed|color|The color for topic text when clicked.
			else if (infile.key == "topic_color_pressed") topic_color_pressed = Parse::toRGB(infile.val);

			// @ATTR trade_color_normal|color|The normal color for the "Trade" text.
			else if (infile.key == "trade_color_normal") trade_color_normal = Parse::toRGB(infile.val);
			// @ATTR trade_color_hover|color|The color for the "Trade" text when highlighted.
			else if (infile.key == "trade_color_hover") trade_color_hover = Parse::toRGB(infile.val);
			// @ATTR trade_color_normal|color|The color for the "Trade" text when clicked.
			else if (infile.key == "trade_color_pressed") trade_color_pressed = Parse::toRGB(infile.val);

			else infile.error("MenuTalker: '%s' is not a valid key.", infile.key.c_str());
		}
		infile.close();
	}
	default_window_area = window_area;
	default_dialog_pos = dialog_pos;
	default_text_pos = text_pos;
	default_text_offset = text_offset;
	default_portrait_he = portrait_he;
	default_portrait_you = portrait_you;
	default_alignment = alignment;
	default_close_pos = closeButton->pos_base;
	default_advance_pos = advanceButton->pos_base;

	label_name = new WidgetLabel();
	label_name->setBasePos(text_pos.x + text_offset.x, text_pos.y + text_offset.y, Utils::ALIGN_TOPLEFT);
	label_name->setColor(font->getColor(FontEngine::COLOR_MENU_NORMAL));

	textbox = new WidgetScrollBox(text_pos.w, text_pos.h-(text_offset.y*2));
	textbox->setBasePos(text_pos.x, text_pos.y + text_offset.y, Utils::ALIGN_TOPLEFT);
	textbox->show_focus_when_scrollbar_disabled = false;
	ai_input = new WidgetInput(WidgetInput::NO_FILE);
	ai_input->accept_to_defocus = false;
	ai_input->max_length = 500;
	ai_input->setFontName(font_dialog);
	ai_input->setBasePos(text_pos.x + text_offset.x,
		text_pos.y + text_pos.h - 42, Utils::ALIGN_TOPLEFT);
	ai_input->resize(text_pos.w - 95);
	ai_send = new WidgetButton(WidgetButton::NO_FILE);
	ai_send->setLabel("发送");
	ai_send->setTextFont(font_dialog);
	ai_send->setBasePos(text_pos.x + text_pos.w - 75,
		text_pos.y + text_pos.h - 42, Utils::ALIGN_TOPLEFT);
	ai_bridge = new AIChatBridge();
	ai_waiting = false;
	ai_thinking = false;

	if (!background)
		setBackground("images/menus/dialog_box.png");

	align();
}

void MenuTalker::align() {
	Menu::align();
	if (ai_chat_layout) {
		advanceButton->setPos(window_area.x, window_area.y);
		closeButton->setPos(window_area.x, window_area.y);
		label_name->setPos(window_area.x, window_area.y);
		textbox->setPos(window_area.x, window_area.y);
		textbox->pos.h = text_pos.h - text_offset.y*2;
		textbox->resize(text_pos.w, textbox->pos.h);
		ai_input->setPos(window_area.x, window_area.y);
		ai_send->setPos(window_area.x, window_area.y);
        for (size_t i = 0; i < ai_quest_buttons.size(); ++i) ai_quest_buttons[i]->setPos(window_area.x, window_area.y);
		return;
	}

	advanceButton->setPos(window_area.x, window_area.y);
	closeButton->setPos(window_area.x, window_area.y);

	label_name->setPos(window_area.x, window_area.y);

	textbox->setPos(window_area.x, window_area.y + label_name->getBounds()->h);
	textbox->pos.h = text_pos.h - (text_offset.y*2);
	if (npc && npc->ai_chat)
		textbox->pos.h -= 56;
	ai_input->setPos(window_area.x, window_area.y);
	ai_send->setPos(window_area.x, window_area.y);
    for (size_t i = 0; i < ai_quest_buttons.size(); ++i) ai_quest_buttons[i]->setPos(window_area.x, window_area.y);
}

void MenuTalker::setAIChatLayout(bool enabled) {
	if (ai_chat_layout == enabled) return;
	ai_chat_layout = enabled;
	if (enabled) {
		const int panel_w = std::max(480, std::min(900, settings->view_w * 45 / 100));
		const int panel_h = std::max(420, std::min(1000, settings->view_h - 40));
		window_area = Rect(0, 0, panel_w, panel_h);
		setWindowPos(-24, 0);
		alignment = Utils::ALIGN_RIGHT;
		dialog_pos = Rect(0, 0, panel_w, panel_h);
		text_pos = Rect(32, 370, panel_w - 64, panel_h - 506);
		text_offset = Point(12, 10);
		// Portrait sprites are clipped at native resolution by the render device;
		// using a smaller rectangle crops the upper-left instead of scaling down.
		portrait_he = Rect(28, 26, 320, 320);
		portrait_you = portrait_he;
		label_name->setBasePos(372, 54, Utils::ALIGN_TOPLEFT);
		textbox->setBasePos(text_pos.x, text_pos.y, Utils::ALIGN_TOPLEFT);
		ai_input->setBasePos(32, panel_h - 66, Utils::ALIGN_TOPLEFT);
		ai_input->resize(panel_w - 142);
		ai_send->setBasePos(panel_w - 96, panel_h - 66, Utils::ALIGN_TOPLEFT);
		closeButton->setBasePos(panel_w - 48, 14, Utils::ALIGN_TOPLEFT);
		advanceButton->setBasePos(panel_w - 48, 14, Utils::ALIGN_TOPLEFT);
		setBackgroundColor(Color(20, 24, 38, 235));
	}
	else {
		window_area = default_window_area;
		setWindowPos(default_window_area.x, default_window_area.y);
		alignment = default_alignment;
		dialog_pos = default_dialog_pos;
		text_pos = default_text_pos;
		text_offset = default_text_offset;
		portrait_he = default_portrait_he;
		portrait_you = default_portrait_you;
		label_name->setBasePos(text_pos.x + text_offset.x, text_pos.y + text_offset.y, Utils::ALIGN_TOPLEFT);
		textbox->setBasePos(text_pos.x, text_pos.y + text_offset.y, Utils::ALIGN_TOPLEFT);
		ai_input->setBasePos(text_pos.x + text_offset.x,
			text_pos.y + text_pos.h - 42, Utils::ALIGN_TOPLEFT);
		ai_input->resize(text_pos.w - 95);
		ai_send->setBasePos(text_pos.x + text_pos.w - 75,
			text_pos.y + text_pos.h - 42, Utils::ALIGN_TOPLEFT);
		closeButton->setBasePos(default_close_pos.x, default_close_pos.y, Utils::ALIGN_TOPLEFT);
		advanceButton->setBasePos(default_advance_pos.x, default_advance_pos.y, Utils::ALIGN_TOPLEFT);
		setBackground("images/menus/dialog_box.png");
	}
	align();
}

void MenuTalker::chooseDialogNode(int request_dialog_node) {
	event_cursor = 0;
	if (npc && npc->ai_chat) {
		setAIChatLayout(true);
		dialog_node = -1;
		clearActionButtons();
		if (!npc->portraits.empty()) npc->npc_portrait = npc->portraits[0];
		label_name->setText(npc->name);
		label_name->setFont(font_who);
		closeButton->enabled = true;
		advanceButton->enabled = false;
		ai_text = "村长：欢迎来到堕落港。可以问我有什么任务，或直接点击下方的接取、提交按钮。\n\n" + camp->villageQuestSummary();
        camp->setVillageQuestion("");
        refreshQuestControls();
		ai_waiting = false;
		ai_thinking = false;
		ai_input->setText("");
		ai_input->edit_mode = true;
		align();
		refreshAIBuffer();
		if (!ai_bridge->start()) {
			ai_text += "\n（Python AI 环境不可用，请在 flare-engine 目录执行 uv sync。）";
			refreshAIBuffer();
		}
		first_interaction = false;
		return;
	}
	setAIChatLayout(false);

	if(request_dialog_node == -1) {
		// display the topic list (or automatically select a topic if there's only one)
		dialog_node = -1;
		createActionBuffer();

		// need to set the portrait here since we don't call processDialog()
		if (!npc->portraits.empty())
			npc->npc_portrait = npc->portraits[0];

		if (actions.size() == 1 && first_interaction) {
			executeAction(0);
		}
		else if (actions.empty()) {
			setNPC(NULL); // end dialog
		}
	}
	else {
		dialog_node = request_dialog_node;
		npc->processEvent(dialog_node, event_cursor);
		if (npc->processDialog(dialog_node, event_cursor))
			createBuffer();
		else
			setNPC(NULL); // end dialog
	}

	first_interaction = false;
}

/**
 * Menu interaction (enter/space/click to continue)
 */
void MenuTalker::logic() {

	if (!visible || !npc)
		return;
	if (npc->ai_chat) {
		logicAI();
		return;
	}

	if (!inpt->usingMouse() && tablist.getCurrent() == -1) {
		tablist.setCurrent(textbox);
	}
	tablist.enable_activate = !actions.empty();

	tablist.logic();

	if (advanceButton->checkClick() || closeButton->checkClick()) {
		// button was clicked
		if (closeButton->enabled) {
			nextDialog();
			setNPC(NULL);
		}
		else {
			nextDialog();
		}
	}
	else if	((advanceButton->enabled || closeButton->enabled) && inpt->pressing[Input::ACCEPT] && !inpt->lock[Input::ACCEPT]) {
		// pressed next/more
		inpt->lock[Input::ACCEPT] = true;
		if (closeButton->enabled) {
			nextDialog();
			setNPC(NULL);
		}
		else {
			nextDialog();
		}
	}
	else {
		textbox->logic();

		Point mouse = textbox->input_assist(inpt->mouse);
		for (size_t i = 0; i < actions.size(); ++i) {
			if (actions[i].btn->checkClickAt(mouse.x, mouse.y)) {
				executeAction(i);
				break;
			}
		}

		Rect lock_area = dialog_pos;
		lock_area.x += window_area.x;
		lock_area.y += window_area.y;
		if (inpt->pressing[Input::MAIN1] && !inpt->lock[Input::MAIN1] && Utils::isWithinRect(lock_area, inpt->mouse)) {
			inpt->lock[Input::MAIN1] = true;
		}
	}
}

void MenuTalker::logicAI() {
	std::vector<std::string> events;
	ai_bridge->poll(events);
	bool changed = false;
	bool stream_finished = false;
	for (size_t i = 0; i < events.size(); ++i) {
		const std::string& event = events[i];
		if (event.size() > 2 && event[0] == 'U' && event[1] == '\t' && ai_waiting) {
			std::string request = AIChatBridge::decode(event.substr(2));
			size_t separator = request.find('\t');
			std::string operation = request.substr(0, separator);
			std::string id = separator == std::string::npos ? "" : request.substr(separator + 1);
			std::string result = camp->villageQuestRequest(operation, id);
			ai_bridge->replyTool(result);
            refreshQuestControls();
            ai_text += "\n\n" + camp->villageQuestFeedback() + "\n" + camp->villageQuestSummary() + "\n村长：";
            changed = true;
			AIChatBridge::logDiagnostic("quest_tool operation=" + operation + " id=" + id + " result=" + result);
		}
		else if (event == "T" && ai_waiting) {
			ai_thinking = true;
			changed = true;
		}
		else if (event.size() > 2 && event[1] == '\t' && event[0] == 'D') {
			std::string delta = AIChatBridge::decode(event.substr(2));
			ai_text += delta;
			ai_answer_chunks++;
			ai_answer_bytes += delta.size();
			ai_thinking = false;
			changed = true;
		}
		else if (event.size() > 2 && event[1] == '\t' && event[0] == 'E') {
			ai_text += "\n" + AIChatBridge::decode(event.substr(2));
			ai_waiting = false;
			ai_thinking = false;
			changed = true;
		}
		else if (event == "F") {
			ai_waiting = false;
			ai_thinking = false;
			stream_finished = true;
			changed = true;
		}
		else if (event == "X" && ai_waiting) {
			ai_text += "\n（连接已断开，请关闭对话后重试。）";
			ai_waiting = false;
			ai_thinking = false;
			changed = true;
		}
	}
	if (changed) refreshAIBuffer();
	if (stream_finished) {
		AIChatBridge::logDiagnostic(
			"talker_render_completed chunks=" + std::to_string(ai_answer_chunks) +
			" answer_bytes=" + std::to_string(ai_answer_bytes) +
			" buffer_bytes=" + std::to_string(ai_text.size()) +
			" textbox=" + std::to_string(textbox->pos.w) + "x" + std::to_string(textbox->pos.h)
		);
	}
    for (size_t i = 0; i < ai_quest_buttons.size(); ++i) {
        ai_quest_buttons[i]->enabled = !ai_waiting && ai_quest_operations[i] != "done";
        if (ai_quest_buttons[i]->checkClick()) {
            if (ai_quest_operations[i] == "discover") camp->setVillageQuestion("想聊聊村长的往事");
            std::string result = camp->villageQuestRequest(ai_quest_operations[i], ai_quest_ids[i]);
            AIChatBridge::logDiagnostic("quest_button result=" + result);
            ai_text += "\n\n村长：" + camp->villageQuestFeedback() + "\n" + camp->villageQuestSummary();
            refreshQuestControls();
            refreshAIBuffer();
            break;
        }
    }
	textbox->logic();
	if (closeButton->checkClick()) {
		setNPC(NULL);
		return;
	}
	if (inpt->pressing[Input::CANCEL] && !inpt->lock[Input::CANCEL]) {
		inpt->lock[Input::CANCEL] = true;
		setNPC(NULL);
		return;
	}
	// WidgetInput may lock the accept key while consuming a text-input event.
	// Capture it first so Enter still submits the message on that frame.
	bool submit_pressed = inpt->pressing[Input::ACCEPT] && !inpt->lock[Input::ACCEPT];
	ai_input->logic();
	if (ai_send->checkClick() || submit_pressed) {
		inpt->lock[Input::ACCEPT] = true;
		submitAIQuestion();
	}
	Rect lock_area = dialog_pos;
	lock_area.x += window_area.x;
	lock_area.y += window_area.y;
	if (inpt->pressing[Input::MAIN1] && !inpt->lock[Input::MAIN1] && Utils::isWithinRect(lock_area, inpt->mouse))
		inpt->lock[Input::MAIN1] = true;
}

void MenuTalker::submitAIQuestion() {
	if (ai_waiting) return;
	std::string question = ai_input->getText();
	if (question.empty()) return;
	if (question.size() > 1500) {
		ai_text += "\n（请把问题缩短到 500 个字以内。）";
		refreshAIBuffer();
		return;
	}
	if (ai_text.size() > 6000) {
		size_t cut = ai_text.find('\n', ai_text.size() - 4000);
		if (cut != std::string::npos) ai_text.erase(0, cut + 1);
	}
	ai_text += "\n\n你：" + question + "\n村长：";
	camp->setVillageQuestion(question);
	ai_input->setText("");
	ai_input->edit_mode = true;
	ai_waiting = true;
	ai_thinking = false;
	ai_answer_chunks = ai_answer_bytes = 0;
	if (!ai_bridge->send(question)) {
		ai_text += "（无法连接 AI 服务，请检查启动脚本及模型配置。）";
		ai_waiting = false;
	}
	refreshAIBuffer();
}

void MenuTalker::refreshAIBuffer() {
	std::string shown = ai_text;
	if (ai_thinking) shown += "\n[思考中…]";
	else if (ai_waiting) shown += "\n[正在回复…]";
	const int text_width = textbox->pos.w - text_offset.x*2;
	std::vector<ChatDisplayLine> lines = formatChatMarkdown(shown);
	int content_height = 0;
	for (size_t i = 0; i < lines.size(); ++i) {
		font->setFont(lines[i].heading ? font_who : font_dialog);
		content_height += lines[i].text.empty()
			? font->getLineHeight() : font->calcSizeWrapped(lines[i].text, text_width).y;
	}
	font->setFont(font_dialog);
	textbox->resize(textbox->pos.w, content_height + 8);
	textbox->scrollToBottom();
}

void MenuTalker::createActionBuffer() {
	createActionButtons(-1);

	int button_height = 0;
	for (size_t i = 0; i < actions.size(); ++i) {
		button_height += actions[i].btn->pos.h;
	}

	int button_y = 0;
	for (size_t i = 0; i < actions.size(); ++i) {
		actions[i].btn->pos.x = text_offset.x;
		actions[i].btn->pos.y = button_y;
		actions[i].btn->refresh();

		button_y += actions[i].btn->pos.h;
	}

	label_name->setText(npc->name);
	label_name->setFont(font_who);
	textbox->resize(textbox->pos.w, button_height);

	align();

	closeButton->enabled = true;
	advanceButton->enabled = false;

	setupTabList();
}

void MenuTalker::createBuffer() {
	clearActionButtons();

	if (static_cast<unsigned>(dialog_node) >= npc->dialog.size() || event_cursor >= npc->dialog[dialog_node].size())
		return;

	createActionButtons(dialog_node);

	int button_height = 0;
	for (size_t i = 0; i < actions.size(); ++i) {
		button_height += actions[i].btn->pos.h;
	}

	std::string line;

	// speaker name
	int etype = npc->dialog[dialog_node][event_cursor].type;
	std::string who;

	if (etype == EventComponent::NPC_DIALOG_THEM) {
		who = npc->name;
	}
	else if (etype == EventComponent::NPC_DIALOG_YOU) {
		who = hero_name;
	}

	label_name->setText(who);
	label_name->setFont(font_who);


	line = Utils::substituteVarsInString(npc->dialog[dialog_node][event_cursor].s, pc);

	// render dialog text to the scrollbox buffer
	Point line_size = font->calcSizeWrapped(line,textbox->pos.w-(text_offset.x*2));
	textbox->resize(textbox->pos.w, line_size.y + button_height);
	font->setFont(font_dialog);
	font->render(
		line,
		text_offset.x,
		0,
		FontEngine::JUSTIFY_LEFT,
		textbox->contents->getGraphics(),
		text_pos.w - text_offset.x*2,
		font->getColor(FontEngine::COLOR_MENU_NORMAL),
		!FontEngine::SHADOW_OFFSET
	);

	int button_y = 0;
	for (size_t i = 0; i < actions.size(); ++i) {
		actions[i].btn->pos.x = text_offset.x;
		actions[i].btn->pos.y = line_size.y + button_y;
		actions[i].btn->refresh();

		button_y += actions[i].btn->pos.h;
	}

	align();

	if (!actions.empty()) {
		advanceButton->enabled = false;
		closeButton->enabled = false;
	}
	else if (!npc->dialog[dialog_node].empty() && event_cursor < npc->dialog[dialog_node].size()-1 && npc->dialog[dialog_node][event_cursor+1].type != EventComponent::NONE) {
		advanceButton->enabled = true;
		closeButton->enabled = false;
	}
	else {
		advanceButton->enabled = false;
		closeButton->enabled = true;
	}

	setupTabList();
}

void MenuTalker::render() {
	if (!visible) return;
	Rect src;
	Rect dest;

	int offset_x = window_area.x;
	int offset_y = window_area.y;

	// dialog box
	src.x = 0;
	src.y = 0;
	dest.x = offset_x + dialog_pos.x;
	dest.y = offset_y + dialog_pos.y;
	src.w = dest.w = dialog_pos.w;
	src.h = dest.h = dialog_pos.h;

	setBackgroundClip(src);
	setBackgroundDest(dest);
	Menu::render();

	if (static_cast<unsigned>(dialog_node) < npc->dialog.size() && event_cursor < npc->dialog[dialog_node].size()) {
		// show active portrait
		int etype = npc->dialog[dialog_node][event_cursor].type;
		if (etype == EventComponent::NPC_DIALOG_THEM) {
			if (npc->npc_portrait) {
				src.w = dest.w = portrait_he.w;
				src.h = dest.h = portrait_he.h;
				dest.x = offset_x + portrait_he.x;
				dest.y = offset_y + portrait_he.y;

				npc->npc_portrait->setClipFromRect(src);
				npc->npc_portrait->setDestFromRect(dest);
				render_device->render(npc->npc_portrait);
			}
		}
		else if (etype == EventComponent::NPC_DIALOG_YOU) {
			if (npc->hero_portrait) {
				src.w = dest.w = portrait_you.w;
				src.h = dest.h = portrait_you.h;
				dest.x = offset_x + portrait_you.x;
				dest.y = offset_y + portrait_you.y;
				npc->hero_portrait->setClipFromRect(src);
				npc->hero_portrait->setDestFromRect(dest);
				render_device->render(npc->hero_portrait);
			}
			else if (portrait) {
				src.w = dest.w = portrait_you.w;
				src.h = dest.h = portrait_you.h;
				dest.x = offset_x + portrait_you.x;
				dest.y = offset_y + portrait_you.y;
				portrait->setClipFromRect(src);
				portrait->setDestFromRect(dest);
				render_device->render(portrait);
			}
		}
	}
	else if (dialog_node == -1 && npc->npc_portrait) {
		src.w = dest.w = portrait_he.w;
		src.h = dest.h = portrait_he.h;
		dest.x = offset_x + portrait_he.x;
		dest.y = offset_y + portrait_he.y;

		npc->npc_portrait->setClipFromRect(src);
		npc->npc_portrait->setDestFromRect(dest);
		render_device->render(npc->npc_portrait);
	}

	// name & dialog text
	label_name->render();
	textbox->render();
	if (npc->ai_chat) {
		std::string shown = ai_text;
		if (ai_thinking) shown += "\n[思考中…]";
		else if (ai_waiting) shown += "\n[正在回复…]";
		std::vector<ChatDisplayLine> lines = formatChatMarkdown(shown);
		const int text_width = textbox->pos.w - text_offset.x*2;
		const int viewport_top = textbox->pos.y;
		const int viewport_bottom = viewport_top + textbox->pos.h;
		int line_y = viewport_top - textbox->getScrollOffset();
		for (size_t i = 0; i < lines.size(); ++i) {
			font->setFont(lines[i].heading ? font_who : font_dialog);
			int line_height = lines[i].text.empty()
				? font->getLineHeight() : font->calcSizeWrapped(lines[i].text, text_width).y;
			// Keep whole wrapped lines inside the viewport so long answers never
			// paint over the input area or outside the panel.
			if (!lines[i].text.empty() && line_y >= viewport_top && line_y + line_height <= viewport_bottom) {
				font->render(lines[i].text, textbox->pos.x + text_offset.x, line_y,
					FontEngine::JUSTIFY_LEFT, NULL, text_width,
					font->getColor(FontEngine::COLOR_MENU_NORMAL), !FontEngine::SHADOW_OFFSET);
			}
			line_y += line_height;
		}
		font->setFont(font_dialog);
	}
	if (npc->ai_chat) {
		ai_input->render();
		ai_send->render();
        for (size_t i = 0; i < ai_quest_buttons.size(); ++i) ai_quest_buttons[i]->render();
	}

	// show advance button if there are more event components, or close button if not
	if (advanceButton->enabled)
		advanceButton->render();
	else if (closeButton->enabled)
		closeButton->render();
}

void MenuTalker::setHero(StatBlock &stats) {
	hero_name = stats.name;
	hero_class = stats.getShortClass();

	if (portrait)
		delete portrait;

	if (stats.gfx_portrait == "") return;

	Image *graphics;
	graphics = render_device->loadImage(stats.gfx_portrait, RenderDevice::ERROR_NORMAL);
	if (graphics) {
		portrait = graphics->createSprite();
		graphics->unref();
	}
}

void MenuTalker::setNPC(NPC* _npc) {
	if (npc != _npc) {
		first_interaction = true;
	}

	npc = _npc;

	if (_npc == NULL) {
		ai_bridge->stop();
		if (ai_chat_layout) setAIChatLayout(false);
		ai_input->edit_mode = false;
		inpt->stopTextInput();
		visible = false;
		first_interaction = false;
		tablist.defocus();
		return;
	}

	visible = true;
}

void MenuTalker::createActionButtons(int node_id) {
	if (!npc)
		return;

	clearActionButtons();

	std::vector<int> nodes;
	if (node_id == -1) {
		// primary topic selection
		npc->getDialogNodes(nodes, !NPC::GET_RESPONSE_NODES);
	}
	else {
		// dialog responses
		npc->getDialogResponses(nodes, node_id, event_cursor);
	}

	// add standard topics
	for (size_t i = nodes.size(); i > 0; i--) {
		std::string topic = npc->getDialogTopic(nodes[i-1]);
		if (topic.empty()) {
			topic = msg->getv("<dialog node %d>", nodes[i-1]);
		}

		addAction(topic, nodes[i-1], !Action::IS_VENDOR);
	}

	// add "Trade" topic
	if (node_id == -1 && npc->checkVendor()) {
		addAction(msg->get("Trade"), Action::NO_NODE, Action::IS_VENDOR);
	}

	for (size_t i = 0; i< actions.size(); ++i) {
		actions[i].btn->tablist_nav_align = TabList::NAV_ALIGN_LEFT;
		textbox->addChildWidget(actions[i].btn);
	}
}

void MenuTalker::clearActionButtons() {
	for (size_t i = 0; i < actions.size(); ++i) {
		delete actions[i].btn;
	}
	actions.clear();
	textbox->clearChildWidgets();
}

void MenuTalker::executeAction(size_t index) {
	if (index >= actions.size())
		return;

	int node_id = actions[index].node_id;

	if (actions[index].is_vendor) {
		// defocus the talker menu tablist. Otherwise, CANCEL needs to be pressed twice to exit the vendor screen
		defocusTabLists();

		// begin trading
		NPC *temp_npc = npc;
		menu->closeAll();
		menu->vendor->setNPC(temp_npc);
		menu->inv->visible = true;
	}
	else if (node_id != -1) {
		// begin talking
		chooseDialogNode(node_id);
		if (npc && npc_from_map) {
			pc->allow_movement = npc->checkMovement(node_id);
		}
	}
}

void MenuTalker::nextDialog() {
	bool more = false;

	if (dialog_node != -1) {
		npc->processEvent(dialog_node, event_cursor);
		event_cursor++;
		more = npc->processDialog(dialog_node, event_cursor);
	}
	else {
		more = false;
	}

	if (more)
		createBuffer();
	else {
		if (dialog_node != -1) {
			// return to the topic selection
			int prev_node = dialog_node;
			chooseDialogNode(-1);

			// when returning to the topic selection, a topic is auto-selected if there is only one
			// in this case, we don't want to repeat the same topic, so we check for that here
			if (actions.empty() && dialog_node == prev_node)
				setNPC(NULL);
		}
		else
			setNPC(NULL); // end dialog
	}
}

void MenuTalker::setupTabList() {
	tablist.clear();

	tablist.add(textbox);
}

void MenuTalker::addAction(const std::string& label, int node_id, bool is_vendor) {
	if ((node_id != Action::NO_NODE && is_vendor) || (node_id == Action::NO_NODE && !is_vendor)) {
		Utils::logError("MenuTalker: addAction() parameters are incompatible, skipping action.");
		return;
	}

	actions.push_back(Action());

	actions.back().btn = new WidgetButton(WidgetButton::NO_FILE);
	actions.back().btn->setLabel(label);
	actions.back().btn->setTextFont(font_dialog);

	if (node_id != Action::NO_NODE) {
		actions.back().node_id = node_id;
		actions.back().btn->setTextColor(WidgetButton::BUTTON_NORMAL, topic_color_normal);
		actions.back().btn->setTextColor(WidgetButton::BUTTON_HOVER, topic_color_hover);
		actions.back().btn->setTextColor(WidgetButton::BUTTON_PRESSED, topic_color_pressed);
	}
	else {
		actions.back().is_vendor = true;
		actions.back().btn->setTextColor(WidgetButton::BUTTON_NORMAL, trade_color_normal);
		actions.back().btn->setTextColor(WidgetButton::BUTTON_HOVER, trade_color_hover);
		actions.back().btn->setTextColor(WidgetButton::BUTTON_PRESSED, trade_color_pressed);
	}
}

MenuTalker::~MenuTalker() {
	clearActionButtons();

	delete portrait;
	delete label_name;
	delete textbox;
	delete ai_input;
	delete ai_send;
    for (size_t i = 0; i < ai_quest_buttons.size(); ++i) delete ai_quest_buttons[i];
	delete ai_bridge;
	delete advanceButton;
	delete closeButton;
}

void MenuTalker::refreshQuestControls() {
    std::vector<std::string> labels;
    camp->villageQuestControls(ai_quest_ids, labels, ai_quest_operations);
    while (ai_quest_buttons.size() < labels.size()) {
        WidgetButton* button = new WidgetButton(WidgetButton::NO_FILE);
        button->setTextFont(font_dialog);
        ai_quest_buttons.push_back(button);
    }
    while (ai_quest_buttons.size() > labels.size()) {
        delete ai_quest_buttons.back(); ai_quest_buttons.pop_back();
    }
    const int columns = labels.size() > 3 ? 2 : std::max(1, static_cast<int>(labels.size()));
    const int rows = (static_cast<int>(labels.size()) + columns - 1) / columns;
    const int extra_height = std::max(0, rows - 1) * 42;
    text_pos.h = std::max(40, window_area.h - 506 - extra_height);
    textbox->pos.h = text_pos.h - text_offset.y * 2;
    textbox->resize(text_pos.w, text_pos.h);
    int step = (window_area.w - 64) / columns;
    for (size_t i = 0; i < labels.size(); ++i) {
        ai_quest_buttons[i]->setLabel(labels[i]);
        ai_quest_buttons[i]->tooltip = ai_quest_operations[i] == "discover" ? "聊聊村长的往事" : "接取、查看或提交这项村长委托";
        ai_quest_buttons[i]->setBasePos(32 + static_cast<int>(i % columns) * step,
            window_area.h - 108 - extra_height + static_cast<int>(i / columns) * 42, Utils::ALIGN_TOPLEFT);
        ai_quest_buttons[i]->setPos(window_area.x, window_area.y);
        ai_quest_buttons[i]->enabled = !ai_waiting && ai_quest_operations[i] != "done";
    }
}
