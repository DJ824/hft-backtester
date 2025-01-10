#include "cloud_storage.h"
#include <aws/core/utils/Outcome.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <fstream>
#include <iostream>
#include <iomanip>

CloudStorage::CloudStorage(const std::string& bucket_name,
                         const std::filesystem::path& cache_dir)
    : bucket_name_(bucket_name), cache_dir_(cache_dir) {
    
    init_aws();
    
    std::filesystem::create_directories(cache_dir_);
}

CloudStorage::~CloudStorage() {
    cleanup_aws();
}

void CloudStorage::init_aws() {
    Aws::InitAPI(options_);
    
    Aws::Client::ClientConfiguration config;
    config.region = "us-east-1"; // Change to your region
    config.connectTimeoutMs = 30000;
    config.requestTimeoutMs = 30000;
    
    s3_client_ = std::make_shared<Aws::S3::S3Client>(config);
}

void CloudStorage::cleanup_aws() {
    s3_client_ = nullptr;
    Aws::ShutdownAPI(options_);
}

bool CloudStorage::upload_file(const std::string& local_path,
                             const std::string& cloud_path) {
    try {
        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(cloud_path);

        std::shared_ptr<Aws::IOStream> input_data = 
            Aws::MakeShared<Aws::FStream>("UploadInputStream",
                                         local_path.c_str(),
                                         std::ios_base::in | std::ios_base::binary);

        request.SetBody(input_data);

        auto outcome = s3_client_->PutObject(request);
        return outcome.IsSuccess();
    }
    catch (const std::exception& e) {
        std::cerr << "Error uploading file: " << e.what() << std::endl;
        return false;
    }
}

bool CloudStorage::download_file(const std::string& cloud_path,
                                 const std::string& local_path,
                                 bool show_progress) {
    std::cout << "Download: Initializing request..." << std::endl;
    try {
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(cloud_path);

        std::cout << "Download: Making S3 request..." << std::endl;
        auto outcome = s3_client_->GetObject(request);

        if (!outcome.IsSuccess()) {
            std::cerr << "Download: Failed - "
                      << outcome.GetError().GetMessage() << std::endl;
            return false;
        }

        std::cout << "Download: Opening output file..." << std::endl;
        std::string temp_path = local_path + ".tmp";
        std::ofstream output_file(temp_path, std::ios::binary);

        if (!output_file) {
            std::cerr << "Download: Failed to open temp file: " << temp_path << std::endl;
            return false;
        }

        std::cout << "Download: Starting file transfer..." << std::endl;
        auto& retrieved_file = outcome.GetResult().GetBody();
        size_t file_size = outcome.GetResult().GetContentLength();
        size_t bytes_read = 0;
        std::vector<char> buffer(DOWNLOAD_CHUNK_SIZE);

        while (retrieved_file && output_file) {
            retrieved_file.read(buffer.data(), buffer.size());
            std::streamsize chunk_size = retrieved_file.gcount();

            if (chunk_size > 0) {
                output_file.write(buffer.data(), chunk_size);
                bytes_read += chunk_size;

                if (show_progress && file_size > 0) {
                    float percentage = (float)bytes_read / file_size * 100;
                    std::cout << "\rDownloading: " << std::fixed
                              << std::setprecision(1) << percentage << "% ("
                              << bytes_read << "/" << file_size << " bytes)"
                              << std::flush;
                }
            }
        }

        std::cout << "\nDownload: Finalizing file..." << std::endl;
        output_file.close();

        if (bytes_read != file_size) {
            std::cerr << "Download: Size mismatch - expected " << file_size
                      << " but got " << bytes_read << " bytes" << std::endl;
            std::filesystem::remove(temp_path);
            return false;
        }

        std::cout << "Download: Moving temp file to final location..." << std::endl;
        // If file exists, remove it first
        if (std::filesystem::exists(local_path)) {
            std::filesystem::remove(local_path);
        }

        std::filesystem::rename(temp_path, local_path);

        std::cout << "Download: Complete" << std::endl;
        return true;
    }
    catch (const Aws::S3::S3Error& e) {
        std::cerr << "AWS S3 Error in download_file: "
                  << e.GetExceptionName() << " - "
                  << e.GetMessage() << std::endl;
        return false;
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error in download_file: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Standard exception in download_file: " << e.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "Unknown error in download_file" << std::endl;
        return false;
    }
}

bool CloudStorage::download_with_progress(const std::string& cloud_path,
                                        const std::string& local_path,
                                        const ProgressCallback& progress_cb) {
    try {
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(cloud_path);

        auto outcome = s3_client_->GetObject(request);
        if (!outcome.IsSuccess()) {
            std::cerr << "Failed to download file: " 
                     << outcome.GetError().GetMessage() << std::endl;
            return false;
        }

        std::ofstream output_file(local_path, std::ios::binary);
        auto& retrieved_file = outcome.GetResultWithOwnership().GetBody();

        size_t file_size = outcome.GetResult().GetContentLength();
        size_t bytes_read = 0;
        char buffer[DOWNLOAD_CHUNK_SIZE];

        while (retrieved_file) {
            retrieved_file.read(buffer, DOWNLOAD_CHUNK_SIZE);
            size_t chunk_size = retrieved_file.gcount();
            
            if (chunk_size > 0) {
                output_file.write(buffer, chunk_size);
                bytes_read += chunk_size;
                
                if (progress_cb) {
                    progress_cb(bytes_read, file_size);
                }
            }
        }

        if (progress_cb) {
            std::cout << std::endl;
        }

        output_file.close();
        return verify_file_integrity(local_path);
    }
    catch (const std::exception& e) {
        std::cerr << "Error downloading file: " << e.what() << std::endl;
        return false;
    }
}

bool CloudStorage::file_exists(const std::string& cloud_path) {
    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(cloud_path);

    auto outcome = s3_client_->HeadObject(request);
    return outcome.IsSuccess();
}

std::vector<std::string> CloudStorage::list_files(const std::string& prefix) {
    std::vector<std::string> files;
    Aws::S3::Model::ListObjectsRequest request;
    request.SetBucket(bucket_name_);
    if (!prefix.empty()) {
        request.SetPrefix(prefix);
    }

    auto outcome = s3_client_->ListObjects(request);
    if (outcome.IsSuccess()) {
        for (const auto& object : outcome.GetResult().GetContents()) {
            files.push_back(object.GetKey());
        }
    }

    return files;
}

bool CloudStorage::is_in_cache(const std::string& date) const {
    return std::filesystem::exists(get_cache_path(date));
}

std::filesystem::path CloudStorage::get_cache_path(const std::string& date) const {
    return cache_dir_ / ("es_" + date + ".csv");
}

void CloudStorage::clear_cache() {
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir_)) {
        std::filesystem::remove(entry.path());
    }
}

void CloudStorage::manage_cache_size() {
    size_t total_size = get_cache_size();
    
    if (total_size > MAX_CACHE_SIZE) {
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> 
            cache_files;

        for (const auto& entry : std::filesystem::directory_iterator(cache_dir_)) {
            if (entry.is_regular_file()) {
                cache_files.emplace_back(entry.path(), 
                                       entry.last_write_time());
            }
        }

        std::sort(cache_files.begin(), cache_files.end(),
                 [](const auto& a, const auto& b) {
                     return a.second < b.second;
                 });

        for (const auto& [path, time] : cache_files) {
            if (total_size <= MAX_CACHE_SIZE) break;
            
            total_size -= std::filesystem::file_size(path);
            std::filesystem::remove(path);
        }
    }
}

size_t CloudStorage::get_cache_size() const {
    size_t total_size = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir_)) {
        if (entry.is_regular_file()) {
            total_size += entry.file_size();
        }
    }
    return total_size;
}

bool CloudStorage::verify_file_integrity(const std::string& file_path) const {
    std::filesystem::path path(file_path);
    return std::filesystem::exists(path) && 
           std::filesystem::file_size(path) > 0;
}