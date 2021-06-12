#include <string>
#include <cstring>
#include <iostream>
#include <fstream>

#include "tar.hpp"


int main(int argc, char *argv[]) {
    bool create = false, extract = false;
    std::string working_dir;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--create") == 0) {
            if (create) {
                std::cerr << "Флаг -c может быть указан только один раз\n";
                return 1;
            }
            create = true;
            continue;
        }
        if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--extract") == 0) {
            if (extract) {
                std::cerr << "Флаг -e может быть указан только один раз\n";
                return 1;
            }
            extract = true;
            continue;
        }
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--directory") == 0) {
            if (!working_dir.empty()) {
                std::cerr << "Флаг -d может быть указан только один раз\n";
                return 1;
            }
            if (i >= argc - 1) {
                std::cerr << "После флага -d должна идти рабочая директория\n";
                return 1;
            }
            i++;
            working_dir = std::string(argv[i]);
        }
    }
    if (create && extract) {
        std::cerr << "Нельзя использовать одновременно флаг -c и -x\n";
        return 1;
    }
    if (!create && !extract) {
        std::cerr << "Надо выбрать один из флагов -c или -x\n";
        return 1;
    }
    if (working_dir.empty()) {
        working_dir = "./";
    }

    if (working_dir[working_dir.size() - 1] != '/') {
        working_dir += '/';
    }
    if (create) {
        std::ofstream out_f(argv[argc - 1], std::ios::binary);

        if (tar_write(out_f, working_dir, argv[argc - 1], argv[0]) < 0) {
            return -1;
        }
    } else {
        std::ifstream in_f(argv[argc - 1], std::ios::binary);

        if (tar_extract(in_f, working_dir) < 0) {
            return -1;
        }
    }

    return 0;
}
