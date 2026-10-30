#pragma once

#include <kodi/gui/Window.h>
#include <kodi/gui/ListItem.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Modal dialog: lists every backend provider with logo, name, country/state
// and a checkbox. The user toggles providers and confirms with "Apply".
//
// Skin file: resources/skins/Default/720p/DialogProviderSelect.xml
class ATTR_DLL_LOCAL CProviderSelectDialog : public kodi::gui::CWindow {
public:
  struct Entry {
    std::string name;      // backend key (used for API calls)
    std::string label;     // display name
    std::string country;
    std::string logo;      // URL or local path
    std::string state;     // idle | warming | ready | error | ""
    bool enabled = true;
  };

  explicit CProviderSelectDialog(std::vector<Entry> entries);

  enum class Outcome { Applied, Cancelled, TimedOut, Aborted };

  void SetTitle(const std::string& title) { m_title = title; }

  // Shows the dialog modally.
  //  idleTimeoutSec > 0: closes itself after that many seconds WITHOUT any user input
  //                      (-> TimedOut), for unattended devices.
  //  shouldAbort:        polled while open; when it returns true the dialog closes
  //                      (-> Aborted), e.g. on addon shutdown.
  Outcome Run(int idleTimeoutSec = 0, std::function<bool()> shouldAbort = {});

  // provider name -> desired enabled state (all providers, not just changed)
  std::map<std::string, bool> Result() const;

  // Control IDs - must match DialogProviderSelect.xml
  static constexpr int ID_LIST = 50;
  static constexpr int ID_TITLE = 10;
  static constexpr int ID_COUNTER = 11;
  static constexpr int ID_BTN_ALL = 201;
  static constexpr int ID_BTN_NONE = 202;
  static constexpr int ID_BTN_APPLY = 203;
  static constexpr int ID_BTN_CANCEL = 204;

protected:
  bool OnInit() override;
  bool OnClick(int controlId) override;
  bool OnAction(ADDON_ACTION actionId) override;

private:
  void FillList();
  void SetAll(bool enabled);
  void Toggle(int position);
  void UpdateCounter();

  void Touch();

  std::string m_title;
  std::atomic<long long> m_lastActivityMs{0};
  std::atomic<int> m_override{0};  // 0 none, 1 timed out, 2 aborted
  std::vector<Entry> m_entries;
  // list position -> index into m_entries (list is shown sorted by label)
  std::vector<size_t> m_order;
  bool m_applied = false;
};
