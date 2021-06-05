#include <set>
#include <fstream>
#include <utime.h>
#include <dirent.h>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include "tar.hpp"

#define ERROR(fmt, ...) fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return -1;
#define RC_ERROR(fmt, ...) const int rc = errno; ERROR(fmt, ##__VA_ARGS__); return -1;
#define EXIST_ERROR(fmt, ...) const int rc = errno; if (rc != EEXIST) { ERROR(fmt, ##__VA_ARGS__); return -1; }

void write_tar(std::ostream &out_f, const Tar &tar) {
    out_f << tar.relative_path << '\0';
    out_f << tar.link << '\0';
    out_f << static_cast<char>(tar.type);
    out_f.write(reinterpret_cast<const char *>(&tar.mtime), sizeof tar.mtime);
    out_f.write(reinterpret_cast<const char *>(&tar.atime), sizeof tar.atime);
    out_f.write(reinterpret_cast<const char *>(&tar.st_size), sizeof tar.st_size);
    out_f.write(reinterpret_cast<const char *>(&tar.st_ino), sizeof tar.st_ino);
    out_f.write(reinterpret_cast<const char *>(&tar.st_dev), sizeof tar.st_dev);
    out_f.write(reinterpret_cast<const char *>(&tar.st_gid), sizeof tar.st_gid);
    out_f.write(reinterpret_cast<const char *>(&tar.st_uid), sizeof tar.st_uid);
    out_f.write(reinterpret_cast<const char *>(&tar.st_mode), sizeof tar.st_mode);
}

bool try_read_tar(std::istream &in_f, Tar &tar) {
    std::getline(in_f, tar.relative_path, '\0');
    if (in_f.eof()) {
        return false;
    }
    std::getline(in_f, tar.link, '\0');
    char t;
    in_f >> t;
    tar.type = Type{t};
    in_f.read(reinterpret_cast<char *>(&tar.mtime), sizeof tar.mtime);
    in_f.read(reinterpret_cast<char *>(&tar.atime), sizeof tar.atime);
    in_f.read(reinterpret_cast<char *>(&tar.st_size), sizeof tar.st_size);
    in_f.read(reinterpret_cast<char *>(&tar.st_ino), sizeof tar.st_ino);
    in_f.read(reinterpret_cast<char *>(&tar.st_dev), sizeof tar.st_dev);
    in_f.read(reinterpret_cast<char *>(&tar.st_gid), sizeof tar.st_gid);
    in_f.read(reinterpret_cast<char *>(&tar.st_uid), sizeof tar.st_uid);
    in_f.read(reinterpret_cast<char *>(&tar.st_mode), sizeof tar.st_mode);
    return true;
}

int format_tar_data(struct Tar &entry, const std::string &absolute_path, const std::string &relative_path) {
    struct stat st{};
    if (lstat(absolute_path.c_str(), &st)) {
        RC_ERROR("Cannot stat %s: %s", absolute_path.c_str(), strerror(rc));
    }

    // start putting in new data (all fields are nullptr terminated ASCII strings)
    entry.relative_path = relative_path;
    entry.st_dev = st.st_dev;
    entry.st_ino = st.st_ino;
    entry.st_mode = st.st_mode;
    entry.st_uid = st.st_uid;
    entry.st_gid = st.st_gid;
    entry.st_size = st.st_size;
    entry.atime = st.st_atime;
    entry.mtime = st.st_mtime;

    char* tmp;
    // figure out filename type and fill in type-specific fields
    switch (st.st_mode & S_IFMT) {
        case S_IFREG:
            entry.type = Type::REGULAR;
            break;
        case S_IFLNK:
            entry.type = Type::SYMLINK;
            tmp = new char[entry.st_size];
            // get link name
            if (readlink(absolute_path.c_str(), tmp, entry.st_size) < 0) {
                RC_ERROR("Could not read link %s: %s", absolute_path.c_str(), strerror(rc));
            }
            entry.link = std::string{tmp};
            delete[] tmp;
            entry.st_size = 0;
            break;
        case S_IFDIR:
            entry.st_size = 0;
            entry.type = Type::DIRECTORY;
            break;
        case S_IFIFO:
            entry.type = Type::FIFO;
            break;
        case S_IFSOCK:
            entry.type = Type::SOCK;
            break;
        default:
            entry.type = Type::UNKNOWN;
            ERROR("Error: Unknown filetype");
    }
    return 0;
}

int write_entries(std::ostream &out_f, std::set<std::pair<dev_t, ino_t>> &all_files, const std::string &path,
                  const std::string &relative_path, const std::string &archive_file) {
    // add new data
    struct Tar tar{};  // current entry
    DIR *d;
    struct dirent *dir;
    d = opendir(path.c_str());
    if (!d) {
        ERROR("Не удалось открыть директорию %s", path.c_str());
    }
    while ((dir = readdir(d)) != nullptr) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }
        std::string file_name = dir->d_name;
        std::string absolute_path = path + file_name;
        if (archive_file == absolute_path) {
            continue;
        }
        if (format_tar_data(tar, absolute_path, relative_path + file_name) < 0) {
            ERROR("Failed to stat %s", absolute_path.c_str());
        }
        if (tar.type == Type::SOCK) {
            continue;
        }
        if (tar.type == Type::DIRECTORY) {
            std::string child = absolute_path + '/';
            write_tar(out_f, tar);

            if (write_entries(out_f, all_files, child, relative_path + file_name + '/', archive_file) < 0) {
                ERROR("Recurse error");
            }
            continue;
        }
        if (tar.type == Type::REGULAR) {
            auto e = all_files.find({tar.st_dev, tar.st_ino});
            if (e != all_files.end()) {
                tar.type = Type::HARDLINK;
                tar.link = tar.relative_path;
                tar.st_size = 0;
            }
        }
        write_tar(out_f, tar);

        if (tar.type == Type::REGULAR) {
            std::ifstream sf{absolute_path};

            if (sf) {
                for (off_t i = tar.st_size; i > 0; i--) {
                    out_f.put((char) sf.get());
                }
            } else {
                ERROR("Could not open %s", absolute_path.c_str());
            }
        }
    }
    closedir(d);
    return 0;
}

int extract_entries(std::istream &in_f, const std::string &working_dir) {
    Tar tar;
    while (try_read_tar(in_f, tar)) {
        std::string absolute_path = working_dir + tar.relative_path;
        if (tar.type == Type::REGULAR) {
            std::ofstream sf{absolute_path};

            if (sf) {
                for (off_t i = tar.st_size; i > 0; i--) {
                    sf.put((char) in_f.get());
                }
            } else {
                ERROR("Could not open %s", absolute_path.c_str());
            }
        } else if (tar.type == Type::HARDLINK) {
            if (link(tar.link.c_str(), absolute_path.c_str()) < 0) {
                EXIST_ERROR("Unable to create hardlink %s: %s", absolute_path.c_str(), strerror(rc));
            }
        } else if (tar.type == Type::SYMLINK) {
            if (symlink(tar.link.c_str(), absolute_path.c_str()) < 0) {
                EXIST_ERROR("Unable to make symlink %s: %s", absolute_path.c_str(), strerror(rc));
            }
        } else if (tar.type == Type::DIRECTORY) {
            if (mkdir(absolute_path.c_str(), tar.st_mode) < 0) {
                EXIST_ERROR("Unable to create directory %s: %s", absolute_path.c_str(), strerror(rc));
            }
        } else if (tar.type == Type::FIFO) {
            if (mkfifo(absolute_path.c_str(), tar.st_mode) < 0) {
                EXIST_ERROR("Unable to make pipe %s: %s", absolute_path.c_str(), strerror(rc));
            }
        }
        lchown(absolute_path.c_str(), tar.st_uid, tar.st_gid);
        lchmod(absolute_path.c_str(), tar.st_mode);

        struct utimbuf tmp{tar.atime, tar.mtime};
        utime(absolute_path.c_str(), &tmp);
    }
    return 0;
}

int tar_write(std::ostream &out_f, const std::string &path, const std::string &archive_file) {
    // write entries first
    std::set<std::pair<dev_t, ino_t>> all_files{};
    std::string tmp;
    if (write_entries(out_f, all_files, path, tmp, archive_file) < 0) {
        ERROR("Failed to write entries");
    }
    return 0;
}

int tar_extract(std::istream &in_f, const std::string &working_dir) {
    if (mkdir(working_dir.c_str(), 0777) < 0) {
        EXIST_ERROR("Unable to create directory %s: %s", working_dir.c_str(), strerror(rc));
    }
    return extract_entries(in_f, working_dir);
}
