/**
 * @file        main.cc
 * @author      Stavros Avramidis (@purpl3F0x)
 * @author      naj-dev (@najchris11)
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
#include <thread>
#include <deque>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

#include <FLAC++/metadata.h>
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
bool dry_run = false;
std::string log_file_path = "mqa_identifier.log";
std::map<std::string, std::vector<std::string>> scan_errors;
std::vector<std::string> detailed_log;

// Helper to safely print paths even if they contain weird chars
std::string safe_path_string(const fs::path& p) {
    try {
        return p.string();
    } catch (...) {
        return "<invalid path encoding>";
    }
}

void log_message(const std::string& msg) {
    if (!verbose_logging) return;
    std::lock_guard<std::mutex> lock(log_mutex);
    detailed_log.push_back(msg);
}

void log_skip(const std::string& path, const std::string& reason) {
    std::lock_guard<std::mutex> lock(log_mutex);
    scan_errors[reason].push_back(path);
    if (verbose_logging) {
        detailed_log.push_back("[ERROR] " + path + ": " + reason);
    }
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

void tagFile(const fs::path& file, uint32_t original_rate) {
    std::string fileStr = safe_path_string(file);
    if (dry_run) {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "DRY RUN: Would write tags to " << file.filename().string() << "\n";
        log_message("[DRY RUN] Would tag " + fileStr);
        return;
    }

    try {
        FLAC::Metadata::Chain chain;
        // FLAC++ metadata interface uses const char* filename.
        // On Windows this expects ANSI. For Unicode we might need to use the simple interface 
        // OR rely on the fact that modern Windows might handle UTF-8 if Manifest allows. 
        // But standard FLAC++ metadata chain read() doesn't have a wchar_t overload or FILE* overload.
        // This is a known limitation of FLAC++ metadata wrapper.
        // We will try safe_path_string(file) which is usually UTF-8. 
        // If this fails on Windows for unicode paths, we might need a more complex workaround using the C API directly with filenames encoded or short paths.
        // For now, we try standard read().
        if (!chain.read(fileStr.c_str())) {
             log_skip(fileStr, "Failed to read metadata chain");
             return;
        }

        FLAC::Metadata::Iterator iterator;
        iterator.init(chain);

        FLAC::Metadata::VorbisComment* vcBlock = nullptr;
        
        // Find existing Vorbis Comment block
        do {
            if (iterator.get_block()->get_type() == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
                vcBlock = dynamic_cast<FLAC::Metadata::VorbisComment*>(iterator.get_block());
                break;
            }
        } while (iterator.next());

        // Create if missing
        if (!vcBlock) {
            vcBlock = new FLAC::Metadata::VorbisComment();
            // Move to end to append
            while (iterator.next()) {} 
            if (!iterator.insert_block_after(vcBlock)) {
                 delete vcBlock;
                 log_skip(fileStr, "Failed to create new VorbisComment block");
                 return;
            }
        }

        bool modified = false;

        // Check/Add MQAENCODER
        if (vcBlock->find_entry_from(0, "MQAENCODER") == -1) {
             vcBlock->append_comment(FLAC::Metadata::VorbisComment::Entry("MQAENCODER", "MQAEncode v1.1, 2.3.3+800 (a505918), F8EC1703-7616-45E5-B81E-D60821434062, Dec 01 2017 22:19:30"));
             modified = true;
        }

        // Check/Add ORIGINALSAMPLERATE
        if (original_rate > 0 && vcBlock->find_entry_from(0, "ORIGINALSAMPLERATE") == -1) {
            vcBlock->append_comment(FLAC::Metadata::VorbisComment::Entry("ORIGINALSAMPLERATE", std::to_string(original_rate).c_str()));
             modified = true;
        }

        if (modified) {
            if (!chain.write()) {
                 log_skip(fileStr, "Failed to write metadata changes");
            } else {
                 log_message("[TAGGED] " + fileStr);
            }
        }

    } catch (const std::exception& e) {
        log_skip(fileStr, "Tagging error: " + std::string(e.what()));
    } catch (...) {
        log_skip(fileStr, "Unknown tagging error");
    }
}

/**
 * @short Recursively scan a directory for .flac files
 * @param curDir directory to scan
 * @param files vector to add the file paths
 */
void recursiveScan(const fs::path &curDir, std::vector<fs::path> &files) {
    try {
        if (!fs::exists(curDir)) {
             log_skip(safe_path_string(curDir), "Path does not exist");
             return;
        }

        if (fs::is_regular_file(curDir)) {
            if (curDir.extension() == ".flac")
                files.push_back(curDir);
            return;
        }
        
        if (fs::is_directory(curDir)) {
            // Live progress logging
            static int counter = 0;
            if (++counter % 50 == 0) {
                 std::cout << "\rScanning... Found " << files.size() << " files so far. Current: " << safe_path_string(curDir.filename()) << "        " << std::flush;
            }

            for (const auto &entry : fs::directory_iterator(curDir, fs::directory_options::skip_permission_denied)) {
                try {
                    if (fs::is_regular_file(entry) && (entry.path().extension() == ".flac"))
                        files.push_back(entry.path());
                    else if (fs::is_directory(entry))
                        recursiveScan(entry.path(), files);
                } catch (const fs::filesystem_error& e) {
                     log_skip(safe_path_string(entry.path()), "Filesystem Error: " + std::string(e.what()));
                } catch (const std::exception& e) {
                     log_skip(safe_path_string(entry.path()), "Error during scan: " + std::string(e.what()));
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
         log_skip(safe_path_string(curDir), "Access Denied / Filesystem Error: " + std::string(e.what()));
    } catch (const std::exception& e) {
         log_skip(safe_path_string(curDir), "Error accessing path. " + std::string(e.what()));
    }
}

void processFile(const fs::path& file, int index) {
    auto id = MQA_identifier(file);
    bool detected = id.detect();
    std::string filename;
    try {
        filename = safe_path_string(file.filename());
    } catch (...) {
        filename = "Unknown_File";
    }

    if (detected) {
        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << std::setw(3) << index << "\t";
            if (id.originalSampleRate()) {
                std::cout << "MQA " << (id.isMQAStudio() ? "Studio " : "") << getSampleRateString(id.originalSampleRate()) << "\t";
            }
            else {
                std::cout << "MQA\t\t";
            }
            std::cout << filename << "\n";
        }
        
        // Write tags (outside the lock)
        if (id.originalSampleRate()) {
             tagFile(file, id.originalSampleRate());
        } else {
             tagFile(file, 0);
        }

        mqa_count++;
        std::string extra_info = id.isMQAStudio() ? "Studio " : "";
        extra_info += getSampleRateString(id.originalSampleRate());
        log_message("[MQA] " + safe_path_string(file) + " (" + extra_info + ")");
    } else {
        std::string err = id.getErrorMessage();
        if (!err.empty()) {
            log_skip(safe_path_string(file), err);
        } else {
            // Only print NOT MQA if no error occurred
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cout << std::setw(3) << index << "\tNOT MQA \t" << filename << "\n";
            }
            log_message("[NOT MQA] " + safe_path_string(file));
        }
    }
    scanned_count++;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Set console code page to UTF-8 to handle special characters in paths
    SetConsoleOutputCP(65001);
#endif

    // Parse arguments
    std::vector<std::string> input_paths;
    
    if (argc == 1) {
        std::cout << "HINT: To use the tool provide files and/or directories as program arguments.\n";
        std::cout << "      Use -v to enable verbose logging to mqa_identifier.log\n";
        std::cout << "      Use --dry-run to scan without modifying files.\n\n";
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v") {
            verbose_logging = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg.rfind("--log=", 0) == 0) {
             input_paths.push_back(arg);
        } else {
            input_paths.push_back(arg);
        }
    }

    std::cout << "Scanning directories...\n";
    std::vector<fs::path> files;
    for (const auto& path : input_paths) {
        recursiveScan(fs::path(path), files);
    }
    
    // Clear the progress line
    std::cout << "\r                                                                                \r";

    // Flush error buffer
    std::cerr << std::flush;

    // Let's do some printing
    std::cout << "**************************************************\n";
    std::cout << "***********  MQA flac identifier tool  ***********\n";
    std::cout << "********  Stavros Avramidis (@purpl3F0x)  ********\n";
    std::cout << "************  naj-dev (@najchris11)   ************\n";
    std::cout << "** https://github.com/purpl3F0x/MQA_identifier  **\n";
    std::cout << "**************************************************\n";

    std::cout << "Found " << files.size() << " file for scanning. Processing...\n\n";

    std::cout << "  #\tEncoding\tName\n";

    // Determine max threads (bounded concurrency)
    unsigned int max_threads = std::thread::hardware_concurrency();
    if (max_threads == 0) max_threads = 4; // Fallback
    // Cap it reasonable to avoid too much contention even on high-core systems if IO bound
    // But for this tool, IO is the bottleneck. 8-16 is usually good.
    if (max_threads > 16) max_threads = 16;
    
    std::vector<std::future<void>> futures;
    int count = 0;
    
    // Bounded execution loop
    for (const auto &file : files) {
        // If we filled our pool, wait for at least one to finish
        // Simple strategy: cleanup finished tasks before adding new ones
        // If still full, wait for the oldest one.
        
        // 1. Clean up finished tasks
        for (auto it = futures.begin(); it != futures.end(); ) {
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                it->get(); // Propagate exceptions if any (though processFile catches them)
                it = futures.erase(it);
            } else {
                ++it;
            }
        }
        
        // 2. If full, wait for one
        if (futures.size() >= max_threads) {
             // Wait for the first one (FIFO ish) or any. 
             // Simplest is wait for front.
             futures.front().wait();
             futures.front().get();
             futures.erase(futures.begin());
        }

        // 3. Launch new task
        futures.push_back(std::async(std::launch::async, processFile, file, ++count));
    }

    // Wait for remaining
    for (auto &f : futures) {
        f.wait();
        f.get();
    }

    std::cout << "\n**************************************************\n";
    std::cout << "Scanned " << scanned_count << " files\n"; 
    std::cout << "Found " << mqa_count << " MQA files\n";

    if (verbose_logging) {
        try {
            fs::path exe_path = fs::absolute(fs::path(argv[0]));
            if (!exe_path.parent_path().empty()) {
                log_file_path = (exe_path.parent_path() / "mqa_identifier.log").string();
            }
        } catch(...) {
            // Fallback to CWD
        }

        std::ofstream log(log_file_path);
        if (log.is_open()) {
            log << "MQA Identifier Scan Log\n";
            log << "=======================\n";
            log << "Detailed Event Log:\n";
            for(const auto& line : detailed_log) {
                log << line << "\n";
            }

            if (!scan_errors.empty()) {
                log << "\nSummary of Errors:\n";
                for (const auto& [reason, paths] : scan_errors) {
                    log << "Reason: " << reason << "\n";
                    for (const auto& p : paths) {
                        log << " - " << p << "\n";
                    }
                }
            }
            std::cout << "Log written to " << log_file_path << "\n";
        } else {
            std::cerr << "Failed to open log file for writing at " << log_file_path << "\n";
        }
    }
}

