#include <gflags/gflags.h>
#include <glog/logging.h>

#include "util/status.h"
#include "util/env.h"
#include "util/dfs.h"
#include "util/nfs.h"

DECLARE_string(env_nfs_mountpoint);
DECLARE_string(env_nfs_conf_path);
DECLARE_string(env_nfs_so_path);

namespace mdt {

static Status IOError(const std::string& context, int err_number) {
    return Status::IOError(context, strerror(err_number));
}

class DfsWritableFile : public WritableFile {
public:
    DfsWritableFile(Dfs* fs, const std::string& fname)
        :fs_(fs), filename_(fname), file_(NULL) {
        fs_->Delete(filename_);
        file_ = fs_->OpenFile(filename_, WRONLY);
        assert(file_);
    }

    virtual ~DfsWritableFile() {
        if (file_) {
            file_->CloseFile();
            delete file_;
        }
    }

    bool isValid() {
        return file_ != NULL;
    }

    Status Append(const Slice& data) {
        const char* src = data.data();
        size_t size = data.size();

        int32_t ret = file_->Write(src, size);
        if (ret != static_cast<int32_t>(size)) {
            return IOError(filename_, errno);
        }
        return Status::OK();
    }

    Status Flush() {return Status::OK();}

    Status Sync() {
        Status s;
        if (file_->Sync() == -1) {
            s = IOError(filename_, errno);
        }
        return s;
    }

    Status Close() {
        Status s;
        if (file_ != NULL && file_->CloseFile() != 0) {
            s = IOError(filename_, errno);
        }
        delete file_;
        file_ = NULL;
        return s;
    }

private:
    Dfs* fs_;
    std::string filename_;
    DfsFile* file_;
};

class EnvNfsImpl : public Env {
public:
    EnvNfsImpl(Dfs* dfs) {
        env_name_ = "NfsImpl";
        dfs_ = dfs;
    }

    virtual ~EnvNfsImpl() {}

    //static Env* Default();

    virtual Status NewSequentialFile(const std::string& fname,
                                     SequentialFile** result) {
        std::string s;
        return Status::OK();
    }

    virtual Status NewRandomAccessFile(const std::string& fname,
                                       RandomAccessFile** result) {
        return Status::OK();
    }

    virtual Status NewRandomAccessFile(const std::string& fname,
                                       uint64_t fsize,
                                       RandomAccessFile** result) {
        return Status::OK();
    }

    virtual Status NewWritableFile(const std::string& fname,
                                   WritableFile** result) {
        Status s;
        DfsWritableFile* f = new DfsWritableFile(dfs_, fname);
        if (f == NULL || !f->isValid()) {
            delete f;
            *result = NULL;
            return IOError(fname, errno);
        }
        *result = dynamic_cast<WritableFile*>(f);
        return Status::OK();
    }

    virtual bool FileExists(const std::string& fname) {
        return (0 == dfs_->Exists(fname));
    }

    virtual Status GetChildren(const std::string& dir,
            std::vector<std::string>* result,
            std::vector<int64_t>* ctime = NULL) {
        if (0 != dfs_->ListDirectory(dir, result, ctime)) {
            abort();
        }
        return Status::OK();
    }

    virtual Status DeleteFile(const std::string& fname) {
        if (dfs_->Delete(fname) == 0) {
            return Status::OK();
        }
        return IOError(fname, errno);
    }

    virtual Status CreateDir(const std::string& dirname) {
        if (dfs_->CreateDirectory(dirname) == 0) {
            return Status::OK();
        }
        return IOError(dirname, errno);
    }

    virtual Status DeleteDir(const std::string& dirname) {
        if (dfs_->DeleteDirectory(dirname) == 0) {
            return Status::OK();
        }
        return IOError(dirname, errno);
    }

    virtual Status CopyFile(const std::string& from,
                            const std::string& to) {
        if (from != to && dfs_->Copy(from, to) != 0) {
            return Status::IOError("DFS Copy", from);
        }
        return Status::OK();
    }

    virtual Status DeleteDirRecursive(const std::string& name) {
        return Status::OK();
    }

    virtual Status GetFileSize(const std::string& fname,
                               uint64_t* file_size) {return Status::OK();}

    virtual Status RenameFile(const std::string& src,
            const std::string& target) {return Status::OK();}

    virtual Status LockFile(const std::string& fname,
                            FileLock** lock) {return Status::OK();}
    virtual Status UnlockFile(FileLock* lock) {return Status::OK();}

    virtual Env* CacheEnv() {
        return NULL;
    }

private:
    EnvNfsImpl(const EnvNfsImpl&);
    void operator=(const EnvNfsImpl&);

private:
    std::string env_name_;
    Dfs* dfs_;
};

static pthread_once_t once = PTHREAD_ONCE_INIT;
static Env* nfs_env;

static void InitNfsEnv() {
    Nfs::Init(FLAGS_env_nfs_mountpoint, FLAGS_env_nfs_conf_path);
    Dfs* dfs = Nfs::GetInstance();
    nfs_env = new EnvNfsImpl(dfs);
}

Env* EnvNfs() {
  pthread_once(&once, InitNfsEnv);
  return nfs_env;
}

}
