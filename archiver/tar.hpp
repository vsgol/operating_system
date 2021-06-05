#ifndef ARCHIVER_TAR_HPP
#define ARCHIVER_TAR_HPP

enum class Type : char {
    REGULAR = '0', HARDLINK = '1', SYMLINK = '2', DIRECTORY = '3', FIFO = '4', SOCK = '5', UNKNOWN = '-'
};

struct Tar {
    std::string relative_path{};  // relative file name
    std::string link{};
    dev_t st_dev{};               // ID of device containing file
    ino_t st_ino{};               // inode number
    mode_t st_mode{};             // protection
    uid_t st_uid{};               // user id (octal)
    gid_t st_gid{};               // group id (octal)
    off_t st_size{};              // size (octal)
    time_t atime{};               // time of last access
    time_t mtime{};               // time of last modification
    Type type = Type::UNKNOWN;    // type
};

int tar_extract(std::istream &in_f, const std::string &working_dir);

int tar_write(std::ostream &out_f, const std::string &path, const std::string &archive_file);

#endif //ARCHIVER_TAR_HPP
