#ifndef  __SRC/SDK/DB_IMPL_H_
#define  __SRC/SDK/DB_IMPL_H_

namespace mdt {
// table in memory control structure
struct TeraAdapter {
    tera::Client* client_;
    std::map<std::string, tera::Table*> tera_table_map_; // <table_name, table desc>
};

struct FilesystemAdapter {
    std::string root_path_;
    Env* env_;
};

struct Options {
    std::string tera_flag_file_path_; // tera.flag's path
    Env* env_;
};

class TableImpl : public Table {
public:
    int Put(const StoreRequest* request, StoreResponse* response, StoreCallback callback);
    int Put(const StoreRequest* request, StoreResponse* response);

private:
    std::string db_name_;
    TableDescription table_desc_;
    TeraAdapter tera_;
    FilesystemAdapter fs_;
};

struct TeraOptions {
    std::string root_path_;
    std::string tera_flag_;
    tera::Client* client_;
    tera::Table* schema_table_;
};

class DatabaseImpl : public Database {
public:
    // create fs namespace
    static int CreateDB(const Options& options, std::string db_name, Database** db);
    // if db not exit, create it
    int OpenTable(const CreateRequest& request, CreateResponse* response, Table** table_ptr);

private:
    std::string db_name_;
    const Options options_;
    TeraOptions tera_opt_;
    std::map<std::string, TableImpl*> table_map_; // <table_name, table ptr>
};

}
#endif  //__SRC/SDK/DB_IMPL_H_