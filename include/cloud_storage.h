#pragma once

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <filesystem>
#include <string>
#include <memory>
#include <functional>

class CloudStorage {
public:
    using ProgressCallback = std::function<void(size_t bytes_transferred, size_t total_bytes)>;

private:
    Aws::SDKOptions options_;
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    std::string bucket_name_;
    std::filesystem::path cache_dir_;

    static constexpr size_t MAX_CACHE_SIZE = 10ULL * 1024 * 1024 * 1024;
    static constexpr size_t DOWNLOAD_CHUNK_SIZE = 8 * 1024 * 1024;

public:
    CloudStorage(const std::string& bucket_name,
                 const std::filesystem::path& cache_dir);
    ~CloudStorage();

    CloudStorage(const CloudStorage&) = delete;
    CloudStorage& operator=(const CloudStorage&) = delete;

    bool upload_file(const std::string& local_path,
                     const std::string& cloud_path);

    bool download_file(const std::string& cloud_path,
                       const std::string& local_path,
                       bool show_progress = true);

    bool file_exists(const std::string& cloud_path);

    std::vector<std::string> list_files(const std::string& prefix = "");

    void clear_cache();
    bool is_in_cache(const std::string& date) const;
    std::filesystem::path get_cache_path(const std::string& date) const;

    Aws::S3::Model::HeadObjectOutcome get_object_info(const Aws::S3::Model::HeadObjectRequest& request) {
        return s3_client_->HeadObject(request);
    }

private:
    void init_aws();
    void cleanup_aws();
    void manage_cache_size();
    bool verify_file_integrity(const std::string& file_path) const;
    size_t get_cache_size() const;

    bool download_with_progress(const std::string& cloud_path,
                                const std::string& local_path,
                                const ProgressCallback& progress_cb);
};