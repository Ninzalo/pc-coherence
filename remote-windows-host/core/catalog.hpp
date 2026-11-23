
#ifndef RWH_CORE_CATALOG_HPP
#define RWH_CORE_CATALOG_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace remotewindows::core {

// FR-01: catalog records normalized from multiple sources. Dedup uses a
// stable identity key derived from normalized launch metadata (never
// localized display text). app_id is Host-generated and stable.
struct CatalogRecord {
  std::string app_id;         // stable, [A-Za-z0-9][A-Za-z0-9_-]{0,127}
  std::string display_name;
  std::string identity_key;   // dedup key (lowercased, NFKC-normalized)
  std::string launch_target;  // shortcut path / package family id
  std::string source;         // "start_menu" | "packaged" | "user_added"
  uint32_t priority = 0;      // start_menu=10, packaged=20, user_added=30
  bool available = true;
  std::string icon_ref;
};

// NFKC-lite normalization: casefold + strip control chars + collapse
// whitespace. Deterministic and dependency-free (a full NFKC table is
// out of scope for the MVP; documented as a limitation).
std::string normalizeIdentity(const std::string& text);

// app_id = first 16 hex chars of sha256(identity_key + ":" + launch_target)
// prefixed with "app-". Stable across restarts and re-scans.
std::string makeAppId(const std::string& identity_key,
                      const std::string& launch_target);

// Merge records: same identity_key => one record; higher priority wins,
// display_name kept from the higher-priority source, union of availability
// (available if ANY source reports available). Order-preserving.
std::vector<CatalogRecord> dedupeCatalog(std::vector<CatalogRecord> records);

// Monotonic catalog revision: bump on any change.
class CatalogStore {
 public:
  void addOrUpdate(CatalogRecord rec);
  void markUnavailable(const std::string& app_id);
  const std::vector<CatalogRecord>& records() const;
  uint32_t revision() const;
  size_t changeCount() const;  // changes since last snapshot
  bool snapshotDirty() const;  // true if APP_CATALOG_CHANGED should send
 private:
  std::map<std::string, size_t> by_identity_;
  std::vector<CatalogRecord> records_;
  uint32_t revision_ = 0;
  size_t dirty_ = 0;
};

}  // namespace remotewindows::core

#endif
