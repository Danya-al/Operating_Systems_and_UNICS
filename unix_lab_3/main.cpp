#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <openssl/evp.h>
#include <sys/stat.h>

namespace fs = std::filesystem;
using namespace std;

string sha256(const fs::path& path) {
    ifstream file(path, ios::binary);
    if (!file)
        throw runtime_error("Не удалось открыть файл: " + path.string());

    unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
        ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw runtime_error("Ошибка инициализации SHA-256");

    array<char, 8192> buffer;
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<size_t>(file.gcount())) != 1)
            throw runtime_error("Ошибка вычисления SHA-256: " + path.string());
    }
    if (file.bad() || !file.eof())
        throw runtime_error("Ошибка чтения файла: " + path.string());

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length;
    if (EVP_DigestFinal_ex(ctx.get(), hash, &length) != 1)
        throw runtime_error("Ошибка получения SHA-256: " + path.string());

    return string(reinterpret_cast<const char*>(hash), length);
}

void replace_with_link(const fs::path& original, const fs::path& duplicate) {
    fs::path temp;
    for (size_t i = 0; ; ++i) {
        temp = duplicate.parent_path() / (".dedup-tmp-" + to_string(i));
        error_code ec;
        fs::create_hard_link(original, temp, ec);
        if (!ec)
            break;
        if (ec != errc::file_exists)
            throw fs::filesystem_error("Не удалось создать жёсткую ссылку", original, temp, ec);
    }

    try {
        fs::rename(temp, duplicate);
    } catch (...) {
        error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Использование: " << argv[0] << " <каталог>\n";
        return 1;
    }

    try {
        vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(argv[1])) {
            if (fs::is_regular_file(entry.symlink_status()))
                files.push_back(entry.path());
        }

        map<pair<dev_t, string>, fs::path> originals;
        size_t count = 0;
        for (const auto& path : files) {
            struct stat info{};
            if (::stat(path.c_str(), &info) != 0)
                throw runtime_error("Не удалось получить информацию о файле: " + path.string());

            auto [it, inserted] = originals.try_emplace(make_pair(info.st_dev, sha256(path)), path);
            if (inserted || fs::equivalent(it->second, path))
                continue;

            replace_with_link(it->second, path);
            ++count;
        }

        cout << "Создано жёстких ссылок: " << count << '\n';
    } catch (const exception& error) {
        cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}