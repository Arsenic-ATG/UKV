/**
 * @file backend_rocksdb.cpp
 * @author Ashot Vardanian
 *
 * @brief Embedded Persistent Key-Value Store on top of @b RocksDB.
 * It natively supports ACID transactions and iterators (range queries)
 * and is implemented via @b Log-Structured-Merge-Tree. This makes RocksDB
 * great for write-intensive operations. It's already a common engine
 * choice for various Relational Database, built on top of it.
 * Examples: Yugabyte, TiDB, and, optionally: Mongo, MySQL, Cassandra, MariaDB.
 *
 * @section @b `PlainTable` vs `BlockBasedTable` Format
 * We use fixed-length integer keys, which are natively supported by `PlainTable`.
 * It, however, doesn't support @b non-prefix-based-`Seek()` in scans.
 * Moreover, not being the default variant, its significantly less optimized,
 * so after numerous tests we decided to stick to `BlockBasedTable`.
 * https://github.com/facebook/rocksdb/wiki/PlainTable-Format
 */

#include <rocksdb/db.h>
#include <rocksdb/utilities/options_util.h>
#include <rocksdb/utilities/transaction_db.h>

#include "ukv/db.h"
#include "helpers.hpp"

using namespace unum::ukv;
using namespace unum;

using rocks_native_t = rocksdb::TransactionDB;
using rocks_status_t = rocksdb::Status;
using rocks_value_t = rocksdb::PinnableSlice;
using rocks_txn_t = rocksdb::Transaction;
using rocks_col_t = rocksdb::ColumnFamilyHandle;

/*********************************************************/
/*****************   Structures & Consts  ****************/
/*********************************************************/

ukv_col_t ukv_col_main_k = 0;
ukv_val_len_t ukv_val_len_missing_k = std::numeric_limits<ukv_val_len_t>::max();
ukv_key_t ukv_key_unknown_k = std::numeric_limits<ukv_key_t>::max();

struct key_comparator_t final : public rocksdb::Comparator {
    inline int Compare(rocksdb::Slice const& a, rocksdb::Slice const& b) const override {
        auto ai = *reinterpret_cast<ukv_key_t const*>(a.data());
        auto bi = *reinterpret_cast<ukv_key_t const*>(b.data());
        if (ai == bi)
            return 0;
        return ai < bi ? -1 : 1;
    }
    const char* Name() const override { return "Integral"; }
    void FindShortestSeparator(std::string*, const rocksdb::Slice&) const override {}
    void FindShortSuccessor(std::string* key) const override {
        auto& int_key = *reinterpret_cast<ukv_key_t*>(key->data());
        ++int_key;
    }
};

static key_comparator_t key_comparator_k = {};

struct rocks_db_t {
    std::vector<rocks_col_t*> columns;
    std::unique_ptr<rocks_native_t> native;
};

inline rocksdb::Slice to_slice(ukv_key_t const& key) noexcept {
    return {reinterpret_cast<char const*>(&key), sizeof(ukv_key_t)};
}

inline rocksdb::Slice to_slice(value_view_t value) noexcept {
    return {reinterpret_cast<const char*>(value.begin()), value.size()};
}

inline std::unique_ptr<rocks_value_t> make_value(ukv_error_t* c_error) noexcept {
    std::unique_ptr<rocks_value_t> value_uptr;
    try {
        value_uptr = std::make_unique<rocks_value_t>();
    }
    catch (...) {
        *c_error = "Fail to allocate value";
    }
    return value_uptr;
}

bool export_error(rocks_status_t const& status, ukv_error_t* c_error) {
    if (status.ok())
        return false;

    if (status.IsCorruption())
        *c_error = "Failure: DB Corruption";
    else if (status.IsIOError())
        *c_error = "Failure: IO  Error";
    else if (status.IsInvalidArgument())
        *c_error = "Failure: Invalid Argument";
    else
        *c_error = "Failure";
    return true;
}

rocks_col_t* rocks_collection(rocks_db_t& db, ukv_col_t col) {
    return col == ukv_col_main_k ? db.native->DefaultColumnFamily() : reinterpret_cast<rocks_col_t*>(col);
}

/*********************************************************/
/*****************	    C Interface 	  ****************/
/*********************************************************/

void ukv_db_open(ukv_str_view_t, ukv_t* c_db, ukv_error_t* c_error) {
    try {
        rocks_db_t* db_ptr = new rocks_db_t;
        std::vector<rocksdb::ColumnFamilyDescriptor> column_descriptors;
        rocksdb::Options options;
        rocksdb::ConfigOptions config_options;

        rocks_status_t status =
            rocksdb::LoadLatestOptions(config_options, "./tmp/rocksdb/", &options, &column_descriptors);
        if (column_descriptors.empty())
            column_descriptors.push_back({rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions()});

        rocks_native_t* native_db = nullptr;
        options.create_if_missing = true;
        options.comparator = &key_comparator_k;
        status = rocks_native_t::Open(options,
                                      rocksdb::TransactionDBOptions(),
                                      "./tmp/rocksdb/",
                                      column_descriptors,
                                      &db_ptr->columns,
                                      &native_db);

        db_ptr->native = std::unique_ptr<rocks_native_t>(native_db);

        if (!status.ok())
            *c_error = "Open Error";
        *c_db = db_ptr;
    }
    catch (...) {
        *c_error = "Open Failure";
    }
}

void write_one( //
    rocks_db_t& db,
    rocks_txn_t* txn,
    places_arg_t const& places,
    contents_arg_t const& contents,
    rocksdb::WriteOptions const& options,
    ukv_error_t* c_error) {

    auto place = places[0];
    auto content = contents[0];
    auto col = rocks_collection(db, place.col);
    auto key = to_slice(place.key);
    rocks_status_t status;

    if (txn)
        status = !content //
                     ? txn->SingleDelete(col, key)
                     : txn->Put(col, key, to_slice(content));
    else
        status = !content //
                     ? db.native->SingleDelete(options, col, key)
                     : db.native->Put(options, col, key, to_slice(content));

    export_error(status, c_error);
}

void write_many( //
    rocks_db_t& db,
    rocks_txn_t* txn,
    places_arg_t const& places,
    contents_arg_t const& contents,
    rocksdb::WriteOptions const& options,
    ukv_error_t* c_error) {

    if (txn) {
        for (std::size_t i = 0; i != places.size(); ++i) {
            auto place = places[i];
            auto content = contents[i];
            auto col = rocks_collection(db, place.col);
            auto key = to_slice(place.col);
            auto status = !content //
                              ? txn->Delete(col, key)
                              : txn->Put(col, key, to_slice(content));
            export_error(status, c_error);
        }
    }
    else {
        rocksdb::WriteBatch batch;
        for (std::size_t i = 0; i != places.size(); ++i) {
            auto place = places[i];
            auto content = contents[i];
            auto col = rocks_collection(db, place.col);
            auto key = to_slice(place.key);
            auto status = !content //
                              ? batch.Delete(col, key)
                              : batch.Put(col, key, to_slice(content));
            export_error(status, c_error);
        }

        rocks_status_t status = db.native->Write(options, &batch);
        export_error(status, c_error);
    }
}

void ukv_write( //
    ukv_t const c_db,
    ukv_txn_t const c_txn,
    ukv_size_t const c_tasks_count,

    ukv_col_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_keys,
    ukv_size_t const c_keys_stride,

    ukv_1x8_t const* c_presences,

    ukv_val_len_t const* c_offs,
    ukv_size_t const c_offs_stride,

    ukv_val_len_t const* c_lens,
    ukv_size_t const c_lens_stride,

    ukv_val_ptr_t const* c_vals,
    ukv_size_t const c_vals_stride,

    ukv_options_t const c_options,

    ukv_arena_t*,
    ukv_error_t* c_error) {

    return_if_error(c_db, c_error, uninitialized_state_k, "DataBase is uninitialized");

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    rocks_txn_t* txn = reinterpret_cast<rocks_txn_t*>(c_txn);
    strided_iterator_gt<ukv_col_t const> cols {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> keys {c_keys, c_keys_stride};
    strided_iterator_gt<ukv_val_ptr_t const> vals {c_vals, c_vals_stride};
    strided_iterator_gt<ukv_val_len_t const> offs {c_offs, c_offs_stride};
    strided_iterator_gt<ukv_val_len_t const> lens {c_lens, c_lens_stride};
    strided_iterator_gt<ukv_1x8_t const> presences {c_presences, sizeof(ukv_1x8_t)};

    places_arg_t places {cols, keys, {}, c_tasks_count};
    contents_arg_t contents {vals, offs, lens, presences, c_tasks_count};

    rocksdb::WriteOptions options;
    if (c_options & ukv_option_write_flush_k)
        options.sync = true;

    try {
        auto func = c_tasks_count == 1 ? &write_one : &write_many;
        func(db, txn, places, contents, options, c_error);
    }
    catch (...) {
        *c_error = "Write Failure";
    }
}

void measure_one( //
    rocks_db_t& db,
    rocks_txn_t* txn,
    places_arg_t const& tasks,
    rocksdb::ReadOptions const& options,
    ukv_val_ptr_t* c_found_values,
    ukv_val_len_t** c_found_offsets,
    ukv_val_len_t** c_found_lengths,
    stl_arena_t& arena,
    ukv_error_t* c_error) {

    place_t task = tasks[0];
    auto col = rocks_collection(db, task.col);
    auto key = to_slice(task.key);
    auto value_uptr = make_value(c_error);
    rocks_value_t& value = *value_uptr.get();
    rocks_status_t status = txn //
                                ? txn->Get(options, col, key, &value)
                                : db.native->Get(options, col, key, &value);

    if (!status.IsNotFound())
        if (export_error(status, c_error))
            return;

    auto exported_len = status.IsNotFound() ? ukv_val_len_missing_k : static_cast<ukv_size_t>(value.size());
    auto tape = arena.alloc<byte_t>(sizeof(ukv_size_t), c_error);
    return_on_error(c_error);

    std::memcpy(tape.begin(), &exported_len, sizeof(ukv_size_t));
    *c_found_lengths = reinterpret_cast<ukv_val_len_t*>(tape.begin());
    *c_found_offsets = nullptr;
    *c_found_values = nullptr;
}

void read_one( //
    rocks_db_t& db,
    rocks_txn_t* txn,
    places_arg_t const& tasks,
    rocksdb::ReadOptions const& options,
    ukv_val_ptr_t* c_found_values,
    ukv_val_len_t** c_found_offsets,
    ukv_val_len_t** c_found_lengths,
    stl_arena_t& arena,
    ukv_error_t* c_error) {

    place_t task = tasks[0];
    auto col = rocks_collection(db, task.col);
    auto key = to_slice(task.key);
    auto value_uptr = make_value(c_error);
    rocks_value_t& value = *value_uptr.get();
    rocks_status_t status = txn //
                                ? txn->Get(options, col, key, &value)
                                : db.native->Get(options, col, key, &value);

    if (!status.IsNotFound())
        if (export_error(status, c_error))
            return;

    auto bytes_in_value = static_cast<ukv_val_len_t>(value.size());
    auto exported_len = status.IsNotFound() ? ukv_val_len_missing_k : bytes_in_value;
    ukv_val_len_t offset = 0;
    auto tape = arena.alloc<byte_t>(sizeof(ukv_val_len_t) * 2 + bytes_in_value, c_error);
    return_on_error(c_error);

    std::memcpy(tape.begin(), &exported_len, sizeof(ukv_val_len_t));
    std::memcpy(tape.begin() + sizeof(ukv_val_len_t), &offset, sizeof(ukv_val_len_t));
    std::memcpy(tape.begin() + sizeof(ukv_val_len_t) * 2, value.data(), bytes_in_value);

    *c_found_lengths = reinterpret_cast<ukv_val_len_t*>(tape.begin());
    *c_found_offsets = *c_found_lengths + 1;
    *c_found_values = reinterpret_cast<ukv_val_ptr_t>(tape.begin() + sizeof(ukv_val_len_t) * 2);
}

void measure_many( //
    rocks_db_t& db,
    rocks_txn_t* txn,
    places_arg_t const& tasks,
    rocksdb::ReadOptions const& options,
    ukv_val_ptr_t* c_found_values,
    ukv_val_len_t** c_found_offsets,
    ukv_val_len_t** c_found_lengths,
    stl_arena_t& arena,
    ukv_error_t* c_error) {

    std::vector<rocks_col_t*> cols(tasks.count);
    std::vector<rocksdb::Slice> keys(tasks.count);
    std::vector<std::string> vals(tasks.count);
    for (std::size_t i = 0; i != tasks.size(); ++i) {
        place_t task = tasks[i];
        cols[i] = rocks_collection(db, task.col);
        keys[i] = to_slice(task.key);
    }

    std::vector<rocks_status_t> statuses = txn //
                                               ? txn->MultiGet(options, cols, keys, &vals)
                                               : db.native->MultiGet(options, cols, keys, &vals);

    ukv_size_t total_bytes = sizeof(ukv_val_len_t) * tasks.count;
    span_gt<ukv_val_len_t> lens = arena.alloc<ukv_val_len_t>(tasks.count, c_error);
    return_on_error(c_error);

    *c_found_lengths = lens.begin();
    *c_found_offsets = nullptr;
    *c_found_values = nullptr;

    for (ukv_size_t i = 0; i != tasks.count; ++i)
        lens[i] = statuses[i].IsNotFound() ? ukv_val_len_missing_k : static_cast<ukv_val_len_t>(vals[i].size());
}

void read_many( //
    rocks_db_t& db,
    rocks_txn_t* txn,
    places_arg_t const& tasks,
    rocksdb::ReadOptions const& options,
    ukv_val_ptr_t* c_found_values,
    ukv_val_len_t** c_found_offsets,
    ukv_val_len_t** c_found_lengths,
    stl_arena_t& arena,
    ukv_error_t* c_error) {

    std::vector<rocks_col_t*> cols(tasks.count);
    std::vector<rocksdb::Slice> keys(tasks.count);
    std::vector<std::string> vals(tasks.count);
    for (std::size_t i = 0; i != tasks.size(); ++i) {
        place_t task = tasks[i];
        cols[i] = rocks_collection(db, task.col);
        keys[i] = to_slice(task.key);
    }

    std::vector<rocks_status_t> statuses = txn //
                                               ? txn->MultiGet(options, cols, keys, &vals)
                                               : db.native->MultiGet(options, cols, keys, &vals);

    // 1. Estimate the total size
    ukv_size_t total_bytes = sizeof(ukv_val_len_t) * tasks.count * 2;
    for (ukv_size_t i = 0; i != tasks.count; ++i)
        total_bytes += vals[i].size();

    // 2. Allocate a tape for all the values to be fetched
    span_gt<byte_t> tape = arena.alloc<byte_t>(total_bytes, c_error);
    return_on_error(c_error);

    // 3. Fetch the data
    ukv_val_len_t* lens = reinterpret_cast<ukv_val_len_t*>(tape.begin());
    ukv_val_len_t* offs = lens + tasks.count;
    ukv_size_t exported_bytes = sizeof(ukv_val_len_t) * tasks.count * 2;
    *c_found_lengths = lens;
    *c_found_offsets = offs;
    *c_found_values = reinterpret_cast<ukv_val_ptr_t>(tape.begin() + exported_bytes);

    for (std::size_t i = 0; i != tasks.size(); ++i) {
        auto bytes_in_value = vals[i].size();
        if (bytes_in_value) {
            std::memcpy(tape.begin() + exported_bytes, vals[i].data(), bytes_in_value);
            lens[i] = static_cast<ukv_val_len_t>(bytes_in_value);
            offs[i] = reinterpret_cast<ukv_val_ptr_t>(tape.begin() + exported_bytes) - *c_found_values;
            exported_bytes += bytes_in_value;
        }
        else {
            lens[i] = ukv_val_len_missing_k;
            offs[i] = ukv_val_len_missing_k;
        }
    }
}

void ukv_read( //
    ukv_t const c_db,
    ukv_txn_t const c_txn,
    ukv_size_t const c_tasks_count,

    ukv_col_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_keys,
    ukv_size_t const c_keys_stride,

    ukv_options_t const c_options,

    ukv_1x8_t** c_found_presences,

    ukv_val_len_t** c_found_offsets,
    ukv_val_len_t** c_found_lengths,
    ukv_val_ptr_t* c_found_values,

    ukv_arena_t* c_arena,
    ukv_error_t* c_error) {

    return_if_error(c_db, c_error, uninitialized_state_k, "DataBase is uninitialized");

    if (c_txn && (c_options & ukv_option_read_track_k)) {
        *c_error = "RocksDB only supports transparent reads!";
        return;
    }

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    rocks_txn_t* txn = reinterpret_cast<rocks_txn_t*>(c_txn);
    strided_iterator_gt<ukv_col_t const> cols_stride {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> keys_stride {c_keys, c_keys_stride};

    places_arg_t tasks {cols_stride, keys_stride, {}, c_tasks_count};
    stl_arena_t arena = prepare_arena(c_arena, {}, c_error);

    rocksdb::ReadOptions options;
    if (txn && (c_options & ukv_option_txn_snapshot_k))
        options.snapshot = txn->GetSnapshot();

    try {
        if (c_tasks_count == 1) {
            auto func = c_options ? &measure_one : &read_one;
            func(db, txn, tasks, options, c_found_values, c_found_offsets, c_found_lengths, arena, c_error);
        }
        else {
            auto func = c_options ? &measure_many : &read_many;
            func(db, txn, tasks, options, c_found_values, c_found_offsets, c_found_lengths, arena, c_error);
        }
    }
    catch (...) {
        *c_error = "Read Failure";
    }
}

void ukv_scan( //
    ukv_t const c_db,
    ukv_txn_t const c_txn,
    ukv_size_t const c_min_tasks_count,

    ukv_col_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_start_keys,
    ukv_size_t const c_start_keys_stride,

    ukv_val_len_t const* c_scan_lengths,
    ukv_size_t const c_scan_lengths_stride,

    ukv_options_t const c_options,

    ukv_val_len_t** c_found_offsets,
    ukv_val_len_t** c_found_counts,
    ukv_key_t** c_found_keys,

    ukv_arena_t* c_arena,
    ukv_error_t* c_error) {

    return_if_error(c_db, c_error, uninitialized_state_k, "DataBase is uninitialized");

    return_if_error(c_txn, c_error, (c_options & ukv_option_read_track_k), "RocksDB only supports transparent reads!");

    stl_arena_t arena = prepare_arena(c_arena, {}, c_error);
    return_on_error(c_error);

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    rocks_txn_t* txn = reinterpret_cast<rocks_txn_t*>(c_txn);
    strided_iterator_gt<ukv_col_t const> cols {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> keys {c_start_keys, c_start_keys_stride};
    strided_iterator_gt<ukv_val_len_t const> lengths {c_scan_lengths, c_scan_lengths_stride};
    scans_arg_t tasks {cols, keys, lengths, c_min_tasks_count};

    // 1. Allocate a tape for all the values to be fetched
    auto offsets = arena.alloc_or_dummy<ukv_val_len_t>(tasks.count + 1, c_error, c_found_offsets);
    return_on_error(c_error);
    auto counts = arena.alloc_or_dummy<ukv_val_len_t>(tasks.count, c_error, c_found_counts);
    return_on_error(c_error);

    auto total_keys = reduce_n(tasks.lengths, tasks.count, 0ul);
    auto keys_output = *c_found_keys = arena.alloc<ukv_key_t>(total_keys, c_error).begin();
    return_on_error(c_error);

    // 2. Fetch the data
    rocksdb::ReadOptions options;
    options.fill_cache = false;

    for (ukv_size_t i = 0; i != c_min_tasks_count; ++i) {
        scan_t task = tasks[i];
        auto col = rocks_collection(db, task.col);

        std::unique_ptr<rocksdb::Iterator> it;
        try {
            it = txn ? std::unique_ptr<rocksdb::Iterator>(txn->GetIterator(options, col))
                     : std::unique_ptr<rocksdb::Iterator>(db.native->NewIterator(options, col));
        }
        catch (...) {
            *c_error = "Failed To Create Iterator";
        }

        ukv_size_t j = 0;
        for (; it->Valid() && j != task.length; j++, it->Next()) {
            std::memcpy(keys_output, it->key().data(), sizeof(ukv_key_t));
            *keys_output = static_cast<ukv_val_len_t>(it->value().size());
            ++keys_output;
            ++j;
        }

        counts[i] = j;
    }

    offsets[tasks.size()] = keys_output - *c_found_keys;
}

void ukv_size( //
    ukv_t const c_db,
    ukv_txn_t const,
    ukv_size_t const n,

    ukv_col_t const* c_cols,
    ukv_size_t const c_cols_stride,

    ukv_key_t const* c_start_keys,
    ukv_size_t const c_start_keys_stride,

    ukv_key_t const* c_end_keys,
    ukv_size_t const c_end_keys_stride,

    ukv_options_t const,

    ukv_size_t** c_found_estimates,

    ukv_arena_t* c_arena,
    ukv_error_t* c_error) {

    return_if_error(c_db, c_error, uninitialized_state_k, "DataBase is uninitialized");

    stl_arena_t arena = prepare_arena(c_arena, {}, c_error);
    return_on_error(c_error);

    *c_found_estimates = arena.alloc<ukv_size_t>(6 * n, c_error).begin();
    return_on_error(c_error);

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    strided_iterator_gt<ukv_col_t const> cols {c_cols, c_cols_stride};
    strided_iterator_gt<ukv_key_t const> start_keys {c_start_keys, c_start_keys_stride};
    strided_iterator_gt<ukv_key_t const> end_keys {c_end_keys, c_end_keys_stride};
    rocksdb::SizeApproximationOptions options;

    rocksdb::Range range;
    uint64_t approximate_size = 0;
    uint64_t keys_size = 0;
    uint64_t sst_files_size = 0;
    rocks_status_t status;

    for (ukv_size_t i = 0; i != n; ++i) {
        auto col = rocks_collection(db, cols[i]);
        ukv_key_t const min_key = start_keys[i];
        ukv_key_t const max_key = end_keys[i];
        range = rocksdb::Range(to_slice(min_key), to_slice(max_key));
        try {
            status = db.native->GetApproximateSizes(options, col, &range, 1, &approximate_size);
            if (export_error(status, c_error))
                return;
            db.native->GetIntProperty(col, "rocksdb.estimate-num-keys", &keys_size);
            db.native->GetIntProperty(col, "rocksdb.total-sst-files-size", &sst_files_size);
        }
        catch (...) {
            *c_error = "Property Read Failure";
        }

        ukv_size_t* estimates = *c_found_estimates + i * 6;
        estimates[0] = static_cast<ukv_size_t>(0);
        estimates[1] = static_cast<ukv_size_t>(keys_size);
        estimates[2] = static_cast<ukv_size_t>(0);
        estimates[3] = static_cast<ukv_size_t>(0);
        estimates[4] = approximate_size;
        estimates[5] = sst_files_size;
    }
}

void ukv_col_upsert(
    // Inputs:
    ukv_t const c_db,
    ukv_str_view_t c_col_name,
    ukv_str_view_t,
    // Outputs:
    ukv_col_t* c_col,
    ukv_error_t* c_error) {

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    if (!c_col_name || (c_col_name && !std::strlen(c_col_name))) {
        *c_col = reinterpret_cast<ukv_col_t>(db.native->DefaultColumnFamily());
        return;
    }

    for (auto handle : db.columns) {
        if (handle && handle->GetName() == c_col_name) {
            *c_col = reinterpret_cast<ukv_col_t>(handle);
            return;
        }
    }

    rocks_col_t* col = nullptr;
    rocks_status_t status = db.native->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), c_col_name, &col);
    if (!export_error(status, c_error)) {
        db.columns.push_back(col);
        *c_col = reinterpret_cast<ukv_col_t>(col);
    }
}

void ukv_col_drop(
    // Inputs:
    ukv_t const c_db,
    ukv_col_t c_col_id,
    ukv_str_view_t c_col_name,
    ukv_col_drop_mode_t c_mode,
    // Outputs:
    ukv_error_t* c_error) {

    return_if_error(c_db, c_error, uninitialized_state_k, "DataBase is uninitialized");

    auto col_name = c_col_name ? std::string_view(c_col_name) : std::string_view();
    bool invalidate = c_mode == ukv_col_drop_keys_vals_handle_k;
    return_if_error(!col_name.empty() || !invalidate,
                    c_error,
                    args_combo_k,
                    "Default collection can't be invlaidated.");

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    if (c_mode == ukv_col_drop_keys_vals_handle_k) {
        for (auto it = db.columns.begin(); it != db.columns.end(); it++) {
            if (c_col_name == (*it)->GetName() && (*it)->GetName() != "default") {
                rocks_status_t status = db.native->DropColumnFamily(*it);
                if (export_error(status, c_error))
                    return;
                db.columns.erase(it--);
                break;
            }
        }
    }

    else if (c_mode == ukv_col_drop_keys_vals_k) {
        rocksdb::WriteBatch batch;
        auto col = db.native->DefaultColumnFamily();
        auto it = std::unique_ptr<rocksdb::Iterator>(db.native->NewIterator(rocksdb::ReadOptions(), col));
        for (it->SeekToFirst(); it->Valid(); it->Next())
            batch.Delete(col, it->key());
        rocks_status_t status = db.native->Write(rocksdb::WriteOptions(), &batch);
        export_error(status, c_error);
        return;
    }

    else if (c_mode == ukv_col_drop_vals_k) {
        rocksdb::WriteBatch batch;
        auto col = db.native->DefaultColumnFamily();
        auto it = std::unique_ptr<rocksdb::Iterator>(db.native->NewIterator(rocksdb::ReadOptions(), col));
        for (it->SeekToFirst(); it->Valid(); it->Next())
            batch.Put(col, it->key(), 0);
        rocks_status_t status = db.native->Write(rocksdb::WriteOptions(), &batch);
        export_error(status, c_error);
        return;
    }
}

void ukv_col_list( //
    ukv_t const c_db,
    ukv_size_t* c_count,
    ukv_col_t** c_ids,
    ukv_val_len_t** c_offsets,
    ukv_str_view_t* c_names,
    ukv_arena_t* c_arena,
    ukv_error_t* c_error) {

    return_if_error(c_db, c_error, uninitialized_state_k, "DataBase is uninitialized");

    stl_arena_t arena = prepare_arena(c_arena, {}, c_error);
    return_on_error(c_error);

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    std::size_t cols_count = db.columns.size();

    // Every string will be null-terminated
    std::size_t strings_length = 0;
    for (auto const& column : db.columns)
        strings_length += column->GetName().size() + 1;

    // For every collection we also need to export IDs and offsets
    std::size_t scalars_space = 0;
    scalars_space += cols_count * sizeof(ukv_col_t);
    scalars_space += cols_count * sizeof(ukv_val_len_t);
    scalars_space += arrow_extra_offsets_k * sizeof(ukv_val_len_t);

    span_gt<byte_t> tape = arena.alloc<byte_t>(scalars_space + strings_length, c_error);
    return_on_error(c_error);

    auto ids = reinterpret_cast<ukv_col_t*>(tape.begin());
    auto offs = reinterpret_cast<ukv_val_len_t*>(ids + cols_count);
    auto names = reinterpret_cast<char*>(offs + cols_count + 1);
    *c_count = static_cast<ukv_size_t>(cols_count);
    *c_ids = ids;
    *c_offsets = offs;
    *c_names = names;

    for (auto const& column : db.columns) {
        auto len = column->GetName().size();
        std::memcpy(names, column->GetName().data(), len);
        names[len] = '\0';
        *ids = reinterpret_cast<ukv_col_t>(column);
        *offs = static_cast<ukv_val_len_t>(names - *c_names);
        ++ids;
        ++offs;
        names += len + 1;
    }
    *offs = static_cast<ukv_val_len_t>(names - *c_names);
}

void ukv_db_control( //
    ukv_t const,
    ukv_str_view_t,
    ukv_str_view_t* c_response,
    ukv_error_t* c_error) {
    *c_response = NULL;
    *c_error = "Controls aren't supported in this implementation!";
}

void ukv_txn_begin(
    // Inputs:
    ukv_t const c_db,
    ukv_size_t const,
    ukv_options_t const c_options,
    // Outputs:
    ukv_txn_t* c_txn,
    ukv_error_t* c_error) {

    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    rocks_txn_t* txn = reinterpret_cast<rocks_txn_t*>(*c_txn);
    rocksdb::TransactionOptions options;
    if (c_options & ukv_option_txn_snapshot_k)
        options.set_snapshot = true;
    txn = db.native->BeginTransaction(rocksdb::WriteOptions(), options, txn);
    if (!txn)
        *c_error = "Couldn't start a transaction!";
    else
        *c_txn = txn;
}

void ukv_txn_commit( //
    ukv_txn_t const c_txn,
    ukv_options_t const,
    ukv_error_t* c_error) {

    if (!c_txn)
        return;
    rocks_txn_t* txn = reinterpret_cast<rocks_txn_t*>(c_txn);
    rocks_status_t status = txn->Commit();
    export_error(status, c_error);

    // where do we flush?! in transactions and ouside
}

void ukv_arena_free(ukv_t const, ukv_arena_t c_arena) {
    if (!c_arena)
        return;
    stl_arena_t& arena = *reinterpret_cast<stl_arena_t*>(c_arena);
    delete &arena;
}

void ukv_txn_free(ukv_t const c_db, ukv_txn_t c_txn) {
    if (!c_db || !c_txn)
        return;
    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    if (!db.native)
        return;
    rocks_txn_t* txn = reinterpret_cast<rocks_txn_t*>(c_txn);
    delete txn;
}

void ukv_col_free(ukv_t const, ukv_col_t const) {
}

void ukv_db_free(ukv_t c_db) {
    if (!c_db)
        return;
    rocks_db_t& db = *reinterpret_cast<rocks_db_t*>(c_db);
    for (rocks_col_t* cf : db.columns)
        db.native->DestroyColumnFamilyHandle(cf);
    db.native.reset();
    delete &db;
}

void ukv_error_free(ukv_error_t const) {
}