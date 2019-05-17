/*
 * Project: curve
 * Created Date: Wednesday March 20th 2019
 * Author: yangyaokai
 * Copyright (c) 2018 netease
 */

#include "src/chunkserver/clone_copyer.h"

namespace curve {
namespace chunkserver {

OriginCopyer::OriginCopyer(std::shared_ptr<FileClient> curveClient,
                           std::shared_ptr<S3Adapter> s3Client)
    : curveClient_(curveClient)
    , s3Client_(s3Client) {}

int OriginCopyer::Init(const CopyerOptions& options) {
    int errorCode = curveClient_->Init(options.curveConf.c_str());
    if (errorCode != 0) {
        LOG(ERROR) << "Init curve client failed."
                   << "error code: " << errorCode;
        return -1;
    }
    s3Client_->Init(options.s3Conf);
    curveUser_ = options.curveUser;
    return 0;
}

int OriginCopyer::Fini() {
    for (auto &pair : fdMap_) {
        curveClient_->Close(pair.second);
    }
    curveClient_->UnInit();
    s3Client_->Deinit();
    return 0;
}

int OriginCopyer::Download(const string& location,
                           off_t off,
                           size_t size,
                           char* buf) {
    std::string originPath;
    OriginType type = LocationOperator::ParseLocation(location, &originPath);
    if (type == OriginType::CurveOrigin) {
        off_t chunkOffset;
        std::string fileName;
        bool ret = LocationOperator::ParseCurveChunkPath(
            originPath, &fileName, &chunkOffset);
        return DownloadFromCurve(fileName, chunkOffset + off, size, buf);
    } else if (type == OriginType::S3Origin) {
        return DownloadFromS3(originPath, off, size, buf);
    } else {
        LOG(ERROR) << "Unknown origin location."
                   << "location: " << location;
        return -1;
    }
}

int OriginCopyer::DownloadFromS3(const string& objectName,
                                     off_t off,
                                     size_t size,
                                     char* buf) {
    int ret = s3Client_->GetObject(objectName, buf, off, size);
    if (ret < 0) {
        LOG(ERROR) << "Failed to get s3 object."
                   << "objectName: " << objectName
                   << ", ret: " << ret;
    }
    return ret;
}

int OriginCopyer::DownloadFromCurve(const string& fileName,
                                        off_t off,
                                        size_t size,
                                        char* buf) {
    int fd = 0;
    {
        std::unique_lock<std::mutex> lock(mtx_);
        auto iter = fdMap_.find(fileName);
        if (iter != fdMap_.end()) {
            fd = iter->second;
        } else {
            fd = curveClient_->Open(fileName, curveUser_);
            if (fd < 0) {
                LOG(ERROR) << "Open curve file failed."
                        << "file name: " << fileName
                        << " ,return code: " << fd;
                return -1;
            }
            fdMap_[fileName] = fd;
        }
    }

    int ret = curveClient_->Read(fd, buf, off, size);
    if (ret < 0) {
        LOG(ERROR) << "Read curve file failed."
                   << "file name: " << fileName
                   << " ,error code: " << ret;
        return -1;
    }
    return 0;
}

}  // namespace chunkserver
}  // namespace curve