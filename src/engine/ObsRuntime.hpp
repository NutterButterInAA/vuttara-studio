#pragma once

#include "ObsAbi.hpp"

#include <QString>

namespace Vuttara {

class ObsRuntime final
{
public:
    ObsRuntime() = default;
    ~ObsRuntime();

    ObsRuntime(const ObsRuntime&) = delete;
    ObsRuntime& operator=(const ObsRuntime&) = delete;

    bool load(const QString& runtimeRoot, QString* errorMessage);
    void unload();

    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] const ObsAbi::Api& api() const;
    [[nodiscard]] const QString& runtimeRoot() const;
    [[nodiscard]] const QString& binaryDirectory() const;
    [[nodiscard]] const QString& pluginDirectory() const;
    [[nodiscard]] const QString& dataDirectory() const;
    [[nodiscard]] QString versionString() const;

private:
    void* library_ = nullptr;
    void* dllDirectoryCookie_ = nullptr;
    ObsAbi::Api api_{};
    QString runtimeRoot_;
    QString binaryDirectory_;
    QString pluginDirectory_;
    QString dataDirectory_;
};

}
