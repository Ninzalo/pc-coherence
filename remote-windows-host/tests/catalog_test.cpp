
#include <gtest/gtest.h>
#include "core/catalog.hpp"

using namespace remotewindows::core;

TEST(Catalog, NormalizeIdentity) {
  EXPECT_EQ(normalizeIdentity("File  Explorer"), "file explorer");
  EXPECT_EQ(normalizeIdentity("\tMY App\x01"), "my app");
  EXPECT_EQ(normalizeIdentity("  spaced   out  "), "spaced out");
}

TEST(Catalog, AppIdStableAndWellFormed) {
  auto a = makeAppId("file explorer", "C:\\start\\Explorer.lnk");
  auto b = makeAppId("file explorer", "C:\\start\\Explorer.lnk");
  auto c = makeAppId("file explorer", "D:\\other.lnk");
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_EQ(a.substr(0, 4), "app-");
  EXPECT_EQ(a.size(), 4u + 16u);
}

TEST(Catalog, DedupePriorityAndAvailability) {
  std::vector<CatalogRecord> recs = {
      {"app-start", "Explorer", "file explorer", "start.lnk",
       "start_menu", 10, false, ""},
      {"app-packaged", "File Explorer", "file explorer", "ms-app://x",
       "packaged", 20, true, "icon.png"},
  };
  auto out = dedupeCatalog(recs);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].display_name, "File Explorer");  // packaged (20) won
  EXPECT_EQ(out[0].source, "packaged");
  EXPECT_TRUE(out[0].available);  // union of availability
}

TEST(Catalog, StoreRevisionAndDirty) {
  CatalogStore store;
  EXPECT_EQ(store.revision(), 0u);
  EXPECT_FALSE(store.snapshotDirty());
  CatalogRecord r;
  r.display_name = "Access";
  r.identity_key = "access";
  r.launch_target = "t.lnk";
  store.addOrUpdate(r);
  EXPECT_EQ(store.revision(), 1u);
  EXPECT_TRUE(store.snapshotDirty());
  store.markUnavailable(store.records().front().app_id);
  EXPECT_EQ(store.revision(), 2u);
}
