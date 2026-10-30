#include "ProviderSelectDialog.h"

#include <kodi/General.h>
#include <kodi/AddonBase.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <thread>

namespace {
// String IDs in resources/language/*/strings.po
constexpr int STR_TITLE = 30919;
constexpr int STR_ACTIVE = 30906;
constexpr int STR_READY = 30910;
constexpr int STR_LOADING = 30911;
constexpr int STR_ERROR = 30912;

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
}  // namespace

CProviderSelectDialog::CProviderSelectDialog(std::vector<Entry> entries)
    : kodi::gui::CWindow("DialogProviderSelect.xml", "Default", /*asDialog=*/true),
      m_entries(std::move(entries)) {
  m_order.resize(m_entries.size());
  for (size_t i = 0; i < m_order.size(); ++i) m_order[i] = i;
  std::stable_sort(m_order.begin(), m_order.end(), [this](size_t a, size_t b) {
    return Lower(m_entries[a].label) < Lower(m_entries[b].label);
  });
}

void CProviderSelectDialog::Touch() {
  m_lastActivityMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();
}

CProviderSelectDialog::Outcome CProviderSelectDialog::Run(int idleTimeoutSec,
                                                          std::function<bool()> shouldAbort) {
  m_applied = false;
  m_override = 0;
  Touch();

  std::atomic<bool> done{false};
  std::thread watchdog;
  if (idleTimeoutSec > 0 || shouldAbort) {
    watchdog = std::thread([&]() {
      while (!done.load()) {
        for (int i = 0; i < 5 && !done.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (done.load()) break;
        if (shouldAbort && shouldAbort()) {
          m_override = 2;
          Close();
          break;
        }
        if (idleTimeoutSec > 0) {
          const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count();
          if (now - m_lastActivityMs.load() > static_cast<long long>(idleTimeoutSec) * 1000) {
            m_override = 1;
            Close();
            break;
          }
        }
      }
    });
  }

  DoModal();
  done = true;
  if (watchdog.joinable()) watchdog.join();

  if (m_applied) return Outcome::Applied;  // an explicit "Apply" always wins a race
  if (m_override == 1) return Outcome::TimedOut;
  if (m_override == 2) return Outcome::Aborted;
  return Outcome::Cancelled;
}

std::map<std::string, bool> CProviderSelectDialog::Result() const {
  std::map<std::string, bool> result;
  for (const auto& e : m_entries) result[e.name] = e.enabled;
  return result;
}

bool CProviderSelectDialog::OnInit() {
  SetControlLabel(ID_TITLE, m_title.empty() ? kodi::addon::GetLocalizedString(STR_TITLE, "Select providers") : m_title);
  FillList();
  UpdateCounter();
  SetFocusId(ID_LIST);
  return true;
}

void CProviderSelectDialog::FillList() {
  ClearList();
  for (size_t idx : m_order) {
    const Entry& e = m_entries[idx];

    std::string sub = e.country;
    std::string stateText;
    if (e.enabled && e.state == "warming") stateText = kodi::addon::GetLocalizedString(STR_LOADING, "Loading channels...");
    else if (e.enabled && e.state == "error") stateText = kodi::addon::GetLocalizedString(STR_ERROR, "Error");
    else if (e.enabled && e.state == "ready") stateText = kodi::addon::GetLocalizedString(STR_READY, "Ready");
    if (!stateText.empty()) sub += (sub.empty() ? "" : "  -  ") + stateText;

    auto item = std::make_shared<kodi::gui::CListItem>(e.label.empty() ? e.name : e.label, sub);
    if (!e.logo.empty()) {
      item->SetArt("icon", e.logo);
      item->SetArt("thumb", e.logo);
    }
    item->SetProperty("enabled", e.enabled ? "true" : "false");
    item->SetProperty("state", e.state);
    AddListItem(item);
  }
}

void CProviderSelectDialog::Toggle(int position) {
  if (position < 0 || position >= static_cast<int>(m_order.size())) return;
  Entry& e = m_entries[m_order[position]];
  e.enabled = !e.enabled;
  if (auto item = GetListItem(position)) item->SetProperty("enabled", e.enabled ? "true" : "false");
  UpdateCounter();
}

void CProviderSelectDialog::SetAll(bool enabled) {
  for (size_t pos = 0; pos < m_order.size(); ++pos) {
    m_entries[m_order[pos]].enabled = enabled;
    if (auto item = GetListItem(static_cast<int>(pos))) item->SetProperty("enabled", enabled ? "true" : "false");
  }
  UpdateCounter();
}

void CProviderSelectDialog::UpdateCounter() {
  const auto active = std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return e.enabled; });
  SetControlLabel(ID_COUNTER, std::to_string(active) + " / " + std::to_string(m_entries.size()) + " " +
                                  kodi::addon::GetLocalizedString(STR_ACTIVE, "active"));
}

bool CProviderSelectDialog::OnClick(int controlId) {
  Touch();
  switch (controlId) {
    case ID_LIST: Toggle(GetCurrentListPosition()); return true;
    case ID_BTN_ALL: SetAll(true); return true;
    case ID_BTN_NONE: SetAll(false); return true;
    case ID_BTN_APPLY: m_applied = true; Close(); return true;
    case ID_BTN_CANCEL: m_applied = false; Close(); return true;
    default: return false;
  }
}

bool CProviderSelectDialog::OnAction(ADDON_ACTION actionId) {
  Touch();
  if (actionId == ADDON_ACTION_PREVIOUS_MENU || actionId == ADDON_ACTION_NAV_BACK) {
    m_applied = false;
    Close();
    return true;
  }
  return false;
}
