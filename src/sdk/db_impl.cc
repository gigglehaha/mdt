// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/sdk.h"
#include "sdk/db_impl.h"
#include <tera.h>

#include <gflags/gflags.h>

DECLARE_string(tera_root_dir);
DECLARE_string(tera_flag_file_path);
DECLARE_string(database_root_dir);

namespace mdt {

Status DatabaseImpl::OpenDB(const std::string& db_name, Database** db_ptr) {
    Options options;
    options.env_ = Env::Default();
    options.tera_flag_file_path_ = FLAGS_tera_flag_file_path;
    return CreateDB(options, db_name, db_ptr);
}

Status DatabaseImpl::CreateDB(const Options& options,
                              const std::string& db_name,
                              Database** db_ptr) {
    DatabaseImpl* db_impl = new DatabaseImpl(options, db_name);
    assert(db_impl);
    *db_ptr = db_impl;
    return Status::OK();
}

// Database ops
Options InitDefaultOptions(const Options& options, const std::string& db_name) {
    Options opt = options;
    std::string database_root_dir = FLAGS_database_root_dir + "/" + db_name;
    Status s = opt.env_->CreateDir(database_root_dir);
    return opt;
}

DatabaseImpl::DatabaseImpl(const Options& options, const std::string& db_name)
    : db_name_(db_name),
    options_(InitDefaultOptions(options, db_name)) {
    // create fs's dir
    fs_opt_.env_ = options.env_;
    fs_opt_.fs_path_ = FLAGS_database_root_dir + "/" + db_name + "/Filesystem/";
    fs_opt_.env_->CreateDir(fs_opt_.fs_path_);

    // create tera client
    ::tera::ErrorCode error_code;
    tera_opt_.root_path_ = FLAGS_tera_root_dir;
    tera_opt_.tera_flag_ = options.tera_flag_file_path_;
    tera_opt_.client_ = tera::Client::NewClient(tera_opt_.tera_flag_, "mdt", &error_code);
    assert(tera_opt_.client_);

    // create db schema table (kv mode)
    std::string schema_table_name = db_name + "#SchemaTable#";
    tera::TableDescriptor schema_desc(schema_table_name, true);
    //schema_desc.SetRawKey(tera::kBinary);
    tera::LocalityGroupDescriptor* schema_lg = schema_desc.AddLocalityGroup("lg");
    schema_lg->SetBlockSize(32 * 1024);
    tera_opt_.client_->CreateTable(schema_desc, &error_code);

    tera_opt_.schema_table_ = tera_opt_.client_->OpenTable(schema_table_name, &error_code);
    LOG(INFO) << "open schema table, table name " << schema_table_name <<
        ", error code " << tera::strerr(error_code);
    assert(tera_opt_.schema_table_);

    tera_adapter_.opt_ = tera_opt_;
    tera_adapter_.table_prefix_ = db_name_;
}

Status DatabaseImpl::CreateTable(const TableDescription& table_desc) {
    // insert schema into schema table
    tera::ErrorCode error_code;
    BigQueryTableSchema schema;
    AssembleTableSchema(table_desc, &schema);
    std::string schema_value;
    schema.SerializeToString(&schema_value);
    tera_adapter_.opt_.schema_table_->Put(schema.table_name(), "", "", schema_value, &error_code);
    LOG(INFO) << "Put Schema: table name " << schema.table_name() << ", size " << schema_value.size()
        << ", error code " << tera::strerr(error_code);

    // create primary key table
    std::string primary_table_name = tera_adapter_.table_prefix_ + "#pri#" + schema.table_name();
    tera::TableDescriptor primary_table_desc(primary_table_name);
    LOG(INFO) << "Create primary table name " << primary_table_name;
    primary_table_desc.SetRawKey(tera::kBinary);
    tera::LocalityGroupDescriptor* lg = primary_table_desc.AddLocalityGroup("lg");
    lg->SetBlockSize(32 * 1024);
    lg->SetCompress(tera::kSnappyCompress);
    tera::ColumnFamilyDescriptor* cf = primary_table_desc.AddColumnFamily("Location", "lg");
    cf->SetTimeToLive(0);
    tera_adapter_.opt_.client_->CreateTable(primary_table_desc, &error_code);

    // create index key table
    std::vector<IndexDescription>::const_iterator it;
    for (it = table_desc.index_descriptor_list.begin();
         it != table_desc.index_descriptor_list.end();
         ++it) {
        std::string index_table_name = tera_adapter_.table_prefix_ + "#" + schema.table_name() + "#" + it->index_name;
        LOG(INFO) << "Create index table name " << index_table_name;
        tera::TableDescriptor index_table_desc(index_table_name);
        index_table_desc.SetRawKey(tera::kBinary);
        tera::LocalityGroupDescriptor* index_lg = index_table_desc.AddLocalityGroup("lg");
        index_lg->SetBlockSize(32 * 1024);
        index_lg->SetCompress(tera::kSnappyCompress);
        tera::ColumnFamilyDescriptor* index_cf = index_table_desc.AddColumnFamily("PrimaryKey", "lg");
        index_cf->SetTimeToLive(0);
        tera_adapter_.opt_.client_->CreateTable(index_table_desc, &error_code);
    }

    return Status::OK();
}

Status DatabaseImpl::OpenTable(const std::string& table_name, Table** table_ptr) {
    // read schema from schema table
    tera::ErrorCode error_code;
    std::string schema_value;
    TableDescription table_desc;
    tera_adapter_.opt_.schema_table_->Get(table_name, "", "", &schema_value, &error_code);
    LOG(INFO) << "OpenTable: get table schema, table name " << table_name <<
        ", error code " << tera::strerr(error_code);

    // assemble TableDescription
    BigQueryTableSchema schema;
    schema.ParseFromString(schema_value);
    DisassembleTableSchema(schema, &table_desc);

    if (table_map_.find(table_desc.table_name) != table_map_.end()) {
        *table_ptr = table_map_[table_name];
        return Status::OK();
    }

    // construct memory structure
    TableImpl::OpenTable(db_name_, tera_opt_, fs_opt_, table_desc, table_ptr);
    table_map_[table_name] = *table_ptr;
    return Status::OK();
}

int DatabaseImpl::AssembleTableSchema(const TableDescription& table_desc,
                                      BigQueryTableSchema* schema) {
    schema->set_table_name(table_desc.table_name);
    schema->set_primary_key_type(table_desc.primary_key_type);
    LOG(INFO) << "Assemble: table name " << schema->table_name();
    std::vector<IndexDescription>::const_iterator it;
    for (it = table_desc.index_descriptor_list.begin();
         it != table_desc.index_descriptor_list.end();
         ++it) {
        IndexSchema* index;
        index = schema->add_index_descriptor_list();
        index->set_index_name(it->index_name);
        index->set_index_key_type(it->index_key_type);
        LOG(INFO) << "Assemble: index table name " << index->index_name();
    }
    return 0;
}

int DatabaseImpl::DisassembleTableSchema(const BigQueryTableSchema& schema,
                                         TableDescription* table_desc) {
    table_desc->table_name = schema.table_name();
    table_desc->primary_key_type = (TYPE)schema.primary_key_type();
    LOG(INFO) << "Disassemble: table name" << table_desc->table_name;
    for (int32_t i = 0; i < schema.index_descriptor_list_size(); i++) {
        const IndexSchema& index_schema = schema.index_descriptor_list(i);
        IndexDescription index_desc;
        index_desc.index_name = index_schema.index_name();
        index_desc.index_key_type = (TYPE)index_schema.index_key_type();
        table_desc->index_descriptor_list.push_back(index_desc);
        LOG(INFO) << "Disassemble: index table name " << index_desc.index_name;
    }
    return 0;
}

} // namespace mdt
