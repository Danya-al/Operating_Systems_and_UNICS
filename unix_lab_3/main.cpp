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

struct Entry {
    fs::path path;
    struct stat info;
};

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
        if (EVP_DigestUpdate(ctx.get(), buffer.data(),
                             static_cast<size_t>(file.gcount())) != 1)
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
            throw fs::filesystem_error(
                "Не удалось создать жёсткую ссылку", original, temp, ec);
    }

    try {
        fs::rename(temp, duplicate);
    } catch (...) {
        error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}

bool unchanged(const Entry& entry) {
    struct stat now{};
    if (::stat(entry.path.c_str(), &now) != 0)
        return false;

    return now.st_dev == entry.info.st_dev &&
           now.st_ino == entry.info.st_ino &&
           now.st_size == entry.info.st_size &&
           now.st_mtim.tv_sec == entry.info.st_mtim.tv_sec &&
           now.st_mtim.tv_nsec == entry.info.st_mtim.tv_nsec;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Использование: " << argv[0] << " <каталог>\n";
        return 1;
    }

    size_t errors = 0;
    auto report = [&errors](const string& message) {
        cerr << "Предупреждение: " << message << '\n';
        ++errors;
    };

    try {
        vector<fs::path> files;

        fs::directory_iterator{argv[1]};

        error_code ec;
        fs::recursive_directory_iterator it(
            argv[1], fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;

        if (ec)
            throw runtime_error("Не удалось открыть каталог: " + ec.message());

        while (it != end) {
            error_code status_ec;
            auto status = it->symlink_status(status_ec);

            if (status_ec)
                report("Не удалось получить статус " + it->path().string() +
                       ": " + status_ec.message());
            else if (fs::is_regular_file(status))
                files.push_back(it->path());

            it.increment(ec);
            if (ec) {
                report("Ошибка при обходе каталога: " + ec.message());
                break;
            }
        }

        vector<Entry> entries;
        for (const auto& path : files) {
            Entry entry{path, {}};

            if (::stat(path.c_str(), &entry.info) != 0) {
                report("Не удалось получить информацию о файле: " + path.string());
                continue;
            }

            entries.push_back(entry);
        }

        map<pair<dev_t, string>, Entry> originals;
        size_t count = 0;

        for (const auto& entry : entries) {
            try {
                string hash = sha256(entry.path);

                if (!unchanged(entry))
                    throw runtime_error(
                        "Файл изменился во время хэширования: " + entry.path.string());

                auto [it, inserted] = originals.try_emplace(
                    make_pair(entry.info.st_dev, hash), entry);

                if (inserted || fs::equivalent(it->second.path, entry.path))
                    continue;

                if (!unchanged(it->second) || !unchanged(entry))
                    throw runtime_error(
                        "Файл изменился перед заменой: " + entry.path.string());

                replace_with_link(it->second.path, entry.path);
                ++count;
            } catch (const exception& error) {
                report(error.what());
            }
        }

        cout << "Создано жёстких ссылок: " << count << '\n';
        cout << "Ошибок: " << errors << '\n';
    } catch (const exception& error) {
        cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }

    return errors == 0 ? 0 : 2;
}