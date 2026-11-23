
#include "core/catalog.hpp"

#include <algorithm>
#include <cctype>

#include "core/sha256.hpp"

namespace remotewindows::core {

std::string normalizeIdentity(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  bool lastSpace = true;
  for (unsigned char c : text) {
    if (c < 0x20 || c == 0x7f) continue;
    char lc = (char)std::tolower(c);
    if (lc == ' ') {
      if (!lastSpace) out.push_back(' ');
      lastSpace = true;
    } else {
      out.push_back(lc);
      lastSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::string makeAppId(const std::string& identity_key,
                      const std::string& launch_target) {
  auto digest = Sha256::digest_hex(identity_key + ":" + launch_target);
  return "app-" + digest.substr(0, 16);
}

std::vector<CatalogRecord> dedupeCatalog(std::vector<CatalogRecord> records) {
  // Higher priority wins; insertion order of first sight preserved.
  std::vector<CatalogRecord> out;
  std::map<std::string, size_t> index;
  for (auto& rec : records) {
    if (rec.identity_key.empty()) rec.identity_key = normalizeIdentity(rec.display_name);
    auto it = index.find(rec.identity_key);
    if (it == index.end()) {
      index[rec.identity_key] = out.size();
      out.push_back(std::move(rec));
    } else {
      CatalogRecord& kept = out[it->second];
      if (rec.priority > kept.priority) {
        kept.display_name = rec.display_name;
        kept.launch_target = rec.launch_target;
        kept.source = rec.source;
        kept.priority = rec.priority;
        if (!rec.icon_ref.empty()) kept.icon_ref = rec.icon_ref;
      }
      kept.available = kept.available || rec.available;
    }
  }
  return out;
}

void CatalogStore::addOrUpdate(CatalogRecord rec) {
  if (rec.app_id.empty())
    rec.app_id = makeAppId(rec.identity_key, rec.launch_target);
  auto it = by_identity_.find(rec.identity_key);
  if (it == by_identity_.end()) {
    by_identity_[rec.identity_key] = records_.size();
    records_.push_back(std::move(rec));
  } else {
    CatalogRecord& kept = records_[it->second];
    if (rec.priority >= kept.priority) {
      kept.display_name = rec.display_name;
      kept.launch_target = rec.launch_target;
      kept.source = rec.source;
      kept.priority = rec.priority;
    }
    kept.available = kept.available || rec.available;
  }
  ++revision_;
  ++dirty_;
}

void CatalogStore::markUnavailable(const std::string& app_id) {
  for (auto& r : records_) {
    if (r.app_id == app_id && r.available) {
      r.available = false;
      ++revision_;
      ++dirty_;
      return;
    }
  }
}

const std::vector<CatalogRecord>& CatalogStore::records() const { return records_; }
uint32_t CatalogStore::revision() const { return revision_; }
size_t CatalogStore::changeCount() const { return dirty_; }
bool CatalogStore::snapshotDirty() const { return dirty_ > 0; }

}  // namespace remotewindows::core
