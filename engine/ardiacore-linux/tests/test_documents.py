import pathlib
import shutil
import subprocess
import tempfile
import unittest

from test_session_merge import VALUES, WRITER, cpp_value

SOURCE = pathlib.Path(__file__).resolve().parents[1] / 'src'


class DocumentsTests(unittest.TestCase):
    def test_canonical_transactions(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        folder = {'id': 'lf-a', 'profileId': 'logical', 'kind': 'documents', 'name': 'A', 'icon': None, 'collapsed': False, 'sources': [], 'filters': {'authored': True, 'reviewRequests': True}, 'createdAt': 0}
        operations = {
            'create': [{'type': 'create', 'id': 'lf-a', 'incarnation': 'folder:lf-a', 'value': folder}],
            'rename': [{'type': 'field', 'target': 'folder', 'id': 'lf-a', 'incarnation': 'folder:lf-a', 'field': 'name', 'expected': 'A', 'value': 'Renamed'}],
            'delete_folder': [{'type': 'delete', 'id': 'lf-a', 'incarnation': 'folder:lf-a'}],
            'calendar_a': [{'type': 'field', 'target': 'calendar', 'field': 'showPreview', 'expected': True, 'value': False}],
            'calendar_b': [{'type': 'field', 'target': 'calendar', 'field': 'showTimeToNext', 'expected': True, 'value': False}],
            'calendar_stale': [{'type': 'field', 'target': 'calendar', 'field': 'showPreview', 'expected': True, 'value': True}],
            'claim': [{'type': 'claim', 'id': 'lf-a', 'incarnation': 'folder:lf-a'}],
            'hidden_a': [{'type': 'member', 'target': 'hiddenCalendarIds', 'member': 'a', 'present': True}],
            'hidden_b': [{'type': 'member', 'target': 'hiddenCalendarIds', 'member': 'b', 'present': True}],
            'secret': [{'type': 'field', 'target': 'account', 'id': 'github', 'field': 'value', 'expected': None, 'value': {'login': 'x', 'token': 'forbidden'}}],
        }
        for suffix in ['b', 'c', 'd']:
            operations['create_' + suffix] = [{'type': 'create', 'id': 'lf-' + suffix, 'incarnation': 'folder:lf-' + suffix, 'value': {**folder, 'id': 'lf-' + suffix}}]
        operations['claim_c'] = [{'type': 'claim', 'id': 'lf-c', 'incarnation': 'folder:lf-c'}]
        operations['refresh_c'] = [{'type': 'refresh', 'id': 'lf-c', 'incarnation': 'folder:lf-c', 'generation': 2, 'owner': 'a', 'operations': [{'type': 'member', 'target': 'known', 'id': 'lf-c', 'incarnation': 'folder:lf-c', 'member': 'retired-fetch', 'present': True}]}]
        operations['claim_b'] = [{'type': 'claim', 'id': 'lf-b', 'incarnation': 'folder:lf-b'}]
        operations['filter_b'] = [{'type': 'field', 'target': 'folder', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'field': 'filters.authored', 'expected': True, 'value': False}]
        operations['filter_review_b'] = [{'type': 'field', 'target': 'folder', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'field': 'filters.reviewRequests', 'expected': True, 'value': False}]
        operations['refresh_b'] = [{'type': 'refresh', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'generation': 2, 'owner': 'a', 'operations': [{'type': 'member', 'target': 'known', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'member': 'late-fetch', 'present': True}]}]
        item = {'id': 'actual-item', 'source': 'github', 'url': 'https://example.test/item', 'title': 'Item', 'subtitle': '', 'icon': None, 'updatedAt': 0}
        entry = {'type': 'entry', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'expected': None}
        operations['seed_b'] = [
            {**entry, 'target': 'items', 'key': 'actual-item', 'value': item},
            {**entry, 'target': 'hidden', 'key': 'hidden-item', 'value': {**item, 'id': 'hidden-item'}},
            {**entry, 'target': 'completed', 'key': 'completed-item', 'value': {'item': {**item, 'id': 'completed-item'}, 'state': 'closed', 'at': 1}},
            {'type': 'member', 'target': 'known', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'member': 'actual-item', 'present': True},
            {'type': 'member', 'target': 'unread', 'id': 'lf-b', 'incarnation': 'folder:lf-b', 'member': 'actual-item', 'present': True},
            {'type': 'field', 'target': 'expandedStack', 'id': 'lf-b', 'field': 'value', 'expected': None, 'value': True},
        ]
        operations['wrong_completed_id'] = [{**entry, 'target': 'completed', 'key': 'wrong-id', 'value': {'item': item, 'state': 'closed', 'at': 1}}]
        operations['delete_b'] = [{'type': 'delete', 'id': 'lf-b', 'incarnation': 'folder:lf-b'}]
        operations['place_c'] = [{'type': 'place', 'id': 'lf-c', 'incarnation': 'folder:lf-c', 'after': 'lf-b', 'expected': None}]
        main = r'''
#include <cassert>
#include <optional>
#include <vector>
#include "ardia/linux/ac_linux_documents.h"
#include "ardia/linux/ac_linux_document_reducer.h"
using namespace ardia;
int main() {
  std::optional<base::DictValue> disk;
  bool fail = false;
  int writes = 0;
  auto read = [&](std::string_view name) {
    LinuxDocumentRead result;
    if (name == kLinuxLiveEnvelope && disk) {
      result.state = LinuxDocumentRead::State::kFound;
      result.value = disk->Clone();
    }
    return result;
  };
  auto write = [&](const base::DictValue& value) {
    if (fail) return false;
    disk = value.Clone(); ++writes; return true;
  };
  LinuxDocuments service("scope", "epoch", read, write);
  LinuxDocumentClient a{"a", "scope", true, true};
  LinuxDocumentClient b{"b", "scope", true, true};
  LinuxDocumentClient c{"settings", "scope", true, true};
  LinuxDocumentClient forged{"forged", "other", true, true};
  LinuxDocumentClient ordinary{"ordinary", "scope", false, true};
  LinuxDocumentClient private_client{"private", "scope", true, false};
  std::vector<base::DictValue> events;
  assert(!service.Watch(forged, [](auto) {}).ok());
  assert(!service.Watch(ordinary, [](auto) {}).ok());
  assert(!service.Watch(private_client, [](auto) {}).ok());
  assert(service.Watch(a, [&](auto event) { assert(disk); events.push_back(std::move(event)); }).ok());
  assert(service.Watch(b, [](auto) {}).ok());
  assert(service.Watch(c, [](auto) {}).ok());
  auto commit = [&](const LinuxDocumentClient& client, int sequence, const base::Value& operations, int observed = 0) {
    base::DictValue request;
    request.Set("document", "live.json"); request.Set("epoch", "epoch");
    request.Set("requestId", client.id + ":" + std::to_string(sequence));
    request.Set("observedRevision", base::Value(observed));
    request.Set("operations", operations.Clone());
    return service.Commit(client, request);
  };
'''
        main += '\n'.join(f'auto {name} = {cpp_value(value)};' for name, value in operations.items())
        main += r'''
  assert(commit(a, 1, create).ok());
  assert(commit(b, 1, calendar_a).ok());
  assert(commit(c, 1, calendar_b).ok());
  auto* calendar = disk->FindDict("payload")->FindDict("calendar");
  assert(!calendar->Find("showPreview")->GetBool());
  assert(!calendar->Find("showTimeToNext")->GetBool());
  assert(*commit(c, 2, calendar_stale).value.FindString("status") == "conflict");
  int before = writes;
  assert(commit(a, 1, create).ok());
  assert(writes == before);
  assert(!commit(a, 1, rename).ok());
  fail = true;
  assert(commit(a, 2, rename).code == "ERR_DOCUMENT_STORAGE");
  assert(writes == before);
  assert(*disk->FindDict("payload")->FindDict("folders")->FindDict("lf-a")->FindString("name") == "A");
  fail = false;
  assert(commit(a, 2, claim).ok());
  assert(commit(b, 2, delete_folder).ok());
  assert(*commit(a, 3, rename).value.FindString("status") == "conflict");
  assert(*commit(a, 3, create).value.FindString("status") == "conflict");
  assert(!disk->FindDict("payload")->FindDict("folders")->contains("lf-a"));
  assert(disk->FindDict("tombstones")->contains("lf-a"));
  assert(commit(a, 3, hidden_a).ok());
  assert(commit(c, 2, hidden_b).ok());
  assert(disk->FindDict("payload")->FindDict("calendar")->Find("hiddenCalendarIds")->GetList().size() == 2);
  assert(commit(c, 3, secret).code == "ERR_DOCUMENT_REQUEST");
  base::DictValue flush;
  flush.Set("document", "live.json"); flush.Set("epoch", "epoch");
  flush.Set("watermark", base::Value(4));
  assert(service.Flush(c, flush).code == "ERR_DOCUMENT_WATERMARK");
  flush.Set("watermark", base::Value(2));
  assert(service.Flush(c, flush).ok());
  assert(commit(a, 4, create_b).ok());
  assert(commit(b, 3, create_c).ok());
  assert(disk->FindDict("payload")->FindDict("folders")->size() == 2);
  assert(commit(a, 5, claim_b).ok());
  assert(commit(c, 3, filter_b).ok());
  assert(commit(b, 4, filter_review_b).ok());
  auto* filters = disk->FindDict("payload")->FindDict("folders")->FindDict("lf-b")->FindDict("filters");
  assert(!filters->Find("authored")->GetBool() && !filters->Find("reviewRequests")->GetBool());
  assert(commit(a, 6, refresh_b).value.Find("superseded")->GetBool());
  assert(commit(c, 4, wrong_completed_id).code == "ERR_DOCUMENT_REQUEST");
  assert(commit(c, 4, seed_b).ok());
  assert(disk->FindDict("payload")->FindDict("completed")->Find("lf-b")->GetList().size() == 1);
  assert(commit(a, 7, delete_b).ok());
  for (auto key : {"items", "hidden", "completed", "known", "unread", "expandedStacks"})
    assert(!disk->FindDict("payload")->FindDict(key)->contains("lf-b"));
  assert(*commit(b, 5, place_c).value.FindString("status") == "conflict");
  assert(*commit(b, 5, create_b).value.FindString("status") == "conflict");
  assert(commit(b, 5, create_d).ok());
  assert(!disk->FindDict("payload")->FindDict("known")->contains("lf-b"));
  assert(service.Seal(a, "close-1", 6).code == "ERR_DOCUMENT_WATERMARK");
  assert(service.Seal(a, "close-1", 7).ok());
  assert(service.Seal(a, "close-1", 7).ok());
  assert(service.Seal(a, "close-2", 7).code == "ERR_DOCUMENT_CLOSING");
  assert(commit(a, 8, hidden_b).code == "ERR_DOCUMENT_CLOSING");
  service.Unwatch(a);
  assert(service.Watch(a, [](auto) {}).ok());
  assert(commit(a, 8, hidden_b).code == "ERR_DOCUMENT_CLOSING");
  assert(!service.CancelSeal(a, "forged-close").ok());
  assert(service.CancelSeal(a, "close-1").ok());
  assert(service.CancelSeal(a, "close-1").ok());
  assert(commit(a, 8, hidden_b).ok());
  assert(!service.Seal(forged, "close-1", 0).ok());
  assert(commit(a, 9, claim_c).ok());
  fail = true;
  service.Unwatch(a);
  assert(service.Watch(a, [](auto) {}).ok());
  fail = false;
  assert(commit(a, 10, refresh_c).value.Find("superseded")->GetBool());
  assert(!disk->FindDict("payload")->FindDict("known")->contains("lf-c"));
  service.Unwatch(a);
  assert(!commit(a, 4, hidden_b).ok());
  LinuxDocuments restarted("scope", "other-epoch", read, write);
  auto restarted_snapshot = restarted.Watch(a, [](auto) {});
  assert(restarted_snapshot.ok());
  assert(*restarted_snapshot.value.FindString("epoch") == "other-epoch");
  base::DictValue retired_epoch;
  retired_epoch.Set("document", "live.json"); retired_epoch.Set("epoch", "epoch");
  retired_epoch.Set("requestId", "a:9"); retired_epoch.Set("observedRevision", base::Value(0));
  retired_epoch.Set("operations", hidden_a.Clone());
  assert(restarted.Commit(a, retired_epoch).code == "ERR_DOCUMENT_EPOCH");
  assert(LinuxDocuments::Reserved("live.json"));
  assert(LinuxDocuments::Reserved("live.canonical.json"));
  assert(!LinuxDocuments::Reserved("sync-state.nns"));
  base::DictValue newer; newer.Set("version", base::Value(2));
  assert(!NormalizeLinuxLive(newer));
  assert(NormalizeLinuxLive(base::DictValue()));
  std::optional<base::DictValue> migrated;
  int legacy_reads = 0;
  auto legacy_read = [&](std::string_view name) {
    LinuxDocumentRead result;
    if (name == kLinuxLiveEnvelope && migrated) {
      result.state = LinuxDocumentRead::State::kFound;
      result.value = migrated->Clone();
    } else if (name == kLinuxLiveDocument) {
      ++legacy_reads;
      result.state = LinuxDocumentRead::State::kFound;
      base::DictValue folders;
      folders.Set("lf-a", create.GetList()[0].GetDict().Find("value")->Clone());
      result.value.Set("folders", std::move(folders));
    }
    return result;
  };
  auto legacy_write = [&](const base::DictValue& value) { migrated = value.Clone(); return true; };
  LinuxDocuments legacy("scope", "epoch", legacy_read, legacy_write);
  auto imported = legacy.Watch(a, [](auto) {});
  assert(imported.ok());
  auto* imported_folder = migrated->FindDict("payload")->FindDict("folders")->FindDict("lf-a");
  assert(imported_folder->Find("groupStacks")->GetBool());
  assert(imported_folder->Find("showCompleted")->GetBool());
  assert(LinuxDocumentInteger(*migrated->FindDict("payload"), "version") == 1);
  base::DictValue erase;
  erase.Set("document", "live.json"); erase.Set("epoch", "epoch");
  erase.Set("requestId", "a:1"); erase.Set("observedRevision", base::Value(0));
  erase.Set("operations", delete_folder.Clone());
  assert(legacy.Commit(a, erase).ok());
  LinuxDocuments reopened("scope", "new-epoch", legacy_read, legacy_write);
  assert(reopened.Watch(a, [](auto) {}).ok());
  assert(legacy_reads == 1);
  assert(migrated->FindDict("payload")->FindDict("folders")->size() == 0);
  migrated->Set("revision", base::Value("malformed"));
  LinuxDocuments corrupt("scope", "new-epoch", legacy_read, legacy_write);
  assert(corrupt.Watch(a, [](auto) {}).code == "ERR_DOCUMENT_DATA");
  assert(legacy_reads == 1);
  for (int kind = 0; kind < 4; ++kind) {
    std::optional<base::DictValue> imported_disk;
    auto bad_read = [&](std::string_view name) {
      LinuxDocumentRead result;
      if (name == kLinuxLiveEnvelope && imported_disk) {
        result.state = LinuxDocumentRead::State::kFound;
        result.value = imported_disk->Clone();
      } else if (name == kLinuxLiveDocument) {
        result.state = LinuxDocumentRead::State::kFound;
        if (kind == 0) result.value.Set("version", base::Value(2));
        if (kind == 1) result.value.Set("version", base::Value("malformed"));
        if (kind == 2) result.value.Set("calendar", base::Value("malformed"));
        if (kind == 3) result.value.Set("version", base::Value(1));
      }
      return result;
    };
    LinuxDocuments fallback("scope", "epoch", bad_read, [&](const auto& value) { imported_disk = value.Clone(); return true; });
    assert(fallback.Watch(a, [](auto) {}).ok());
    assert(LinuxDocumentInteger(*imported_disk->FindDict("payload"), "version") == 1);
    assert(imported_disk->FindDict("payload")->FindDict("folders")->size() == 0);
  }
  assert(!events.empty());
}
'''
        with tempfile.TemporaryDirectory(prefix='ardia-documents-') as scratch:
            root = pathlib.Path(scratch)
            (root / 'base/json').mkdir(parents=True)
            (root / 'base/values.h').write_text(VALUES)
            (root / 'base/json/json_writer.h').write_text(WRITER)
            (root / 'main.cc').write_text(main)
            binary = root / 'documents'
            sources = ['ac_linux_documents.cc', 'ac_linux_document_schema.cc', 'ac_linux_document_reducer.cc']
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(root), '-I', str(SOURCE), *[str(SOURCE / 'ardia/linux' / source) for source in sources], str(root / 'main.cc'), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
