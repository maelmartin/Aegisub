// Copyright (c) 2005, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subs_edit_ctrl.cpp
/// @brief Main subtitle editing text control
/// @ingroup main_ui
///

#include "config.h"

#ifndef AGI_PRE
#include <functional>

#include <wx/clipbrd.h>
#include <wx/intl.h>
#include <wx/menu.h>
#include <wx/settings.h>
#endif

#include "subs_edit_ctrl.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "command/command.h"
#include "compat.h"
#include "main.h"
#include "include/aegisub/context.h"
#include "include/aegisub/spellchecker.h"
#include "selection_controller.h"
#include "thesaurus.h"
#include "utils.h"

#include <libaegisub/ass/dialogue_parser.h>
#include <libaegisub/calltip_provider.h>
#include <libaegisub/spellchecker.h>

/// Event ids
enum {
	EDIT_MENU_SPLIT_PRESERVE = 1400,
	EDIT_MENU_SPLIT_ESTIMATE,
	EDIT_MENU_CUT,
	EDIT_MENU_COPY,
	EDIT_MENU_PASTE,
	EDIT_MENU_SELECT_ALL,
	EDIT_MENU_ADD_TO_DICT,
	EDIT_MENU_SUGGESTION,
	EDIT_MENU_SUGGESTIONS,
	EDIT_MENU_THESAURUS = 1450,
	EDIT_MENU_THESAURUS_SUGS,
	EDIT_MENU_DIC_LANGUAGE = 1600,
	EDIT_MENU_DIC_LANGS,
	EDIT_MENU_THES_LANGUAGE = 1700,
	EDIT_MENU_THES_LANGS
};

SubsTextEditCtrl::SubsTextEditCtrl(wxWindow* parent, wxSize wsize, long style, agi::Context *context)
: ScintillaTextCtrl(parent, -1, "", wxDefaultPosition, wsize, style)
, spellchecker(SpellCheckerFactory::GetSpellChecker())
, context(context)
, calltip_position(0)
{
	// Set properties
	SetWrapMode(wxSTC_WRAP_WORD);
	SetMarginWidth(1,0);
	UsePopUp(false);
	SetStyles();

	// Set hotkeys
	CmdKeyClear(wxSTC_KEY_RETURN,wxSTC_SCMOD_CTRL);
	CmdKeyClear(wxSTC_KEY_RETURN,wxSTC_SCMOD_NORM);
	CmdKeyClear(wxSTC_KEY_TAB,wxSTC_SCMOD_NORM);
	CmdKeyClear(wxSTC_KEY_TAB,wxSTC_SCMOD_SHIFT);
	CmdKeyClear('D',wxSTC_SCMOD_CTRL);
	CmdKeyClear('L',wxSTC_SCMOD_CTRL);
	CmdKeyClear('L',wxSTC_SCMOD_CTRL | wxSTC_SCMOD_SHIFT);
	CmdKeyClear('T',wxSTC_SCMOD_CTRL);
	CmdKeyClear('T',wxSTC_SCMOD_CTRL | wxSTC_SCMOD_SHIFT);
	CmdKeyClear('U',wxSTC_SCMOD_CTRL);

	using std::bind;

	Bind(wxEVT_CHAR_HOOK, &SubsTextEditCtrl::OnKeyDown, this);

	Bind(wxEVT_COMMAND_MENU_SELECTED, bind(&SubsTextEditCtrl::Cut, this), EDIT_MENU_CUT);
	Bind(wxEVT_COMMAND_MENU_SELECTED, bind(&SubsTextEditCtrl::Copy, this), EDIT_MENU_COPY);
	Bind(wxEVT_COMMAND_MENU_SELECTED, bind(&SubsTextEditCtrl::Paste, this), EDIT_MENU_PASTE);
	Bind(wxEVT_COMMAND_MENU_SELECTED, bind(&SubsTextEditCtrl::SelectAll, this), EDIT_MENU_SELECT_ALL);

	if (context) {
		Bind(wxEVT_COMMAND_MENU_SELECTED, bind(&cmd::call, "edit/line/split/preserve", context), EDIT_MENU_SPLIT_PRESERVE);
		Bind(wxEVT_COMMAND_MENU_SELECTED, bind(&cmd::call, "edit/line/split/estimate", context), EDIT_MENU_SPLIT_ESTIMATE);
	}

	Bind(wxEVT_STC_STYLENEEDED, &SubsTextEditCtrl::UpdateCallTip, this);

	Bind(wxEVT_CONTEXT_MENU, &SubsTextEditCtrl::OnContextMenu, this);

	OPT_SUB("Subtitle/Edit Box/Font Face", &SubsTextEditCtrl::SetStyles, this);
	OPT_SUB("Subtitle/Edit Box/Font Size", &SubsTextEditCtrl::SetStyles, this);
	Subscribe("Normal");
	Subscribe("Comment");
	Subscribe("Drawing");
	Subscribe("Brackets");
	Subscribe("Slashes");
	Subscribe("Tags");
	Subscribe("Error");
	Subscribe("Parameters");
	Subscribe("Line Break");
	Subscribe("Karaoke Template");
	Subscribe("Karaoke Variable");

	OPT_SUB("Subtitle/Highlight/Syntax", &SubsTextEditCtrl::UpdateStyle, this);
	static wxStyledTextEvent evt;
	OPT_SUB("App/Call Tips", &SubsTextEditCtrl::UpdateCallTip, this, std::ref(evt));
}

SubsTextEditCtrl::~SubsTextEditCtrl() {
}

void SubsTextEditCtrl::Subscribe(std::string const& name) {
	OPT_SUB("Colour/Subtitle/Syntax/" + name, &SubsTextEditCtrl::SetStyles, this);
	OPT_SUB("Colour/Subtitle/Syntax/Background/" + name, &SubsTextEditCtrl::SetStyles, this);
	OPT_SUB("Colour/Subtitle/Syntax/Bold/" + name, &SubsTextEditCtrl::SetStyles, this);
}

BEGIN_EVENT_TABLE(SubsTextEditCtrl,wxStyledTextCtrl)
	EVT_KILL_FOCUS(SubsTextEditCtrl::OnLoseFocus)

	EVT_MENU(EDIT_MENU_ADD_TO_DICT,SubsTextEditCtrl::OnAddToDictionary)
	EVT_MENU_RANGE(EDIT_MENU_SUGGESTIONS,EDIT_MENU_THESAURUS-1,SubsTextEditCtrl::OnUseSuggestion)
	EVT_MENU_RANGE(EDIT_MENU_THESAURUS_SUGS,EDIT_MENU_DIC_LANGUAGE-1,SubsTextEditCtrl::OnUseSuggestion)
	EVT_MENU_RANGE(EDIT_MENU_DIC_LANGS,EDIT_MENU_THES_LANGUAGE-1,SubsTextEditCtrl::OnSetDicLanguage)
	EVT_MENU_RANGE(EDIT_MENU_THES_LANGS,EDIT_MENU_THES_LANGS+100,SubsTextEditCtrl::OnSetThesLanguage)
END_EVENT_TABLE()

void SubsTextEditCtrl::OnLoseFocus(wxFocusEvent &event) {
	CallTipCancel();
	event.Skip();
}

void SubsTextEditCtrl::OnKeyDown(wxKeyEvent &event) {
	// Workaround for wxSTC eating tabs.
	if (event.GetKeyCode() == WXK_TAB) {
		Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
	}
	event.Skip();
}

void SubsTextEditCtrl::SetSyntaxStyle(int id, wxFont &font, std::string const& name) {
	StyleSetFont(id, font);
	StyleSetBold(id, OPT_GET("Colour/Subtitle/Syntax/Bold/" + name)->GetBool());
	StyleSetForeground(id, to_wx(OPT_GET("Colour/Subtitle/Syntax/" + name)->GetColor()));
	const agi::OptionValue *background = OPT_GET("Colour/Subtitle/Syntax/Background/" + name);
	if (background->GetType() == agi::OptionValue::Type_Color)
		StyleSetBackground(id, to_wx(background->GetColor()));
}

void SubsTextEditCtrl::SetStyles() {
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.SetEncoding(wxFONTENCODING_DEFAULT); // this solves problems with some fonts not working properly
	wxString fontname = lagi_wxString(OPT_GET("Subtitle/Edit Box/Font Face")->GetString());
	if (!fontname.empty()) font.SetFaceName(fontname);
	font.SetPointSize(OPT_GET("Subtitle/Edit Box/Font Size")->GetInt());

	namespace ss = agi::ass::SyntaxStyle;
	SetSyntaxStyle(ss::NORMAL, font, "Normal");
	SetSyntaxStyle(ss::COMMENT, font, "Comment");
	SetSyntaxStyle(ss::DRAWING, font, "Drawing");
	SetSyntaxStyle(ss::OVERRIDE, font, "Brackets");
	SetSyntaxStyle(ss::PUNCTUATION, font, "Slashes");
	SetSyntaxStyle(ss::TAG, font, "Tags");
	SetSyntaxStyle(ss::ERROR, font, "Error");
	SetSyntaxStyle(ss::PARAMETER, font, "Parameters");
	SetSyntaxStyle(ss::LINE_BREAK, font, "Line Break");
	SetSyntaxStyle(ss::KARAOKE_TEMPLATE, font, "Karaoke Template");
	SetSyntaxStyle(ss::KARAOKE_VARIABLE, font, "Karaoke Variable");

	// Misspelling indicator
	IndicatorSetStyle(0,wxSTC_INDIC_SQUIGGLE);
	IndicatorSetForeground(0,wxColour(255,0,0));
}

void SubsTextEditCtrl::UpdateStyle() {
	StartStyling(0,255);

	std::string text = GetTextRaw().data();

	if (!OPT_GET("Subtitle/Highlight/Syntax")->GetBool()) {
		SetStyling(text.size(), 0);
		return;
	}

	if (text.empty()) return;

	AssDialogue *diag = context ? context->selectionController->GetActiveLine() : 0;
	bool template_line = diag && diag->Comment && diag->Effect.Lower().StartsWith("template");

	auto tokens = agi::ass::TokenizeDialogueBody(text);
	for (auto const& style_range : agi::ass::SyntaxHighlight(text, tokens, template_line, spellchecker.get()))
		SetStyling(style_range.length, style_range.type);
}

/// @brief Update call tip
void SubsTextEditCtrl::UpdateCallTip(wxStyledTextEvent &) {
	UpdateStyle();

	if (!OPT_GET("App/Call Tips")->GetBool()) return;

	if (!calltip_provider)
		calltip_provider.reset(new agi::CalltipProvider);

	std::string text = GetTextRaw().data();
	auto tokens = agi::ass::TokenizeDialogueBody(text);
	int pos = GetCurrentPos();

	agi::Calltip new_calltip = calltip_provider->GetCalltip(tokens, text, GetCurrentPos());

	if (new_calltip.text.empty()) {
		CallTipCancel();
		return;
	}

	if (!CallTipActive() || calltip_position != new_calltip.tag_position || calltip_text != new_calltip.text)
		CallTipShow(new_calltip.tag_position, to_wx(new_calltip.text));

	calltip_position = new_calltip.tag_position;
	calltip_text = new_calltip.text;

	CallTipSetHighlight(new_calltip.highlight_start, new_calltip.highlight_end);
}

void SubsTextEditCtrl::SetTextTo(wxString text) {
	SetEvtHandlerEnabled(false);
	Freeze();

	int from=0,to=0;
	GetSelection(&from,&to);

	SetText(text);
	UpdateStyle();

	// Restore selection
	SetSelectionU(GetReverseUnicodePosition(from), GetReverseUnicodePosition(to));

	SetEvtHandlerEnabled(true);
	Thaw();
}


void SubsTextEditCtrl::Paste() {
	wxString data = GetClipboard();

	data.Replace("\r\n", "\\N");
	data.Replace("\n", "\\N");
	data.Replace("\r", "\\N");

	int from = GetReverseUnicodePosition(GetSelectionStart());
	int to = GetReverseUnicodePosition(GetSelectionEnd());

	wxString old = GetText();
	SetText(old.Left(from) + data + old.Mid(to));

	int sel_start = GetUnicodePosition(from + data.size());
	SetSelectionStart(sel_start);
	SetSelectionEnd(sel_start);
	UpdateStyle();
}

void SubsTextEditCtrl::OnContextMenu(wxContextMenuEvent &event) {
	wxPoint pos = event.GetPosition();
	int activePos;
	if (pos == wxDefaultPosition) {
		activePos = GetCurrentPos();
	}
	else {
		activePos = PositionFromPoint(ScreenToClient(pos));
	}

	currentWordPos = GetReverseUnicodePosition(activePos);
	currentWord = from_wx(GetWordAtPosition(currentWordPos));

	wxMenu menu;
	if (!currentWord.empty()) {
		if (spellchecker)
			AddSpellCheckerEntries(menu);
		AddThesaurusEntries(menu);
	}

	// Standard actions
	menu.Append(EDIT_MENU_CUT,_("Cu&t"))->Enable(GetSelectionStart()-GetSelectionEnd() != 0);
	menu.Append(EDIT_MENU_COPY,_("&Copy"))->Enable(GetSelectionStart()-GetSelectionEnd() != 0);
	menu.Append(EDIT_MENU_PASTE,_("&Paste"))->Enable(CanPaste());
	menu.AppendSeparator();
	menu.Append(EDIT_MENU_SELECT_ALL,_("Select &All"));

	// Split
	if (context) {
		menu.AppendSeparator();
		menu.Append(EDIT_MENU_SPLIT_PRESERVE,_("Split at cursor (preserve times)"));
		menu.Append(EDIT_MENU_SPLIT_ESTIMATE,_("Split at cursor (estimate times)"));
	}

	PopupMenu(&menu);
}

void SubsTextEditCtrl::AddSpellCheckerEntries(wxMenu &menu) {
	sugs = spellchecker->GetSuggestions(currentWord);
	if (spellchecker->CheckWord(currentWord)) {
		if (sugs.empty())
			menu.Append(EDIT_MENU_SUGGESTION,_("No spell checker suggestions"))->Enable(false);
		else {
			wxMenu *subMenu = new wxMenu;
			for (size_t i = 0; i < sugs.size(); ++i)
				subMenu->Append(EDIT_MENU_SUGGESTIONS+i, to_wx(sugs[i]));

			menu.Append(-1, wxString::Format(_("Spell checker suggestions for \"%s\""), to_wx(currentWord)), subMenu);
		}
	}
	else {
		if (sugs.empty())
			menu.Append(EDIT_MENU_SUGGESTION,_("No correction suggestions"))->Enable(false);

		for (size_t i = 0; i < sugs.size(); ++i)
			menu.Append(EDIT_MENU_SUGGESTIONS+i, to_wx(sugs[i]));

		// Append "add word"
		menu.Append(EDIT_MENU_ADD_TO_DICT, wxString::Format(_("Add \"%s\" to dictionary"), to_wx(currentWord)))->Enable(spellchecker->CanAddWord(currentWord));
	}

	// Append language list
	menu.Append(-1,_("Spell checker language"), GetLanguagesMenu(
		EDIT_MENU_DIC_LANGS,
		lagi_wxString(OPT_GET("Tool/Spell Checker/Language")->GetString()),
		to_wx(spellchecker->GetLanguageList())));
	menu.AppendSeparator();
}

void SubsTextEditCtrl::AddThesaurusEntries(wxMenu &menu) {
	if (!thesaurus)
		thesaurus.reset(new Thesaurus);

	std::vector<Thesaurus::Entry> results;
	thesaurus->Lookup(currentWord, &results);

	thesSugs.clear();

	if (results.size()) {
		wxMenu *thesMenu = new wxMenu;

		int curThesEntry = 0;
		for (auto const& result : results) {
			// Single word, insert directly
			if (result.second.size() == 1) {
				thesMenu->Append(EDIT_MENU_THESAURUS_SUGS+curThesEntry, lagi_wxString(result.first));
				thesSugs.push_back(result.first);
				++curThesEntry;
			}
			// Multiple, create submenu
			else {
				wxMenu *subMenu = new wxMenu;
				for (auto const& sug : result.second) {
					subMenu->Append(EDIT_MENU_THESAURUS_SUGS+curThesEntry, to_wx(sug));
					thesSugs.push_back(sug);
					++curThesEntry;
				}

				thesMenu->Append(-1, to_wx(result.first), subMenu);
			}
		}

		menu.Append(-1, wxString::Format(_("Thesaurus suggestions for \"%s\""), to_wx(currentWord)), thesMenu);
	}
	else
		menu.Append(EDIT_MENU_THESAURUS,_("No thesaurus suggestions"))->Enable(false);

	// Append language list
	menu.Append(-1,_("Thesaurus language"), GetLanguagesMenu(
		EDIT_MENU_THES_LANGS,
		lagi_wxString(OPT_GET("Tool/Thesaurus/Language")->GetString()),
		to_wx(thesaurus->GetLanguageList())));
	menu.AppendSeparator();
}

wxMenu *SubsTextEditCtrl::GetLanguagesMenu(int base_id, wxString const& curLang, wxArrayString const& langs) {
	wxMenu *languageMenu = new wxMenu;
	languageMenu->AppendRadioItem(base_id, _("Disable"))->Check(curLang.empty());

	for (size_t i = 0; i < langs.size(); ++i) {
		const wxLanguageInfo *info = wxLocale::FindLanguageInfo(langs[i]);
		languageMenu->AppendRadioItem(base_id + i + 1, info ? info->Description : langs[i])->Check(langs[i] == curLang);
	}

	return languageMenu;
}

void SubsTextEditCtrl::OnAddToDictionary(wxCommandEvent &) {
	if (spellchecker) spellchecker->AddWord(currentWord);
	UpdateStyle();
	SetFocus();
}

void SubsTextEditCtrl::OnUseSuggestion(wxCommandEvent &event) {
	std::string suggestion;
	int sugIdx = event.GetId() - EDIT_MENU_THESAURUS_SUGS;
	if (sugIdx >= 0) {
		suggestion = lagi_wxString(thesSugs[sugIdx]);
	}
	else {
		suggestion = sugs[event.GetId() - EDIT_MENU_SUGGESTIONS];
	}

	// Strip suggestion of parenthesis
	size_t pos = suggestion.find("(");
	if (pos != suggestion.npos)
		suggestion.resize(pos - 1);

	// Get boundaries of text being replaced
	int start, end;
	GetBoundsOfWordAtPosition(currentWordPos, start, end);

	wxString text = GetText();
	SetText(text.Left(std::max(0, start)) + to_wx(suggestion) + text.Mid(end));

	// Set selection
	SetSelectionU(start, start+suggestion.size());
	SetFocus();
}

void SubsTextEditCtrl::OnSetDicLanguage(wxCommandEvent &event) {
	std::vector<std::string> langs = spellchecker->GetLanguageList();

	int index = event.GetId() - EDIT_MENU_DIC_LANGS - 1;
	std::string lang;
	if (index >= 0)
		lang = langs[index];

	OPT_SET("Tool/Spell Checker/Language")->SetString(lang);

	UpdateStyle();
}

void SubsTextEditCtrl::OnSetThesLanguage(wxCommandEvent &event) {
	if (!thesaurus) return;

	std::vector<std::string> langs = thesaurus->GetLanguageList();

	int index = event.GetId() - EDIT_MENU_THES_LANGS - 1;
	std::string lang;
	if (index >= 0) lang = langs[index];
	OPT_SET("Tool/Thesaurus/Language")->SetString(lang);

	UpdateStyle();
}
