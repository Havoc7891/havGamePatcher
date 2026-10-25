// SPDX-License-Identifier: MIT

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <optional>

#include "core/havGamePatcherCore.hpp"
#include "core/gameRegistry.hpp"
#include "havGamePatcherInfo.hpp"

class MainFrame final : public wxFrame
{
public:
  MainFrame()
      : wxFrame(nullptr,
                wxID_ANY,
                HAVGAMEPATCHER_NAME,
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX))
  {
    SetIcon(wxICON(APP_ICON));

    auto *panel = new wxPanel(this);
    auto *root = new wxBoxSizer(wxVERTICAL);
    auto *heading = new wxStaticText(panel, wxID_ANY, "Classic game compatibility patcher");
    auto font = heading->GetFont();
    font.MakeBold();
    font.SetPointSize(font.GetPointSize() + 2);

    heading->SetFont(font);
    root->Add(heading, 0, wxALL, 14);
    root->Add(new wxStaticText(
                  panel, wxID_ANY,
                  "Browse for a game executable, or drop it into this window.\n"
                  "A patched copy will be saved in the same folder as the selected executable."),
              0, wxLEFT | wxRIGHT | wxBOTTOM, 14);

    auto *supported = new wxBoxSizer(wxVERTICAL);
    auto *supportedHeading = new wxStaticText(panel, wxID_ANY, "Supported retail games");
    auto supportedFont = supportedHeading->GetFont();
    supportedFont.MakeBold();
    supportedHeading->SetFont(supportedFont);
    supported->Add(supportedHeading, 0, wxBOTTOM, FromDIP(4));

    wxArrayString supportedGames;

    for (const auto *game : havGamePatcher::Games())
    {
      wxString label = wxString::FromUTF8("\xE2\x80\xA2 ");
      label += wxString::FromUTF8(game->name.data(), game->name.size());

      if (!game->supportedEditions.empty())
      {
        label += " (";
        label += wxString::FromUTF8(game->supportedEditions.data(), game->supportedEditions.size());
        label += ")";
      }

      supportedGames.Add(label);
    }

    supportedGames.Sort();
    auto *supportedList = new wxStaticText(panel, wxID_ANY, wxJoin(supportedGames, '\n'));
    supportedList->Wrap(FromDIP(560));
    supported->Add(supportedList);
    root->Add(supported, 0, wxLEFT | wxRIGHT | wxBOTTOM, 14);

    auto *chooser = new wxBoxSizer(wxHORIZONTAL);
    mPath = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    chooser->Add(mPath, 1, wxRIGHT | wxEXPAND, 8);

    auto *browse = new wxButton(panel, wxID_ANY, "Browse...");
    chooser->Add(browse, 0);
    root->Add(chooser, 0, wxLEFT | wxRIGHT | wxEXPAND, 14);

    mFullscreen = new wxCheckBox(panel, wxID_ANY, "Fullscreen compatibility (experimental)");
    mFullscreen->Enable(false);
    mFullscreen->SetToolTip("Includes the joystick compatibility fix.");
    root->Add(mFullscreen, 0, wxLEFT | wxRIGHT | wxTOP, 14);

    auto *statusPanel = new wxPanel(panel);
    auto *statusLayout = new wxBoxSizer(wxVERTICAL);

    mSelectedGameRow = new wxBoxSizer(wxHORIZONTAL);
    mSelectedGameRow->Add(new wxStaticText(statusPanel, wxID_ANY, "Selected game:"), 0,
                          wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(4));
    mGameName = new wxStaticText(statusPanel, wxID_ANY, "");
    auto gameFont = mGameName->GetFont();
    gameFont.MakeBold();
    mGameName->SetFont(gameFont);
    mSelectedGameRow->Add(mGameName, 0, wxALIGN_CENTER_VERTICAL);
    mSelectedGameRow->ShowItems(false);
    statusLayout->Add(mSelectedGameRow, 0, wxBOTTOM, FromDIP(2));

    mStatus = new wxStaticText(statusPanel, wxID_ANY, "No executable selected.");
    statusLayout->Add(mStatus);

    statusPanel->SetSizer(statusLayout);
    statusPanel->SetMinSize(FromDIP(wxSize(560, 54)));
    root->Add(statusPanel, 0, wxALL, 14);
    root->AddSpacer(8);
    root->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, 14);

    auto *actions = new wxBoxSizer(wxHORIZONTAL);
    auto *about = new wxButton(panel, wxID_ABOUT, "&About...");
    actions->Add(about, 0, wxRIGHT, 8);
    actions->AddStretchSpacer();
    mPatchButton = new wxButton(panel, wxID_ANY, "Create patched copy");
    mPatchButton->Enable(false);
    actions->Add(mPatchButton, 0, wxRIGHT, 8);
    actions->Add(new wxButton(panel, wxID_CLOSE, "Close"), 0);
    root->Add(actions, 0, wxALL | wxEXPAND, 14);

    panel->SetSizer(root);
    root->Fit(panel);
    Fit();
    SetMinSize(GetSize());

    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &)
    {
      wxFileDialog dlg(this, "Select a game executable", "", "",
                       "Executable files (*.exe)|*.exe|All files (*.*)|*.*",
                       wxFD_OPEN | wxFD_FILE_MUST_EXIST);

      if (dlg.ShowModal() != wxID_OK)
      {
        return;
      }

      SelectFiles({dlg.GetPath()});
    });

    mPatchButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &)
    {
      try
      {
        auto existingOutput = havGamePatcher::ExistingOutput::reject;
        const auto destination = std::filesystem::absolute(havGamePatcher::OutputPath(mSelected));

        if (std::filesystem::is_regular_file(destination))
        {
          wxMessageDialog confirmation(this, "Replace the existing patched executable?",
                                       "Replace patched copy",
                                       wxOK | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
          confirmation.SetExtendedMessage(wxString(destination.wstring()));
          confirmation.SetOKCancelLabels("Replace", "Cancel");

          if (confirmation.ShowModal() != wxID_OK)
          {
            return;
          }

          existingOutput = havGamePatcher::ExistingOutput::replace;
        }

        auto output = havGamePatcher::Apply(mSelected, mFullscreen->GetValue(), existingOutput);
        mInspection.reset();
        mPatchButton->Enable(false);
        mFullscreen->Enable(false);
        mStatus->SetLabel("Patched copy saved successfully.");
        SetSelectedGameName({});
        wxMessageBox("Saved:\n" + wxString(output.wstring()), "Success", wxOK | wxICON_INFORMATION,
                     this);
      }
      catch (const std::exception &e)
      {
        wxMessageBox(wxString::FromUTF8(e.what()), "Patch failed", wxOK | wxICON_ERROR, this);
      }
    });

    mFullscreen->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { UpdatePatchStatus(); });
    about->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ShowAboutDialog(); });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); }, wxID_CLOSE);

    EnableFileDrops(this);
    Centre();
  }

  void SelectFiles(const wxArrayString &files)
  {
    mSelected.clear();
    mInspection.reset();
    mPath->Clear();
    mPatchButton->Enable(false);
    mFullscreen->SetValue(false);
    mFullscreen->Enable(false);
    mFullscreen->SetToolTip("Includes the joystick compatibility fix.");
    SetSelectedGameName({});

    if (files.size() != 1)
    {
      mStatus->SetLabel("Select one executable at a time.");

      return;
    }

    mPath->SetValue(files.front());

    try
    {
      mSelected = std::filesystem::path(files.front().ToStdWstring());
      mInspection = havGamePatcher::Inspect(mSelected);
      mFullscreen->SetValue(mInspection->fullscreenPatched);
      mFullscreen->Enable(mInspection->fullscreenSupported && !mInspection->fullscreenPatched);
      const auto description = mInspection->FullscreenDescription();

      if (!description.empty())
      {
        mFullscreen->SetToolTip(wxString::FromUTF8(description.data(), description.size()));
      }

      UpdatePatchStatus();
    }
    catch (const std::exception &e)
    {
      mStatus->SetLabel(wxString::FromUTF8(e.what()));
      mStatus->Wrap(FromDIP(560));
    }
  }

private:
  void SetSelectedGameName(std::string_view name)
  {
    mGameName->SetLabel(name.empty() ? wxString{} : wxString::FromUTF8(name.data(), name.size()));
    mSelectedGameRow->ShowItems(!name.empty());
    mGameName->GetParent()->Layout();
  }

  void UpdatePatchStatus()
  {
    if (!mInspection)
    {
      return;
    }

    const auto &result = *mInspection;
    wxString status = wxString::FromUTF8("\xE2\x80\xA2 Joystick compatibility: ");
    status += result.joystickPatched ? "already applied" : "ready to apply";
    status += wxString::FromUTF8("\n\xE2\x80\xA2 Fullscreen compatibility: ");

    if (result.fullscreenPatched)
    {
      status += "already applied";
    }
    else if (!result.fullscreenSupported)
    {
      status += "unavailable for this executable";
    }
    else if (mFullscreen->GetValue())
    {
      status += "ready to apply";
    }
    else
    {
      status += "available (optional)";
    }

    mStatus->SetLabel(status);
    SetSelectedGameName(result.GameName());
    mPatchButton->Enable(result.CanApply(mFullscreen->GetValue()));
  }

  void ShowAboutDialog()
  {
    wxDialog dialog(this, wxID_ANY, "About " HAVGAMEPATCHER_NAME);

    auto *content = new wxBoxSizer(wxHORIZONTAL);
    const auto iconSize = dialog.FromDIP(48);
    const wxIcon icon("APP_ICON", wxBITMAP_TYPE_ICO_RESOURCE, iconSize, iconSize);
    content->Add(new wxStaticBitmap(&dialog, wxID_ANY, icon), 0, wxRIGHT, dialog.FromDIP(16));

    auto *details = new wxBoxSizer(wxVERTICAL);
    auto *name = new wxStaticText(&dialog, wxID_ANY, HAVGAMEPATCHER_NAME);
    auto font = name->GetFont();
    font.MakeBold();
    font.SetPointSize(font.GetPointSize() + 2);
    name->SetFont(font);
    details->Add(name);
    details->Add(new wxStaticText(&dialog, wxID_ANY, "Version " HAVGAMEPATCHER_DISPLAY_VERSION), 0,
                 wxTOP, dialog.FromDIP(4));
    details->AddSpacer(dialog.FromDIP(12));

    auto *description =
        new wxStaticText(&dialog, wxID_ANY, wxString::FromUTF8(HAVGAMEPATCHER_COMMENTS));
    description->Wrap(dialog.FromDIP(360));
    details->Add(description);
    details->AddSpacer(dialog.FromDIP(12));
    details->Add(new wxStaticText(&dialog, wxID_ANY, wxString::FromUTF8(HAVGAMEPATCHER_COPYRIGHT)));

    auto *links = new wxBoxSizer(wxHORIZONTAL);
    auto *website =
        new wxHyperlinkCtrl(&dialog, wxID_ANY, "Website", HAVGAMEPATCHER_WEBSITE, wxDefaultPosition,
                            wxDefaultSize, wxHL_CONTEXTMENU | wxHL_ALIGN_LEFT | wxNO_BORDER);
    website->SetToolTip(HAVGAMEPATCHER_WEBSITE);
    links->Add(website, 0, wxRIGHT, dialog.FromDIP(16));

    auto *source = new wxHyperlinkCtrl(&dialog, wxID_ANY, "Source Code", HAVGAMEPATCHER_SOURCE_URL,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxHL_CONTEXTMENU | wxHL_ALIGN_LEFT | wxNO_BORDER);
    source->SetToolTip(HAVGAMEPATCHER_SOURCE_URL);
    links->Add(source, 0, wxRIGHT, dialog.FromDIP(16));

    auto *email = new wxHyperlinkCtrl(&dialog, wxID_ANY, "Email", "mailto:" HAVGAMEPATCHER_EMAIL,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxHL_CONTEXTMENU | wxHL_ALIGN_LEFT | wxNO_BORDER);
    email->SetToolTip(HAVGAMEPATCHER_EMAIL);
    links->Add(email);

    details->Add(links, 0, wxTOP, dialog.FromDIP(12));
    content->Add(details, 1);

    auto *layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(content, 1, wxALL | wxEXPAND, dialog.FromDIP(16));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK), 0, wxALL | wxEXPAND, dialog.FromDIP(12));
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    dialog.ShowModal();
  }

  void EnableFileDrops(wxWindow *window)
  {
    // File-drop events don't propagate to parents, so cover the child controls too
    window->DragAcceptFiles(true);
    window->Bind(wxEVT_DROP_FILES, [this](wxDropFilesEvent &event)
    { SelectFiles(wxArrayString(event.GetNumberOfFiles(), event.GetFiles())); });

    for (auto *child : window->GetChildren())
    {
      EnableFileDrops(child);
    }
  }

  wxTextCtrl *mPath{};
  wxBoxSizer *mSelectedGameRow{};
  wxStaticText *mGameName{};
  wxStaticText *mStatus{};
  wxButton *mPatchButton{};
  wxCheckBox *mFullscreen{};
  std::optional<havGamePatcher::Inspection> mInspection;
  std::filesystem::path mSelected;
};

class HavGamePatcherApp final : public wxApp
{
public:
  bool OnInit() override
  {
    SetAppearance(wxApp::Appearance::System);

    auto *frame = new MainFrame();
    wxArrayString files;

    for (int i = 1; i < argc; ++i)
    {
      files.Add(argv[i]);
    }

    if (!files.empty())
    {
      frame->SelectFiles(files);
    }

    frame->Show();

    return true;
  }
};

wxIMPLEMENT_APP(HavGamePatcherApp);
