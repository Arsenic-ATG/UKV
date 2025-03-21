/**
 * @file docs.h
 * @author Ashot Vardanian
 * @date 27 Jun 2022
 *
 * @brief C bindings for collections of @b Documents.
 * It extends the basic "ukv.h" towards values storing hierarchical documents.
 * Examples: JSONs, MsgPacks, BSONs and a number of other similar formats.
 * Yet no guarantees are provided regarding the internal representation of the
 * values, so if if you want to access same values through binary interface,
 * you may not get the exact same bytes as you have provided in.
 *
 * @section Number of Keys vs Number of Fields
 * One of the biggest questions to API is preferring "Zips" vs the "Cartesian Product"
 * of "Key" and "Field" combinations. When writing we may want to discard a certain
 * subset of fields in every document, but we may also be interested in having a
 * more targetted approach.
 *
 * @section Type Checking and Casting
 * Ideally, the data shouldn't be parsed more than once, to avoid performance loss.
 * So the primary interfaces of Docs Store are type-agnostic. Vectorized "gather"
 * operations perform the best effort to convert into the requested format, but
 * it's not always possible.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ukv/db.h"

/*********************************************************/
/*****************   Structures & Consts  ****************/
/*********************************************************/

/**
 * @brief Formats describing contents of collections.
 * The low-level interface
 *
 * Many of the numerical values are set to their RFC proposal numbers.
 * https://en.wikipedia.org/wiki/List_of_RFCs
 */
typedef enum {
    ukv_format_binary_k = 0,
    ukv_format_graph_k = 1,
    ukv_format_doc_k = 2,
    ukv_format_table_k = 3,

    // Flexible dynamically-typed document formats
    // https://github.com/msgpack/msgpack/blob/master/spec.md#type-system
    ukv_format_msgpack_k = 11,
    ukv_format_bson_k = 12,
    ukv_format_ubjson_k = 13,
    ukv_format_json_k = 7159,
    ukv_format_cbor_k = 7049,

    // Patches and modifiers to documents
    // https://stackoverflow.com/a/64882070/2766161
    ukv_format_json_patch_k = 6902,       // RFC
    ukv_format_json_merge_patch_k = 7386, // RFC

    ukv_format_csv_k = 4180,
    ukv_format_arrow_k = 14,
    ukv_format_parquet_k = 15,

    // Generic text-based formats, that  generally come in long chunks
    // would benefit from compression and may require full-text search.
    ukv_format_text_k = 20,
    ukv_format_text_xml_k = 3470,
    ukv_format_text_html_k = 1866,

    // Image formats
    ukv_format_img_jpeg200_k = 3745, // RFC
    ukv_format_img_jpeg_k = 1314,    // RFC
    ukv_format_img_png_k = 2083,     // RFC
    ukv_format_img_gif_k = 51,
    ukv_format_img_webp_k = 52,

} ukv_format_t;

/**
 * Type IDs needed to describe the values stored in the leafs of
 * hierarchical documents. Most types mimic what's present in
 * Apache Arrow. Most often the `ukv_type_i64_k` and `ukv_type_f64_k`
 * are used.
 */
typedef enum {
    ukv_type_null_k = 0,
    ukv_type_bool_k = 1,
    ukv_type_uuid_k = 2,

    ukv_type_i8_k = 10,
    ukv_type_i16_k = 11,
    ukv_type_i32_k = 12,
    ukv_type_i64_k = 13,

    ukv_type_u8_k = 20,
    ukv_type_u16_k = 21,
    ukv_type_u32_k = 22,
    ukv_type_u64_k = 23,

    ukv_type_f16_k = 30,
    ukv_type_f32_k = 31,
    ukv_type_f64_k = 32,

    ukv_type_bin_k = 40,
    ukv_type_str_k = 41,

    ukv_type_any_k = 0xFFFFFFFF,
} ukv_type_t;

extern ukv_format_t ukv_format_docs_internal_k;

/*********************************************************/
/*****************	 Primary Functions	  ****************/
/*********************************************************/

/**
 * @brief The primary "setter" interface for sub-document-level data.
 * Is an extension of the @see `ukv_write` function for structured vals.
 *
 * @param[in] collections  Must have collections storing only docs!
 * @param[in] fields       Optional JSON-Pointer strings for field paths.
 * @param[in] format       Imported `values` format, which will be converted
 *                         to some internal representation. On retrieval, a
 *                         different format can be requested. Like importing
 *                         JSONs & BSONs from Mongo, but later exporting
 *                         Apache Arrow Tables.
 *
 * @section Supported Formats
 * > ukv_format_json_k
 *      Most frequently used text-based format.
 * > ukv_format_msgpack_k, ukv_format_bson_k
 * > ukv_format_ubjson_k, ukv_format_cbor_k
 *      Commonly used binary alternatives to JSON.
 * > ukv_format_json_patch_k, ukv_format_json_merge_patch_k
 *      Describe a set of transforms to be applied at document & sub-document
 *      level. Transforms themselves are standardized and packed in JSON.
 *
 * @section Slicing Docs and Inferring IDs
 * In other interfaces it's necessary to explicitly provide the @c `ukv_key_t`s
 * and the number of input entries. With documents, if an array of objects is
 * supplied as `values[0]`, we slice it into separate objects.
 * With documents, we can often infer the ID from the documents @b "_id" field,
 * similar to MongoDB and ElasticSearch.
 */
void ukv_docs_write( //
    ukv_t const db,
    ukv_txn_t const txn,
    ukv_size_t const tasks_count,

    ukv_col_t const* collections,
    ukv_size_t const collections_stride,

    ukv_key_t const* keys,
    ukv_size_t const keys_stride,

    ukv_str_view_t const* fields,
    ukv_size_t const fields_stride,

    ukv_1x8_t const* presences,

    ukv_val_len_t const* offsets,
    ukv_size_t const offsets_stride,

    ukv_val_len_t const* lengths,
    ukv_size_t const lengths_stride,

    ukv_val_ptr_t const* values,
    ukv_size_t const values_stride,

    ukv_options_t const options,
    ukv_format_t const format,
    ukv_type_t const type,

    ukv_arena_t* arena,
    ukv_error_t* error);

/**
 * @brief The primary "getter" interface for sub-document-level data.
 * Is an extension of the @see `ukv_read` function for structured vals.
 *
 * @param[in] collections  Must have collections storing only docs!
 * @param[in] fields       Optional JSON-Pointer strings for field paths.
 * @param[in] format       Imported `values` format, which will be converted
 *                         to some internal representation. On retrieval, a
 *                         different format can be requested. Like importing
 *                         JSONs & BSONs from Mongo, but later exporting
 *                         Apache Arrow Tables.
 * @param[in] type         A binary type to which entries have to be converted to.
 *                         Only applies if @param format is set to `ukv_format_binary_k`
 *                         and @param field is not NULL.
 *
 * @section Supported Formats
 * > ukv_format_json_k
 *      Most frequently used text-based format.
 * > ukv_format_msgpack_k, ukv_format_bson_k
 * > ukv_format_ubjson_k, ukv_format_cbor_k
 *      Commonly used binary alternatives to JSON.
 * > ukv_format_json_patch_k, ukv_format_json_merge_patch_k
 *      Describe a set of transforms to be applied at document & sub-document
 *      level. Transforms themselves are standardized and packed in JSON.
 */
void ukv_docs_read( //
    ukv_t const db,
    ukv_txn_t const txn,
    ukv_size_t const tasks_count,

    ukv_col_t const* collections,
    ukv_size_t const collections_stride,

    ukv_key_t const* keys,
    ukv_size_t const keys_stride,

    ukv_str_view_t const* fields,
    ukv_size_t const fields_stride,

    ukv_options_t const options,
    ukv_format_t const format,
    ukv_type_t const type,

    ukv_1x8_t** found_presences,

    ukv_val_len_t** found_offsets,
    ukv_val_len_t** found_lengths,
    ukv_val_ptr_t* found_values,

    ukv_arena_t* arena,
    ukv_error_t* error);

/**
 * @brief Describes the statistics (presence) of select or all fields among
 * specified documents. Will export a histogram of frequencies of every @c `ukv_type_t`
 * under every field. Can be used as a preparation step before `ukv_docs_gather`
 * or `ukv_docs_read`.
 *
 * @param[in] keys      Optional. Passing nothing will find all the unique keys
 *                      appearing in all the documents. That might be a very heavy
 *                      query for big collections of flexible schema documents.
 */
void ukv_docs_gist( //
    ukv_t const db,
    ukv_txn_t const txn,
    ukv_size_t const docs_count,

    ukv_col_t const* collections,
    ukv_size_t const collections_stride,

    ukv_key_t const* keys,
    ukv_size_t const keys_stride,

    ukv_options_t const options,

    ukv_size_t* found_fields_count,
    ukv_val_len_t** found_offsets,
    ukv_str_view_t* found_fields,

    ukv_arena_t* arena,
    ukv_error_t* error);

/**
 * @brief The vectorized "gather" interface, that collects, type-checks and
 * casts (N*M) from M fields in N docs into a @b columnar format.
 *
 * @param[in] fields
 *              JSON-Pointer paths to scalars in desired documents.
 *              If `fields_stride` is set to zero, we assume that all paths
 *              are concatenated with a NULL-character delimiter.
 *
 * @param[out] columns_validities
 *              Bitset, which will indicate validity of gather objects.
 *              It must contain enough bits to consecutively stores the validity
 *              indicators for every cell in the exported table of scalars.
 *              The layout is:
 *                  * 1st column bitset: `docs_count` bits rounded up to 8 multiple.
 *                  * 2nd column bitset: `docs_count` bits rounded up to 8 multiple.
 *                  * 3rd column bitset: `docs_count` bits rounded up to 8 multiple.
 *              And so on for `fields_count` columns.
 *              Indexing (little-endian vs BIG-endian) is identical to Apache Arrow.
 *              The @param columns_conversions and @param columns_collisions have
 *              same sizes and layouts, but are not supported by Apache Arrow.
 *
 * @param[out] columns_conversions
 *              An @b Optional bitset, which will indicate type conversions.
 *              Conversions mean, that the export/cast changes the semantics.
 *              We identify following type groups: booleans, integers, floats, strings.
 *              Any downcasting conversion between them will be done with best-effort,
 *              but may not be lossless. Meaning that @c `bool` to @c `int` isn't
 *              considered a downcast, same as @c `bool` to @c `double`.
 *
 * @param[out] columns_collisions
 *              An @b Optional bitset, which will indicate key collisions.
 *              Collisions imply, that a key was found, but it's internal
 *              contents can't be converted to the requested scalar type.
 *
 * @param[out] columns_scalars
 *              Buffers for scalars to be exported to.
 *              The ordering of it's element is identical to @param columns_validities,
 *              but every column only needs: `docs_count * sizeof(scalar)`
 *
 * @section Strings Layout
 * Texts will be appended with a null-termination character, binaries - will not.
 * Offsets and lengths will be organized in a @b column-major layout with `docs_count`
 * entries in every column, but the contents of the joined string will be organized
 * in a @b row-major order. It will make the data easier to pass into bulk text-search
 * systems or Language Models training pipelines.
 *
 * @section Apache Arrow
 * We may have used Apache Arrow @c `RecordBatch` directly with @c `ArrowSchema`
 * or @c `ArrowArray`. It, however, would be inconsistent with other UKV APIs.
 * To go from the response of this function to Arrow: @see `ukv/arrow.h`.
 *
 * @section Joins
 * A user may want to join fields stored under different same keys in different collections.
 * That should be implemented as two (or multiple) separate requests for space-efficiency.
 */
void ukv_docs_gather( //
    ukv_t const db,
    ukv_txn_t const txn,
    ukv_size_t const docs_count,
    ukv_size_t const fields_count,

    ukv_col_t const* collections,
    ukv_size_t const collections_stride,

    ukv_key_t const* keys,
    ukv_size_t const keys_stride,

    ukv_str_view_t const* fields,
    ukv_size_t const fields_stride,

    ukv_type_t const* types,
    ukv_size_t const types_stride,

    ukv_options_t const options,

    ukv_1x8_t*** columns_validities,
    ukv_1x8_t*** columns_conversions,
    ukv_1x8_t*** columns_collisions,
    ukv_val_ptr_t** columns_scalars,
    ukv_val_len_t*** columns_offsets,
    ukv_val_len_t*** columns_lengths,
    ukv_val_ptr_t* joined_strings,

    ukv_arena_t* arena,
    ukv_error_t* error);

#ifdef __cplusplus
} /* end extern "C" */
#endif
