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
#include <eh.h>

// Structured exception handler for Windows crashes
void translate_se_exception(unsigned int code, EXCEPTION_POINTERS* ep) {
    std::string error_msg = "Access Violation (possible memory corruption)";
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        error_msg = "Access Violation - attempted to read/write protected memory";
    } else if (code == EXCEPTION_STACK_OVERFLOW) {
        error_msg = "Stack Overflow";
    } else if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
        error_msg = "Illegal Instruction";
    }
    throw std::runtime_error(error_msg);
}
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
    std::string filename_str;
    MQA_identifier* id_ptr = nullptr;  // Initialize pointer early for safety
    
    try {
        filename_str = safe_path_string(file);
    } catch (...) {
        filename_str = "<unknown>";
    }
    
    try {
        std::cerr << "[FUNC] processFile starting for index " << index << "\n"; std::cerr.flush();
        
        // Validate FLAC file header before processing
        std::cerr << "[FUNC] Validating FLAC file header\n"; std::cerr.flush();
        try {
            std::ifstream flac_check(file, std::ios::binary);
            if (!flac_check.is_open()) {
                throw std::runtime_error("Cannot open file");
            }
            
            // Read FLAC header (4 bytes: "fLaC")
            char flac_header[4];
            flac_check.read(flac_header, 4);
            if (!flac_check || flac_check.gcount() != 4) {
                throw std::runtime_error("File too small or unreadable");
            }
            
            if (!(flac_header[0] == 'f' && flac_header[1] == 'L' && 
                  flac_header[2] == 'a' && flac_header[3] == 'C')) {
                std::cerr << "[FUNC] Not a valid FLAC file (bad header)\n"; std::cerr.flush();
                log_skip(filename_str, "Not a valid FLAC file");
                scanned_count++;
                return;
            }
            flac_check.close();
        } catch (const std::exception& e) {
            std::cerr << "[FUNC] Error validating FLAC header: " << e.what() << "\n"; std::cerr.flush();
            log_skip(filename_str, std::string("Invalid FLAC: ") + e.what());
            scanned_count++;
            return;
        }
        
        std::cerr << "[FUNC] FLAC header valid, creating MQA_identifier object\n"; std::cerr.flush();
        
        // Create the MQA_identifier with extra safety
        MQA_identifier* id_ptr = nullptr;
        try {
            id_ptr = new MQA_identifier(file);
        } catch (const std::exception& e) {
            std::cerr << "[FUNC] Exception creating MQA_identifier: " << e.what() << "\n"; std::cerr.flush();
            log_skip(filename_str, std::string("Failed to create decoder: ") + e.what());
            scanned_count++;
            if (id_ptr) delete id_ptr;
            return;
        } catch (...) {
            std::cerr << "[FUNC] Unknown exception creating MQA_identifier\n"; std::cerr.flush();
            log_skip(filename_str, "Failed to create decoder");
            scanned_count++;
            if (id_ptr) delete id_ptr;
            return;
        }
        
        std::cerr << "[FUNC] Calling detect() - this may crash for corrupted files\n"; std::cerr.flush();
        std::cerr.flush();
        
        // Add a safety wrapper around detect to catch crashes
        bool detected = false;
        try {
            detected = id_ptr->detect();
            std::cerr << "[FUNC] detect() completed successfully, result: " << (detected ? "MQA" : "NOT MQA") << "\n"; std::cerr.flush();
        } catch (const std::exception& e) {
            std::cerr << "[FUNC] Exception in detect(): " << e.what() << "\n"; std::cerr.flush();
            delete id_ptr;
            log_skip(filename_str, std::string("detect() failed: ") + e.what());
            scanned_count++;
            return;
        } catch (...) {
            std::cerr << "[FUNC] Unknown exception/crash in detect() - file may be corrupted\n"; std::cerr.flush();
            delete id_ptr;
            log_skip(filename_str, "detect() crashed - possible file corruption");
            scanned_count++;
            return;
        }
        
        MQA_identifier& id = *id_ptr;
        
        std::cerr << "[FUNC] Getting filename\n"; std::cerr.flush();
        std::string filename;
        try {
            filename = safe_path_string(file.filename());
        } catch (...) {
            filename = "Unknown_File";
        }

        std::cerr << "[FUNC] Checking if detected\n"; std::cerr.flush();
        if (detected) {
            std::cerr << "[FUNC] MQA detected, printing output\n"; std::cerr.flush();
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
                std::cout.flush();
            }
            
            std::cerr << "[FUNC] Tagging file\n"; std::cerr.flush();
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
            std::cerr << "[FUNC] Not MQA, checking for errors\n"; std::cerr.flush();
            std::string err = id.getErrorMessage();
            if (!err.empty()) {
                log_skip(safe_path_string(file), err);
            } else {
                // Only print NOT MQA if no error occurred
                {
                    std::lock_guard<std::mutex> lock(console_mutex);
                    std::cout << std::setw(3) << index << "\tNOT MQA \t" << filename << "\n";
                    std::cout.flush();
                }
                log_message("[NOT MQA] " + safe_path_string(file));
            }
        }
        std::cerr << "[FUNC] Incrementing counter\n"; std::cerr.flush();
        scanned_count++;
        std::cerr << "[FUNC] processFile completed successfully\n"; std::cerr.flush();
        
        // Cleanup
        if (id_ptr) delete id_ptr;
        
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cerr << "ERROR processing file: " << filename_str << " - Exception: " << e.what() << "\n";
            std::cerr.flush();
        }
        try {
            log_skip(filename_str, std::string("Exception: ") + e.what());
        } catch (...) {}
        try {
            scanned_count++;
        } catch (...) {}
        if (id_ptr) delete id_ptr;
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cerr << "UNKNOWN ERROR processing file: " << filename_str << "\n";
            std::cerr.flush();
        }
        try {
            log_skip(filename_str, "Unknown exception");
        } catch (...) {}
        try {
            scanned_count++;
        } catch (...) {}
        if (id_ptr) delete id_ptr;
    }
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Set console code page to UTF-8 to handle special characters in paths
    SetConsoleOutputCP(65001);
    
    // Set up structured exception handler for crashes in FLAC library
    _set_se_translator(translate_se_exception);
#endif

    try {
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
    std::cout.flush();
    std::cerr << "[DEBUG] About to process " << files.size() << " files\n";
    std::cerr.flush();

    std::cout << "  #\tEncoding\tName\n";

    // Process files sequentially (no threading - simpler and more reliable)
    std::cerr << "[DEBUG] Processing files sequentially\n"; std::cerr.flush();
    
    int count = 0;
    
    std::cerr << "[DEBUG] Starting file loop with " << files.size() << " files\n";
    std::cerr.flush();
    
    for (const auto &file : files) {
        try {
            std::cerr << "[DEBUG] Processing file #" << (count+1) << ": " << file.filename().string() << "\n"; std::cerr.flush();
            processFile(file, ++count);
            std::cerr << "[DEBUG] File #" << count << " completed successfully\n"; std::cerr.flush();
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Exception in main loop for file #" << count << ": " << e.what() << "\n"; std::cerr.flush();
        } catch (...) {
            std::cerr << "[ERROR] Unknown exception in main loop for file #" << count << "\n"; std::cerr.flush();
        }
    }
    
    std::cerr << "[DEBUG] File loop completed\n"; std::cerr.flush();

    std::cerr << "[DEBUG] All files processed\n";
    std::cerr.flush();
    
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
    
    std::cerr << "[DEBUG] Program completed successfully\n";
    std::cerr.flush();
    return 0;
    
    } catch (const std::exception& e) {
        std::cerr << "\n[FATAL ERROR] Uncaught exception: " << e.what() << "\n";
        std::cerr.flush();
        return 1;
    } catch (...) {
        std::cerr << "\n[FATAL ERROR] Unknown uncaught exception\n";
        std::cerr.flush();
        return 1;
    }
}

