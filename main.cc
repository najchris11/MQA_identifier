/**
 * @file        main.cc
 * @author      Stavros Avramidis (@purpl3F0x)
 * @date        16/12/2019
 * @copyright   2019 Stavros Avramidis under Apache 2.0 License
 */

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <fstream>
#include <map>
#include <atomic>

#include "mqa_identifier.h"

namespace fs = std::filesystem;

// Global mutex for console output
std::mutex console_mutex;
std::mutex log_mutex;

// Scan statistics
std::atomic<size_t> scanned_count{0};
std::atomic<size_t> mqa_count{0};

// Logging
bool verbose_logging = false;
std::string log_file_path = "mqa_identifier.log";
std::map<std::string, std::vector<std::string>> scan_errors;

void log_skip(const std::string& path, const std::string& reason) {
    std::lock_guard<std::mutex> lock(log_mutex);
    scan_errors[reason].push_back(path);
}

auto getSampleRateString(const uint32_t fs) {
    std::stringstream ss;
    if (fs <= 768000)
        ss << fs / 1000. << "K";
    else if (fs % 44100 == 0)
        ss << "DSD" << fs / 44100;
    else
        ss << "DSD" << fs / 48000 << "x48";

    return ss.str();
}

/**
 * @short Recursively scan a directory for .flac files
 * @param curDir directory to scan
 * @param files vector to add the file paths
 */
void recursiveScan(const fs::path &curDir, std::vector<std::string> &files) {
    try {
        if (!fs::exists(curDir)) {
             log_skip(curDir.string(), "Path does not exist");
             return;
        }

        if (fs::is_regular_file(curDir)) {
            if (curDir.extension() == ".flac")
                files.push_back(curDir.string());
            return;
        }
        
        if (fs::is_directory(curDir)) {
            for (const auto &entry : fs::directory_iterator(curDir, fs::directory_options::skip_permission_denied)) {
                try {
                    if (fs::is_regular_file(entry) && (entry.path().extension() == ".flac"))
                        files.push_back(entry.path().string());
                    else if (fs::is_directory(entry))
                        recursiveScan(entry.path(), files);
                } catch (const fs::filesystem_error& e) {
                     log_skip(entry.path().string(), "Filesystem Error: " + std::string(e.what()));
                } catch (const std::exception& e) {
                     log_skip(entry.path().string(), "Error during scan: " + std::string(e.what()));
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
         log_skip(curDir.string(), "Access Denied / Filesystem Error: " + std::string(e.what()));
    } catch (const std::exception& e) {
         log_skip(curDir.string(), "Error accessing path: " + std::string(e.what()));
    }
}

void processFile(const std::string& file, int index) {
    auto id = MQA_identifier(file);
    bool detected = id.detect();
    std::string filename;
    try {
        filename = fs::path(file).filename().string();
    } catch (...) {
        filename = file;
    }

    if (detected) {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << std::setw(3) << index << "\t";
        if (id.originalSampleRate())
            std::cout << "MQA " << getSampleRateString(id.originalSampleRate()) << "\t";
        else
            std::cout << "MQA\t\t";
        std::cout << filename << "\n";
        mqa_count++;
    } else {
        std::string err = id.getErrorMessage();
        if (!err.empty()) {
            log_skip(file, err);
        } else {
            // Only print NOT MQA if no error occurred
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << std::setw(3) << index << "\tNOT MQA \t" << filename << "\n";
        }
    }
    scanned_count++;
}

int main(int argc, char *argv[]) {
    // Parse arguments
    std::vector<std::string> input_paths;
    
    if (argc == 1) {
        std::cout << "HINT: To use the tool provide files and/or directories as program arguments.\n";
        std::cout << "      Use -v to enable verbose logging to mqa_identifier.log\n\n";
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v") {
            verbose_logging = true;
        } else if (arg.rfind("--log=", 0) == 0) {
            // Optional support for custom log path if user wants, though -v was requested
            // Keeping simplistic -v per newest request, but old plan mentioned --log. 
            // We'll stick to -v -> mqa_identifier.log as per latest comment, 
            // but just ignore this or treat as path? Treating as path to be safe.
             input_paths.push_back(arg);
        } else {
            input_paths.push_back(arg);
        }
    }

    std::vector<std::string> files;
    for (const auto& path : input_paths) {
        recursiveScan(fs::path(path), files);
    }

    // Flush error buffer
    std::cerr << std::flush;

    // Let's do some printing
    std::cout << "**************************************************\n";
    std::cout << "***********  MQA flac identifier tool  ***********\n";
    std::cout << "********  Stavros Avramidis (@purpl3F0x)  ********\n";
    std::cout << "** https://github.com/purpl3F0x/MQA_identifier  **\n";
    std::cout << "**************************************************\n";

    std::cout << "Found " << files.size() << " file for scanning...\n\n";

    std::cout << "  #\tEncoding\tName\n";

    // Launch parallel tasks
    std::vector<std::future<void>> futures;
    int count = 0;
    for (const auto &file : files) {
        futures.push_back(std::async(std::launch::async, processFile, file, ++count));
    }

    // Wait for all tasks
    for (auto &f : futures) {
        f.get();
    }

    std::cout << "\n**************************************************\n";
    std::cout << "Scanned " << scanned_count << " files\n"; // Using atomic count which tracks actual processed
    std::cout << "Found " << mqa_count << " MQA files\n";

    if (verbose_logging && !scan_errors.empty()) {
        std::ofstream log(log_file_path);
        if (log.is_open()) {
            log << "MQA Identifier Scan Log\n";
            log << "=======================\n\n";
            for (const auto& [reason, paths] : scan_errors) {
                log << "Reason: " << reason << "\n";
                for (const auto& p : paths) {
                    log << " - " << p << "\n";
                }
                log << "\n";
            }
            std::cout << "Log written to " << log_file_path << "\n";
        } else {
            std::cerr << "Failed to open log file for writing.\n";
        }
    }
}
